/*
 * Tyrian 3000: Remote Control
 * Copyright (C) 2026  Gary Perrigo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#include "remote_control.h"

#include "debug_console.h"
#include "video.h"
#include "video_scale.h"

#ifndef TARGET_WIN32

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REMOTE_DEFAULT_SOCKET_PATH "/tmp/tyrian3000-remote.sock"
#define REMOTE_RX_BUF_SIZE 4096
#define REMOTE_CONTEXT_SIZE 64

typedef enum
{
	REMOTE_PENDING_NONE = 0,
	REMOTE_PENDING_WAIT_FRAMES,
	REMOTE_PENDING_SCREENSHOT
} RemotePendingType;

static bool remote_enabled = false;
static bool remote_initialized = false;

static int listen_fd = -1;
static int client_fd = -1;

static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = REMOTE_DEFAULT_SOCKET_PATH;
static char ui_context[REMOTE_CONTEXT_SIZE] = "unknown";

static char rx_buf[REMOTE_RX_BUF_SIZE];
static size_t rx_len = 0;

static Uint64 frame_counter = 0;
static SDL_Surface *last_presented_surface = NULL;
static Uint32 next_accept_poll_ticks = 0;

static struct
{
	RemotePendingType type;
	int frames_left;
	char screenshot_path[PATH_MAX];
} pending = { REMOTE_PENDING_NONE, 0, { 0 } };

static int set_nonblocking(const int fd)
{
	const int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_client(void)
{
	if (client_fd >= 0)
	{
		close(client_fd);
		client_fd = -1;
	}

	rx_len = 0;
	pending.type = REMOTE_PENDING_NONE;
	pending.frames_left = 0;
	pending.screenshot_path[0] = '\0';
}

static bool write_all(const char *data, size_t len)
{
	while (len > 0)
	{
		const ssize_t wrote = send(client_fd, data, len, 0);
		if (wrote < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return false;
			close_client();
			return false;
		}

		data += wrote;
		len -= (size_t)wrote;
	}

	return true;
}

static void remote_reply_raw(const char *json_line)
{
	if (client_fd < 0)
		return;

	write_all(json_line, strlen(json_line));
	write_all("\n", 1);
	close_client();
}

static void remote_reply_ok(void)
{
	remote_reply_raw("{\"ok\":true}");
}

static void remote_reply_error(const char *msg)
{
	char safe_msg[200];
	size_t j = 0;
	for (size_t i = 0; msg[i] != '\0' && j < sizeof(safe_msg) - 1; ++i)
	{
		char c = msg[i];
		if (c == '"' || c == '\\' || c == '\n' || c == '\r')
			c = ' ';
		safe_msg[j++] = c;
	}
	safe_msg[j] = '\0';

	char json[256];
	snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", safe_msg);
	remote_reply_raw(json);
}

static bool json_extract_string(const char *json, const char *key, char *out, size_t out_size)
{
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);

	const char *p = strstr(json, pattern);
	if (p == NULL)
		return false;

	p += strlen(pattern);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		++p;
	if (*p != ':')
		return false;
	++p;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		++p;
	if (*p != '"')
		return false;
	++p;

	size_t j = 0;
	for (; *p != '\0' && *p != '"'; ++p)
	{
		if (*p == '\\')
		{
			++p;
			if (*p == '\0')
				break;
		}

		if (j + 1 < out_size)
			out[j++] = *p;
	}

	if (*p != '"')
		return false;

	out[j] = '\0';
	return true;
}

static bool json_extract_int(const char *json, const char *key, int *out)
{
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);

	const char *p = strstr(json, pattern);
	if (p == NULL)
		return false;

	p += strlen(pattern);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		++p;
	if (*p != ':')
		return false;
	++p;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		++p;

	char *end = NULL;
	long value = strtol(p, &end, 10);
	if (end == p)
		return false;

	*out = (int)value;
	return true;
}

static SDL_Scancode parse_scancode_name(const char *name)
{
	char lower[64];
	size_t i = 0;
	for (; name[i] != '\0' && i < sizeof(lower) - 1; ++i)
	{
		char c = name[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		lower[i] = c;
	}
	lower[i] = '\0';

	if (strcmp(lower, "up") == 0)
		return SDL_SCANCODE_UP;
	if (strcmp(lower, "down") == 0)
		return SDL_SCANCODE_DOWN;
	if (strcmp(lower, "left") == 0)
		return SDL_SCANCODE_LEFT;
	if (strcmp(lower, "right") == 0)
		return SDL_SCANCODE_RIGHT;
	if (strcmp(lower, "enter") == 0 || strcmp(lower, "return") == 0)
		return SDL_SCANCODE_RETURN;
	if (strcmp(lower, "esc") == 0 || strcmp(lower, "escape") == 0)
		return SDL_SCANCODE_ESCAPE;
	if (strcmp(lower, "space") == 0)
		return SDL_SCANCODE_SPACE;
	if (strcmp(lower, "grave") == 0 || strcmp(lower, "backtick") == 0 || strcmp(lower, "`") == 0)
		return SDL_SCANCODE_GRAVE;
	if (strcmp(lower, "backspace") == 0)
		return SDL_SCANCODE_BACKSPACE;
	if (strcmp(lower, "tab") == 0)
		return SDL_SCANCODE_TAB;

	SDL_Scancode scan = SDL_GetScancodeFromName(name);
	if (scan == SDL_SCANCODE_UNKNOWN)
		scan = SDL_GetScancodeFromName(lower);
	return scan;
}

static bool push_key_event(Uint32 type, SDL_Scancode scan)
{
	SDL_Event ev;
	SDL_memset(&ev, 0, sizeof(ev));

	ev.type = type;
	ev.key.type = type;
	ev.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
	ev.key.repeat = 0;
	ev.key.keysym.scancode = scan;
	ev.key.keysym.sym = SDL_GetKeyFromScancode(scan);
	ev.key.keysym.mod = KMOD_NONE;
	ev.key.windowID = main_window ? SDL_GetWindowID(main_window) : 0;

	return SDL_PushEvent(&ev) == 1;
}

static bool push_text_event(const char *text)
{
	SDL_Event ev;
	SDL_memset(&ev, 0, sizeof(ev));

	ev.type = SDL_TEXTINPUT;
	ev.text.type = SDL_TEXTINPUT;
	ev.text.windowID = main_window ? SDL_GetWindowID(main_window) : 0;
	SDL_strlcpy(ev.text.text, text, sizeof(ev.text.text));

	return SDL_PushEvent(&ev) == 1;
}

static void remote_reply_state(void)
{
	char context_safe[REMOTE_CONTEXT_SIZE];
	SDL_strlcpy(context_safe, ui_context, sizeof(context_safe));
	for (size_t i = 0; context_safe[i] != '\0'; ++i)
	{
		if (context_safe[i] == '"' || context_safe[i] == '\\')
			context_safe[i] = '_';
	}

	char json[512];
	snprintf(
		json,
		sizeof(json),
		"{\"ok\":true,\"frame\":%" PRIu64 ",\"context\":\"%s\",\"console_active\":%s,"
		"\"scaler_index\":%u,\"scaler_name\":\"%s\",\"scaling_mode\":\"%s\",\"fullscreen_display\":%d}",
		frame_counter,
		context_safe,
		debug_console_is_active() ? "true" : "false",
		scaler,
		scalers[scaler].name,
		scaling_mode_names[scaling_mode],
		fullscreen_display
	);
	remote_reply_raw(json);
}

static void handle_command(const char *line)
{
	if (client_fd < 0)
		return;

	if (pending.type != REMOTE_PENDING_NONE)
	{
		remote_reply_error("busy");
		return;
	}

	char cmd[64];
	if (!json_extract_string(line, "cmd", cmd, sizeof(cmd)))
	{
		remote_reply_error("missing cmd");
		return;
	}

	if (strcmp(cmd, "ping") == 0)
	{
		remote_reply_raw("{\"ok\":true,\"pong\":true}");
		return;
	}

	if (strcmp(cmd, "get_state") == 0)
	{
		remote_reply_state();
		return;
	}

	if (strcmp(cmd, "send_key") == 0)
	{
		char key_name[64];
		char action[16] = "tap";
		int repeat = 1;

		if (!json_extract_string(line, "key", key_name, sizeof(key_name)))
		{
			remote_reply_error("missing key");
			return;
		}
		(void)json_extract_string(line, "action", action, sizeof(action));
		(void)json_extract_int(line, "repeat", &repeat);
		repeat = MAX(1, repeat);

		const SDL_Scancode scan = parse_scancode_name(key_name);
		if (scan == SDL_SCANCODE_UNKNOWN)
		{
			remote_reply_error("unknown key");
			return;
		}

		bool pushed = true;
		for (int i = 0; i < repeat; ++i)
		{
			if (strcmp(action, "down") == 0)
			{
				pushed = push_key_event(SDL_KEYDOWN, scan) && pushed;
			}
			else if (strcmp(action, "up") == 0)
			{
				pushed = push_key_event(SDL_KEYUP, scan) && pushed;
			}
			else
			{
				pushed = push_key_event(SDL_KEYDOWN, scan) && pushed;
				pushed = push_key_event(SDL_KEYUP, scan) && pushed;
			}
		}

		if (!pushed)
		{
			remote_reply_error("failed to push key event");
			return;
		}

		remote_reply_ok();
		return;
	}

	if (strcmp(cmd, "send_text") == 0)
	{
		char text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
		if (!json_extract_string(line, "text", text, sizeof(text)))
		{
			remote_reply_error("missing text");
			return;
		}

		if (!push_text_event(text))
		{
			remote_reply_error("failed to push text event");
			return;
		}

		remote_reply_ok();
		return;
	}

	if (strcmp(cmd, "console_exec") == 0)
	{
		char command[256];
		if (!json_extract_string(line, "command", command, sizeof(command)))
		{
			remote_reply_error("missing command");
			return;
		}

		debug_console_execute_command(command);

		char output[160];
		SDL_strlcpy(output, debug_console_get_last_line(), sizeof(output));
		for (size_t i = 0; output[i] != '\0'; ++i)
		{
			if (output[i] == '"' || output[i] == '\\')
				output[i] = '_';
		}

		char json[240];
		snprintf(json, sizeof(json), "{\"ok\":true,\"output\":\"%s\"}", output);
		remote_reply_raw(json);
		return;
	}

	if (strcmp(cmd, "wait_frames") == 0)
	{
		int frames = 1;
		(void)json_extract_int(line, "frames", &frames);
		if (frames <= 0)
		{
			remote_reply_ok();
			return;
		}

		pending.type = REMOTE_PENDING_WAIT_FRAMES;
		pending.frames_left = frames;
		return;
	}

	if (strcmp(cmd, "screenshot") == 0)
	{
		char path[PATH_MAX];
		if (!json_extract_string(line, "path", path, sizeof(path)))
			SDL_strlcpy(path, "/tmp/tyrian3000-remote.bmp", sizeof(path));

		if (last_presented_surface != NULL && video_save_surface_rgb(last_presented_surface, path))
		{
			char safe_path[PATH_MAX];
			SDL_strlcpy(safe_path, path, sizeof(safe_path));
			for (size_t i = 0; safe_path[i] != '\0'; ++i)
			{
				if (safe_path[i] == '"' || safe_path[i] == '\\')
					safe_path[i] = '_';
			}

			char json[PATH_MAX + 64];
			snprintf(json, sizeof(json), "{\"ok\":true,\"path\":\"%s\"}", safe_path);
			remote_reply_raw(json);
			return;
		}

		pending.type = REMOTE_PENDING_SCREENSHOT;
		pending.frames_left = 1;
		SDL_strlcpy(pending.screenshot_path, path, sizeof(pending.screenshot_path));
		return;
	}

	if (strcmp(cmd, "quit") == 0)
	{
		SDL_Event quit;
		SDL_memset(&quit, 0, sizeof(quit));
		quit.type = SDL_QUIT;
		SDL_PushEvent(&quit);
		remote_reply_ok();
		return;
	}

	remote_reply_error("unknown cmd");
}

static void consume_rx(void)
{
	while (true)
	{
		char *newline = memchr(rx_buf, '\n', rx_len);
		if (newline == NULL)
			break;

		const size_t line_len = (size_t)(newline - rx_buf);
		char line[REMOTE_RX_BUF_SIZE];

		const size_t copy_len = MIN(line_len, sizeof(line) - 1);
		memcpy(line, rx_buf, copy_len);
		line[copy_len] = '\0';

		const size_t consume = line_len + 1;
		memmove(rx_buf, rx_buf + consume, rx_len - consume);
		rx_len -= consume;

		if (line[0] != '\0')
			handle_command(line);
	}
}

void remote_control_enable(const char *requested_socket_path)
{
	remote_enabled = true;

	if (requested_socket_path != NULL && requested_socket_path[0] != '\0')
		SDL_strlcpy(socket_path, requested_socket_path, sizeof(socket_path));
}

bool remote_control_is_enabled(void)
{
	return remote_enabled;
}

const char *remote_control_socket_path(void)
{
	return socket_path;
}

bool remote_control_init(void)
{
	if (!remote_enabled || remote_initialized)
		return true;

	if (socket_path[0] == '\0')
		SDL_strlcpy(socket_path, REMOTE_DEFAULT_SOCKET_PATH, sizeof(socket_path));

	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0)
	{
		fprintf(stderr, "remote: socket() failed: %s\n", strerror(errno));
		return false;
	}

	if (set_nonblocking(listen_fd) < 0)
	{
		fprintf(stderr, "remote: failed to set nonblocking: %s\n", strerror(errno));
		close(listen_fd);
		listen_fd = -1;
		return false;
	}

	unlink(socket_path);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	SDL_strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		fprintf(stderr, "remote: bind(%s) failed: %s\n", socket_path, strerror(errno));
		close(listen_fd);
		listen_fd = -1;
		return false;
	}

	if (listen(listen_fd, 1) < 0)
	{
		fprintf(stderr, "remote: listen() failed: %s\n", strerror(errno));
		close(listen_fd);
		listen_fd = -1;
		unlink(socket_path);
		return false;
	}

	remote_initialized = true;
	printf("remote control listening on %s\n", socket_path);

	return true;
}

void remote_control_shutdown(void)
{
	if (client_fd >= 0)
		close_client();

	if (listen_fd >= 0)
	{
		close(listen_fd);
		listen_fd = -1;
	}

	if (remote_initialized)
		unlink(socket_path);

	remote_initialized = false;
}

void remote_control_pump(void)
{
	if (!remote_initialized)
		return;

	if (client_fd < 0)
	{
		const Uint32 now = SDL_GetTicks();
		if ((Sint32)(now - next_accept_poll_ticks) < 0)
			return;

		next_accept_poll_ticks = now + 8;

		client_fd = accept(listen_fd, NULL, NULL);
		if (client_fd >= 0)
		{
			(void)set_nonblocking(client_fd);
#ifdef SO_NOSIGPIPE
			int one = 1;
			(void)setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
			rx_len = 0;
			next_accept_poll_ticks = now;
		}
	}

	if (client_fd < 0)
		return;

	char buf[512];
	for (;;)
	{
		const ssize_t got = recv(client_fd, buf, sizeof(buf), 0);
		if (got < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			close_client();
			break;
		}

		if (got == 0)
		{
			close_client();
			break;
		}

		const size_t copy_len = MIN((size_t)got, sizeof(rx_buf) - 1 - rx_len);
		memcpy(rx_buf + rx_len, buf, copy_len);
		rx_len += copy_len;
		rx_buf[rx_len] = '\0';

		consume_rx();

		if ((size_t)got > copy_len)
		{
			remote_reply_error("rx overflow");
			close_client();
			break;
		}
	}
}

void remote_control_on_frame(SDL_Surface *presented_surface)
{
	if (!remote_initialized)
		return;

	++frame_counter;
	last_presented_surface = presented_surface;

	if (pending.type == REMOTE_PENDING_NONE)
		return;

	if (pending.type == REMOTE_PENDING_WAIT_FRAMES)
	{
		if (--pending.frames_left <= 0)
		{
			char json[96];
			snprintf(json, sizeof(json), "{\"ok\":true,\"frame\":%" PRIu64 "}", frame_counter);
			remote_reply_raw(json);
			pending.type = REMOTE_PENDING_NONE;
		}
		return;
	}

	if (pending.type == REMOTE_PENDING_SCREENSHOT)
	{
		if (--pending.frames_left > 0)
			return;

		if (presented_surface == NULL)
		{
			remote_reply_error("no frame available");
		}
		else if (!video_save_surface_rgb(presented_surface, pending.screenshot_path))
		{
			remote_reply_error("screenshot failed");
		}
		else
		{
			char safe_path[PATH_MAX];
			SDL_strlcpy(safe_path, pending.screenshot_path, sizeof(safe_path));
			for (size_t i = 0; safe_path[i] != '\0'; ++i)
			{
				if (safe_path[i] == '"' || safe_path[i] == '\\')
					safe_path[i] = '_';
			}

			char json[PATH_MAX + 64];
			snprintf(json, sizeof(json), "{\"ok\":true,\"path\":\"%s\"}", safe_path);
			remote_reply_raw(json);
		}

		pending.type = REMOTE_PENDING_NONE;
		pending.screenshot_path[0] = '\0';
	}
}

void remote_control_set_ui_context(const char *context)
{
	if (context == NULL || context[0] == '\0')
	{
		SDL_strlcpy(ui_context, "unknown", sizeof(ui_context));
		return;
	}

	SDL_strlcpy(ui_context, context, sizeof(ui_context));
}

#else

void remote_control_enable(const char *requested_socket_path)
{
	(void)requested_socket_path;
}

bool remote_control_is_enabled(void)
{
	return false;
}

const char *remote_control_socket_path(void)
{
	return "";
}

bool remote_control_init(void)
{
	return true;
}

void remote_control_shutdown(void)
{
}

void remote_control_pump(void)
{
}

void remote_control_on_frame(SDL_Surface *presented_surface)
{
	(void)presented_surface;
}

void remote_control_set_ui_context(const char *context)
{
	(void)context;
}

#endif
