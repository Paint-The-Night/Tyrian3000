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
#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include "opentyr.h"

#include "SDL.h"

#include <stdbool.h>
#include <stddef.h>

bool debug_console_is_active(void);
void debug_console_toggle(void);
void debug_console_handle_keydown(SDL_Scancode scan, SDL_Keymod mod);
void debug_console_handle_textinput(const char *text);
void debug_console_draw(SDL_Surface *screen);

/* Auto-exec: set a command to run on startup (used by --console-exec). */
void debug_console_set_startup_command(const char *cmd);

/* Screenshot flag: returns true once after a screenshot was requested. */
bool debug_console_wants_screenshot(void);

/* Programmatic command execution (for remote control/testing). */
void debug_console_execute_command(const char *cmd);
const char *debug_console_get_last_line(void);

#endif /* DEBUG_CONSOLE_H */
