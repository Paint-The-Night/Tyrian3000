/*
 * Tyrian 3000: Debug Console
 * Copyright (C) 2025  Gary Perrigo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "debug_console.h"

#include "fonthand.h"
#include "opentyr.h"
#include "vga256d.h"
#include "video.h"
#include "video_scale.h"

#include <stdio.h>
#include <string.h>

#define CONSOLE_MAX_INPUT   128
#define CONSOLE_MAX_LINES   12
#define CONSOLE_MAX_LINE_LEN 52
#define CONSOLE_HEIGHT      100  /* pixels from top of 320x200 screen */
#define CONSOLE_TEXT_X      4
#define CONSOLE_LINE_HEIGHT 8
#define COMPLETION_MAX_MATCHES 64
#define COMPLETION_MAX_DYNAMIC 64
#define COMPLETION_SIG_LEN 160

static bool console_active = false;

static char console_input[CONSOLE_MAX_INPUT];
static int  console_input_len = 0;

static char console_lines[CONSOLE_MAX_LINES][CONSOLE_MAX_LINE_LEN];
static int  console_line_count = 0;

static char startup_cmd[CONSOLE_MAX_INPUT] = "";
static bool startup_done = false;
static bool pending_screenshot = false;
static int completion_selection = 0;
static char completion_signature[COMPLETION_SIG_LEN] = "";

/* ── helpers ────────────────────────────────────────────────────────── */

static void console_print(const char *text)
{
	/* Scroll up if full. */
	if (console_line_count >= CONSOLE_MAX_LINES)
	{
		for (int i = 0; i < CONSOLE_MAX_LINES - 1; ++i)
			memcpy(console_lines[i], console_lines[i + 1], CONSOLE_MAX_LINE_LEN);
		console_line_count = CONSOLE_MAX_LINES - 1;
	}

	snprintf(console_lines[console_line_count], CONSOLE_MAX_LINE_LEN, "%s", text);
	console_line_count++;
}

typedef enum
{
	COMPLETION_CTX_NONE = 0,
	COMPLETION_CTX_ROOT,
	COMPLETION_CTX_RES,
	COMPLETION_CTX_RES_SET,
	COMPLETION_CTX_RES_MODE
} CompletionContext;

typedef struct
{
	CompletionContext ctx;
	int replace_start;
	int replace_len;
	char fragment[CONSOLE_MAX_INPUT];
	const char *matches[COMPLETION_MAX_MATCHES];
	int match_count;
	char dynamic_values[COMPLETION_MAX_DYNAMIC][24];
	int dynamic_count;
} CompletionInfo;

static void build_completion_info(CompletionInfo *info);
static int completion_selected_index(const CompletionInfo *info);
static bool completion_selection_executes_now(const CompletionInfo *info);
static void execute_command(const char *cmd);

static void completion_reset_state(void)
{
	completion_selection = 0;
	completion_signature[0] = '\0';
}

static bool str_starts_with_ci(const char *text, const char *prefix)
{
	return SDL_strncasecmp(text, prefix, strlen(prefix)) == 0;
}

static bool token_equals_ci(const char *token, int token_len, const char *value)
{
	const size_t value_len = strlen(value);
	return (int)value_len == token_len && SDL_strncasecmp(token, value, value_len) == 0;
}

static void add_completion_match(const char *candidate, const char *fragment, const char **matches, int *match_count)
{
	if (candidate == NULL || fragment == NULL || matches == NULL || match_count == NULL)
		return;
	if (*match_count >= COMPLETION_MAX_MATCHES)
		return;
	if (!str_starts_with_ci(candidate, fragment))
		return;

	for (int i = 0; i < *match_count; ++i)
	{
		if (SDL_strcasecmp(matches[i], candidate) == 0)
			return;
	}

	matches[*match_count] = candidate;
	(*match_count)++;
}

static bool replace_input_range(int start, int old_len, const char *replacement, bool append_space)
{
	const int replace_len = (int)strlen(replacement);
	const int tail_start = start + old_len;
	const int tail_len = console_input_len - tail_start;
	const bool need_space = append_space && (tail_len <= 0 || console_input[tail_start] != ' ');
	const int new_len = start + replace_len + (need_space ? 1 : 0) + tail_len;

	if (start < 0 || old_len < 0 || tail_start < 0 || tail_start > console_input_len)
		return false;
	if (new_len < 0 || new_len >= CONSOLE_MAX_INPUT)
		return false;

	memmove(console_input + start + replace_len + (need_space ? 1 : 0),
	        console_input + tail_start,
	        (size_t)tail_len);
	memcpy(console_input + start, replacement, (size_t)replace_len);
	if (need_space)
		console_input[start + replace_len] = ' ';

	console_input[new_len] = '\0';
	console_input_len = new_len;
	return true;
}

static bool completion_sync_signature(const CompletionInfo *info)
{
	if (info == NULL)
		return false;

	char sig[COMPLETION_SIG_LEN];
	snprintf(sig, sizeof(sig), "%d:%d:%s", info->ctx, info->replace_start, info->fragment);
	const bool unchanged = strcmp(sig, completion_signature) == 0;
	if (!unchanged)
	{
		SDL_strlcpy(completion_signature, sig, sizeof(completion_signature));
		completion_selection = 0;
	}
	if (info->match_count > 0 && completion_selection >= info->match_count)
		completion_selection = 0;
	return unchanged;
}

static void completion_step_selection(int delta)
{
	CompletionInfo info;
	build_completion_info(&info);
	if (info.ctx == COMPLETION_CTX_NONE || info.match_count <= 0)
		return;

	(void)completion_sync_signature(&info);
	int next = completion_selection + delta;
	while (next < 0)
		next += info.match_count;
	while (next >= info.match_count)
		next -= info.match_count;
	completion_selection = next;
}

static void build_completion_info(CompletionInfo *info)
{
	static const char *const root_commands[] = { "resolution", "screenshot", "clear" };
	static const char *const res_commands[] = { "list", "show", "current", "set", "mode" };
	static const char *const mode_values[] = { "center", "integer", "fit8:5", "fit4:3", "list", "show", "current" };

	if (info == NULL)
		return;

	memset(info, 0, sizeof(*info));
	info->ctx = COMPLETION_CTX_NONE;

	int replace_start = 0;
	int replace_len = 0;
	int i = 0;
	while (i < console_input_len && console_input[i] == ' ')
		++i;

	if (i >= console_input_len)
	{
		info->ctx = COMPLETION_CTX_ROOT;
		replace_start = console_input_len;
		replace_len = 0;
	}
	else
	{
		const int first_start = i;
		while (i < console_input_len && console_input[i] != ' ')
			++i;
		const int first_end = i;
		const bool first_is_res =
			token_equals_ci(console_input + first_start, first_end - first_start, "resolution");

		if (i >= console_input_len)
		{
			if (first_is_res)
			{
				info->ctx = COMPLETION_CTX_RES;
				replace_start = console_input_len;
				replace_len = 0;
			}
			else
			{
				info->ctx = COMPLETION_CTX_ROOT;
				replace_start = first_start;
				replace_len = first_end - first_start;
			}
		}
		else
		{
			if (!first_is_res)
				return;

			while (i < console_input_len && console_input[i] == ' ')
				++i;
			const int second_start = i;
			if (second_start >= console_input_len)
			{
				info->ctx = COMPLETION_CTX_RES;
				replace_start = console_input_len;
				replace_len = 0;
			}
			else
			{
				while (i < console_input_len && console_input[i] != ' ')
					++i;
				const int second_end = i;
				const bool second_is_set = token_equals_ci(console_input + second_start, second_end - second_start, "set");
				const bool second_is_mode = token_equals_ci(console_input + second_start, second_end - second_start, "mode");
				if (i >= console_input_len)
				{
					if (second_is_set)
					{
						info->ctx = COMPLETION_CTX_RES_SET;
						replace_start = console_input_len;
						replace_len = 0;
					}
					else if (second_is_mode)
					{
						info->ctx = COMPLETION_CTX_RES_MODE;
						replace_start = console_input_len;
						replace_len = 0;
					}
					else
					{
						info->ctx = COMPLETION_CTX_RES;
						replace_start = second_start;
						replace_len = second_end - second_start;
					}
				}
				else
				{
					while (i < console_input_len && console_input[i] == ' ')
						++i;
					const int third_start = i;
					if (third_start >= console_input_len)
					{
						if (second_is_set)
							info->ctx = COMPLETION_CTX_RES_SET;
						else if (second_is_mode)
							info->ctx = COMPLETION_CTX_RES_MODE;
						else
							return;
						replace_start = console_input_len;
						replace_len = 0;
					}
					else
					{
						while (i < console_input_len && console_input[i] != ' ')
							++i;
						if (i < console_input_len)
							return; /* Keep it simple: only complete the active token. */

						if (second_is_set)
							info->ctx = COMPLETION_CTX_RES_SET;
						else if (second_is_mode)
							info->ctx = COMPLETION_CTX_RES_MODE;
						else
							return;

						replace_start = third_start;
						replace_len = i - third_start;
					}
				}
			}
		}
	}

	if (replace_len >= CONSOLE_MAX_INPUT)
		return;
	info->replace_start = replace_start;
	info->replace_len = replace_len;
	memcpy(info->fragment, console_input + replace_start, (size_t)replace_len);
	info->fragment[replace_len] = '\0';

	switch (info->ctx)
	{
	case COMPLETION_CTX_ROOT:
		for (uint k = 0; k < COUNTOF(root_commands); ++k)
			add_completion_match(root_commands[k], info->fragment, info->matches, &info->match_count);
		break;

	case COMPLETION_CTX_RES:
		for (uint k = 0; k < COUNTOF(res_commands); ++k)
			add_completion_match(res_commands[k], info->fragment, info->matches, &info->match_count);
		/* Keep the "next word" list focused; only add scaler values once user starts typing a value. */
		if (info->fragment[0] != '\0')
		{
			for (uint k = 0; k < scalers_count; ++k)
			{
				add_completion_match(scalers[k].name, info->fragment, info->matches, &info->match_count);
				if (info->dynamic_count < COMPLETION_MAX_DYNAMIC)
				{
					snprintf(info->dynamic_values[info->dynamic_count], sizeof(info->dynamic_values[info->dynamic_count]),
					         "%dx%d", scalers[k].width, scalers[k].height);
					add_completion_match(info->dynamic_values[info->dynamic_count], info->fragment, info->matches, &info->match_count);
					++info->dynamic_count;
				}
			}
		}
		break;

	case COMPLETION_CTX_RES_SET:
		for (uint k = 0; k < scalers_count; ++k)
		{
			add_completion_match(scalers[k].name, info->fragment, info->matches, &info->match_count);
			if (info->dynamic_count < COMPLETION_MAX_DYNAMIC)
			{
				snprintf(info->dynamic_values[info->dynamic_count], sizeof(info->dynamic_values[info->dynamic_count]),
				         "%dx%d", scalers[k].width, scalers[k].height);
				add_completion_match(info->dynamic_values[info->dynamic_count], info->fragment, info->matches, &info->match_count);
				++info->dynamic_count;
			}
		}
		break;

	case COMPLETION_CTX_RES_MODE:
		for (uint k = 0; k < COUNTOF(mode_values); ++k)
			add_completion_match(mode_values[k], info->fragment, info->matches, &info->match_count);
		break;

	case COMPLETION_CTX_NONE:
	default:
		return;
	}
}

static void debug_console_cycle_completion(void)
{
	CompletionInfo info;
	build_completion_info(&info);
	if (info.ctx == COMPLETION_CTX_NONE || info.match_count <= 0)
		return;

	const bool unchanged = completion_sync_signature(&info);
	if (info.match_count == 1)
	{
		(void)replace_input_range(info.replace_start, info.replace_len, info.matches[0], true);
		completion_reset_state();
		return;
	}

	if (unchanged)
		completion_selection = (completion_selection + 1) % info.match_count;
}

static bool debug_console_accept_completion(bool execute_terminal)
{
	CompletionInfo info;
	build_completion_info(&info);
	if (info.ctx == COMPLETION_CTX_NONE || info.match_count <= 0)
		return false;

	(void)completion_sync_signature(&info);
	const int selected = completion_selected_index(&info);
	if (selected < 0)
		return false;

	const bool execute_now = completion_selection_executes_now(&info);
	(void)replace_input_range(info.replace_start, info.replace_len, info.matches[selected], !execute_now);
	if (execute_now && execute_terminal)
	{
		execute_command(console_input);
		console_input[0] = '\0';
		console_input_len = 0;
	}

	completion_reset_state();
	return true;
}

static void build_input_preview(const CompletionInfo *info, char *out, size_t out_size)
{
	if (out == NULL || out_size == 0)
		return;

	SDL_strlcpy(out, console_input, out_size);
	if (info == NULL || info->match_count <= 0)
		return;

	int sel = completion_selection;
	if (sel < 0 || sel >= info->match_count)
		sel = 0;

	const char *candidate = info->matches[sel];
	if (candidate == NULL)
		return;

	const int start = info->replace_start;
	const int old_len = info->replace_len;
	const int input_len = (int)strlen(out);
	const int candidate_len = (int)strlen(candidate);
	const int tail_start = start + old_len;
	const int tail_len = input_len - tail_start;
	const int new_len = start + candidate_len + tail_len;

	if (start < 0 || old_len < 0 || tail_start < 0 || tail_start > input_len)
		return;
	if ((size_t)new_len + 1 > out_size)
		return;

	memmove(out + start + candidate_len, out + tail_start, (size_t)tail_len);
	memcpy(out + start, candidate, (size_t)candidate_len);
	out[new_len] = '\0';
}

static int completion_selected_index(const CompletionInfo *info)
{
	if (info == NULL || info->match_count <= 0)
		return -1;
	if (completion_selection < 0 || completion_selection >= info->match_count)
		return 0;
	return completion_selection;
}

static bool completion_selection_executes_now(const CompletionInfo *info)
{
	if (info == NULL || info->match_count <= 0)
		return false;

	const int selected = completion_selected_index(info);
	if (selected < 0)
		return false;

	const char *choice = info->matches[selected];
	if (choice == NULL)
		return false;

	switch (info->ctx)
	{
	case COMPLETION_CTX_ROOT:
		/* Keep "resolution" as submenu-first; execute one-shot commands immediately. */
		return SDL_strcasecmp(choice, "screenshot") == 0 || SDL_strcasecmp(choice, "clear") == 0;

	case COMPLETION_CTX_RES:
		/* "set" and "mode" open submenus; other values are terminal. */
		return SDL_strcasecmp(choice, "set") != 0 && SDL_strcasecmp(choice, "mode") != 0;

	case COMPLETION_CTX_RES_SET:
	case COMPLETION_CTX_RES_MODE:
		return true;

	case COMPLETION_CTX_NONE:
	default:
		return false;
	}
}

static void debug_console_remove_last_word(void)
{
	int len = console_input_len;
	if (len <= 0)
		return;

	/* Trim trailing spaces. */
	while (len > 0 && console_input[len - 1] == ' ')
		--len;
	/* Remove the previous token. */
	while (len > 0 && console_input[len - 1] != ' ')
		--len;
	/* Trim spaces before that token. */
	while (len > 0 && console_input[len - 1] == ' ')
		--len;

	if (len > 0 && len < CONSOLE_MAX_INPUT - 1)
		console_input[len++] = ' ';

	console_input[len] = '\0';
	console_input_len = len;
	completion_reset_state();
}

/* ── commands ───────────────────────────────────────────────────────── */

static bool parse_scaling_mode_name(const char *raw, ScalingMode *out_mode)
{
	if (raw == NULL || out_mode == NULL)
		return false;

	for (int i = 0; i < ScalingMode_MAX; ++i)
	{
		if (SDL_strcasecmp(raw, scaling_mode_names[i]) == 0)
		{
			*out_mode = (ScalingMode)i;
			return true;
		}
	}

	char norm[64];
	size_t j = 0;
	for (size_t i = 0; raw[i] != '\0' && j + 1 < sizeof(norm); ++i)
	{
		char c = raw[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (c == ' ' || c == '_' || c == '-' || c == ':' || c == '/')
			continue;
		norm[j++] = c;
	}
	norm[j] = '\0';

	if (strcmp(norm, "center") == 0 || strcmp(norm, "centre") == 0)
	{
		*out_mode = SCALE_CENTER;
		return true;
	}
	if (strcmp(norm, "integer") == 0 || strcmp(norm, "int") == 0)
	{
		*out_mode = SCALE_INTEGER;
		return true;
	}
	if (strcmp(norm, "fit85") == 0 || strcmp(norm, "aspect85") == 0 || strcmp(norm, "85") == 0)
	{
		*out_mode = SCALE_ASPECT_8_5;
		return true;
	}
	if (strcmp(norm, "fit43") == 0 || strcmp(norm, "aspect43") == 0 || strcmp(norm, "43") == 0)
	{
		*out_mode = SCALE_ASPECT_4_3;
		return true;
	}

	return false;
}

static void cmd_resolution_mode(const char *arg)
{
	if (arg == NULL || *arg == '\0' ||
	    SDL_strcasecmp(arg, "show") == 0 ||
	    SDL_strcasecmp(arg, "current") == 0)
	{
		char buf[CONSOLE_MAX_LINE_LEN];
		snprintf(buf, sizeof(buf), "Mode: %s", scaling_mode_names[scaling_mode]);
		console_print(buf);
		return;
	}

	if (SDL_strcasecmp(arg, "list") == 0)
	{
		console_print("Modes:");
		console_print("  Center, Integer, Fit 8:5, Fit 4:3");
		return;
	}

	ScalingMode new_mode = scaling_mode;
	if (!parse_scaling_mode_name(arg, &new_mode))
	{
		console_print("Unknown mode. Try: resolution mode list");
		return;
	}

	scaling_mode = new_mode;
	char buf[CONSOLE_MAX_LINE_LEN];
	snprintf(buf, sizeof(buf), "Mode: %s", scaling_mode_names[scaling_mode]);
	console_print(buf);
}

static void cmd_resolution_show(void)
{
	char buf[CONSOLE_MAX_LINE_LEN];
	int win_w = 0, win_h = 0;

	if (main_window)
		SDL_GetWindowSize(main_window, &win_w, &win_h);

	snprintf(buf, sizeof(buf), "Game:   %dx%d", vga_width, vga_height);
	console_print(buf);

	snprintf(buf, sizeof(buf), "Scaler: %s (%dx%d)",
	         scalers[scaler].name,
	         scalers[scaler].width,
	         scalers[scaler].height);
	console_print(buf);

	snprintf(buf, sizeof(buf), "Window: %dx%d", win_w, win_h);
	console_print(buf);

	snprintf(buf, sizeof(buf), "Mode:   %s  Display: %s",
	         scaling_mode_names[scaling_mode],
		 fullscreen_display >= 0 ? "Fullscreen" : "Windowed");
	console_print(buf);
}

static void cmd_resolution_list(void)
{
	for (uint i = 0; i < scalers_count; ++i)
	{
		char buf[CONSOLE_MAX_LINE_LEN];
		snprintf(buf, sizeof(buf), "%s: %dx%d", scalers[i].name, scalers[i].width, scalers[i].height);
		console_print(buf);
	}
}

static void cmd_resolution_set(const char *arg)
{
	if (arg == NULL || *arg == '\0')
	{
		cmd_resolution_show();
		return;
	}

	char value[CONSOLE_MAX_INPUT];
	SDL_strlcpy(value, arg, sizeof(value));

	/* Trim leading/trailing spaces from the argument. */
	char *trimmed = value;
	while (*trimmed == ' ')
		++trimmed;
	char *end = trimmed + strlen(trimmed);
	while (end > trimmed && end[-1] == ' ')
		*--end = '\0';

	if (*trimmed == '\0')
	{
		cmd_resolution_show();
		return;
	}

	/* Accept "resolution set <value>" in addition to "resolution <value>". */
	if (SDL_strncasecmp(trimmed, "set ", 4) == 0)
	{
		trimmed += 4;
		while (*trimmed == ' ')
			++trimmed;
		if (*trimmed == '\0')
		{
			cmd_resolution_show();
			return;
		}
	}

	if (SDL_strncasecmp(trimmed, "mode", 4) == 0 &&
	    (trimmed[4] == '\0' || trimmed[4] == ' '))
	{
		char *mode_arg = trimmed + 4;
		while (*mode_arg == ' ')
			++mode_arg;
		if (*mode_arg == '\0')
			mode_arg = NULL;
		cmd_resolution_mode(mode_arg);
		return;
	}

	if (SDL_strcasecmp(trimmed, "show") == 0 || SDL_strcasecmp(trimmed, "current") == 0)
	{
		cmd_resolution_show();
		return;
	}

	if (SDL_strcasecmp(trimmed, "list") == 0)
	{
		cmd_resolution_list();
		return;
	}

	uint target_scaler = scalers_count;

	int width = 0, height = 0;
	if ((sscanf(trimmed, "%dx%d", &width, &height) == 2 ||
	     sscanf(trimmed, "%dX%d", &width, &height) == 2) &&
	    width > 0 && height > 0)
	{
		for (uint i = 0; i < scalers_count; ++i)
		{
			if (scalers[i].width == width && scalers[i].height == height)
			{
				target_scaler = i;
				break;
			}
		}
	}
	else
	{
		for (uint i = 0; i < scalers_count; ++i)
		{
			if (SDL_strcasecmp(trimmed, scalers[i].name) == 0)
			{
				target_scaler = i;
				break;
			}
		}
	}

	if (target_scaler >= scalers_count)
	{
		console_print("Unknown resolution/scaler. Try: resolution list");
		return;
	}

	const uint old_scaler = scaler;
	if (!init_scaler(target_scaler))
	{
		(void)init_scaler(old_scaler);
		console_print("Failed to apply scaler.");
		return;
	}

	char buf[CONSOLE_MAX_LINE_LEN];
	snprintf(buf, sizeof(buf), "Scaler: %s (%dx%d)",
	         scalers[scaler].name,
	         scalers[scaler].width,
	         scalers[scaler].height);
	console_print(buf);
}

static void cmd_clear(void)
{
	console_line_count = 0;
}

static void cmd_screenshot(void)
{
	const char *path = "/tmp/tyrian_screenshot.bmp";
	if (video_save_surface_rgb(VGAScreen, path))
	{
		char buf[CONSOLE_MAX_LINE_LEN];
		snprintf(buf, sizeof(buf), "Saved: %s", path);
		console_print(buf);
	}
	else
	{
		console_print("Screenshot failed.");
	}
}

static void execute_command(const char *cmd)
{
	/* Echo the command. */
	char echo[CONSOLE_MAX_LINE_LEN];
	snprintf(echo, sizeof(echo), "> %s", cmd);
	console_print(echo);

	/* Skip leading spaces. */
	while (*cmd == ' ')
		cmd++;

	if (*cmd == '\0')
		return;

	char command_buf[CONSOLE_MAX_INPUT];
	SDL_strlcpy(command_buf, cmd, sizeof(command_buf));

	char *verb = command_buf;
	char *arg = strchr(command_buf, ' ');
	if (arg != NULL)
	{
		*arg = '\0';
		++arg;
		while (*arg == ' ')
			++arg;
		if (*arg == '\0')
			arg = NULL;
	}

	if (strcmp(verb, "resolution") == 0)
		cmd_resolution_set(arg);
	else if (strcmp(verb, "screenshot") == 0)
		cmd_screenshot();
	else if (strcmp(verb, "clear") == 0)
		cmd_clear();
	else
	{
		char msg[CONSOLE_MAX_LINE_LEN];
		snprintf(msg, sizeof(msg), "Unknown command: %s", verb);
		console_print(msg);
	}
}

/* ── public API ─────────────────────────────────────────────────────── */

void debug_console_execute_command(const char *cmd)
{
	execute_command(cmd);
}

const char *debug_console_get_last_line(void)
{
	if (console_line_count <= 0)
		return "";
	return console_lines[console_line_count - 1];
}

bool debug_console_is_active(void)
{
	return console_active;
}

void debug_console_toggle(void)
{
	console_active = !console_active;

	if (console_active)
	{
		console_input[0] = '\0';
		console_input_len = 0;
		completion_reset_state();

		console_print("Tyrian 3000 Debug Console.");
	}
}

void debug_console_handle_keydown(SDL_Scancode scan, SDL_Keymod mod)
{
	(void)mod;

	switch (scan)
	{
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
	{
		if (debug_console_accept_completion(true))
			break;

		execute_command(console_input);
		console_input[0] = '\0';
		console_input_len = 0;
		completion_reset_state();
		break;
	}

	case SDL_SCANCODE_BACKSPACE:
		if (console_input_len > 0)
		{
			console_input_len--;
			console_input[console_input_len] = '\0';
			completion_reset_state();
		}
		break;

	case SDL_SCANCODE_TAB:
		debug_console_cycle_completion();
		break;

	case SDL_SCANCODE_UP:
		completion_step_selection(-1);
		break;

	case SDL_SCANCODE_DOWN:
		completion_step_selection(1);
		break;

	case SDL_SCANCODE_RIGHT:
		(void)debug_console_accept_completion(true);
		break;

	case SDL_SCANCODE_LEFT:
		debug_console_remove_last_word();
		break;

	case SDL_SCANCODE_ESCAPE:
		console_active = false;
		completion_reset_state();
		break;

	default:
		break;
	}
}

void debug_console_handle_textinput(const char *text)
{
	/* Ignore the backtick that opened the console. */
	if (text[0] == '`' || text[0] == '~')
		return;

	size_t len = strlen(text);
	if (console_input_len + (int)len < CONSOLE_MAX_INPUT - 1)
	{
		memcpy(console_input + console_input_len, text, len);
		console_input_len += (int)len;
		console_input[console_input_len] = '\0';
		completion_reset_state();
	}
}

void debug_console_set_startup_command(const char *cmd)
{
	snprintf(startup_cmd, sizeof(startup_cmd), "%s", cmd);
}

bool debug_console_wants_screenshot(void)
{
	if (pending_screenshot)
	{
		pending_screenshot = false;
		return true;
	}
	return false;
}

void debug_console_draw(SDL_Surface *screen)
{
	/* Auto-exec startup command on first draw. */
	if (startup_cmd[0] != '\0' && !startup_done)
	{
		startup_done = true;
		console_active = true;
		console_input[0] = '\0';
		console_input_len = 0;
		completion_reset_state();
		console_print("Tyrian 3000 Debug Console.");
		execute_command(startup_cmd);
		pending_screenshot = true;
	}

	if (!console_active)
		return;

	/* Use a solid panel so title-screen palette effects do not tint the console. */
	fill_rectangle_wh(screen, 0, 0, 320, CONSOLE_HEIGHT, 0);

	/* Bright separator line at the bottom of the console. */
	JE_barBright(screen, 0, CONSOLE_HEIGHT, 319, CONSOLE_HEIGHT);

	CompletionInfo completion;
	build_completion_info(&completion);
	if (completion.ctx != COMPLETION_CTX_NONE)
		(void)completion_sync_signature(&completion);

	/* Top section: use full space for autocomplete options. */
	const int options_top = 2;
	const int options_bottom = CONSOLE_HEIGHT - 12;
	const int option_rows = (options_bottom - options_top) / CONSOLE_LINE_HEIGHT;
	if (option_rows > 0)
	{
		char header[CONSOLE_MAX_LINE_LEN];
		snprintf(header, sizeof(header), "Options (%d) Up/Down Tab select Enter/-> use <- back", completion.match_count);
		JE_outText(screen, CONSOLE_TEXT_X, options_top, header, 14, 4);

		const int list_rows = option_rows - 1;
		if (list_rows > 0 && completion.match_count > 0)
		{
			int selected = completion_selection;
			if (selected < 0 || selected >= completion.match_count)
				selected = 0;

			int first = 0;
			if (completion.match_count > list_rows)
			{
				first = selected - list_rows / 2;
				if (first < 0)
					first = 0;
				if (first > completion.match_count - list_rows)
					first = completion.match_count - list_rows;
			}

			int y = options_top + CONSOLE_LINE_HEIGHT;
			for (int row = 0; row < list_rows; ++row)
			{
				const int idx = first + row;
				if (idx >= completion.match_count)
					break;

				char line[CONSOLE_MAX_LINE_LEN];
				snprintf(line, sizeof(line), "%c %s", idx == selected ? '>' : ' ', completion.matches[idx]);
				JE_outText(screen, CONSOLE_TEXT_X, y, line, idx == selected ? 15 : 14, 2);
				y += CONSOLE_LINE_HEIGHT;
			}
		}
		else if (list_rows > 0)
		{
			JE_outText(screen, CONSOLE_TEXT_X, options_top + CONSOLE_LINE_HEIGHT, "  (no matches)", 12, 2);
		}
	}

	/* Draw input line at the bottom of the console area. */
	char input_preview[CONSOLE_MAX_INPUT];
	build_input_preview(&completion, input_preview, sizeof(input_preview));

	char input_display[CONSOLE_MAX_INPUT + 4];
	snprintf(input_display, sizeof(input_display), "> %s_", input_preview);
	JE_outText(screen, CONSOLE_TEXT_X, CONSOLE_HEIGHT - 10, input_display, 15, 4);
}
