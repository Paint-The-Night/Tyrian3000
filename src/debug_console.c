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

static bool console_active = false;

static char console_input[CONSOLE_MAX_INPUT];
static int  console_input_len = 0;

static char console_lines[CONSOLE_MAX_LINES][CONSOLE_MAX_LINE_LEN];
static int  console_line_count = 0;

static char startup_cmd[CONSOLE_MAX_INPUT] = "";
static bool startup_done = false;
static bool pending_screenshot = false;

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

/* ── commands ───────────────────────────────────────────────────────── */

static void cmd_help(void)
{
	console_print("Available commands:");
	console_print("  res        - Display resolution info");
	console_print("  screenshot - Save game screen to /tmp");
	console_print("  clear      - Clear console output");
	console_print("  help       - Show this help");
}

static void cmd_resolution(void)
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

	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
		cmd_help();
	else if (strcmp(cmd, "res") == 0 || strcmp(cmd, "resolution") == 0)
		cmd_resolution();
	else if (strcmp(cmd, "screenshot") == 0 || strcmp(cmd, "ss") == 0)
		cmd_screenshot();
	else if (strcmp(cmd, "clear") == 0)
		cmd_clear();
	else
	{
		char msg[CONSOLE_MAX_LINE_LEN];
		snprintf(msg, sizeof(msg), "Unknown command: %s", cmd);
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

		if (console_line_count == 0)
			console_print("Tyrian 3000 Debug Console. Type 'help'.");
	}
}

void debug_console_handle_keydown(SDL_Scancode scan, SDL_Keymod mod)
{
	(void)mod;

	switch (scan)
	{
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
		execute_command(console_input);
		console_input[0] = '\0';
		console_input_len = 0;
		break;

	case SDL_SCANCODE_BACKSPACE:
		if (console_input_len > 0)
		{
			console_input_len--;
			console_input[console_input_len] = '\0';
		}
		break;

	case SDL_SCANCODE_ESCAPE:
		console_active = false;
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
		console_print("Tyrian 3000 Debug Console. Type 'help'.");
		execute_command(startup_cmd);
		pending_screenshot = true;
	}

	if (!console_active)
		return;

	/* Use a solid panel so title-screen palette effects do not tint the console. */
	fill_rectangle_wh(screen, 0, 0, 320, CONSOLE_HEIGHT, 0);

	/* Bright separator line at the bottom of the console. */
	JE_barBright(screen, 0, CONSOLE_HEIGHT, 319, CONSOLE_HEIGHT);

	/* Draw output lines from bottom up. */
	int max_visible = (CONSOLE_HEIGHT - 14) / CONSOLE_LINE_HEIGHT;
	int start = console_line_count - max_visible;
	if (start < 0)
		start = 0;

	int y = CONSOLE_HEIGHT - 12 - (console_line_count - start) * CONSOLE_LINE_HEIGHT;
	for (int i = start; i < console_line_count; ++i)
	{
		JE_outText(screen, CONSOLE_TEXT_X, y, console_lines[i], 15, 2);
		y += CONSOLE_LINE_HEIGHT;
	}

	/* Draw input line at the bottom of the console area. */
	char input_display[CONSOLE_MAX_INPUT + 4];
	snprintf(input_display, sizeof(input_display), "> %s_", console_input);
	JE_outText(screen, CONSOLE_TEXT_X, CONSOLE_HEIGHT - 10, input_display, 15, 4);
}
