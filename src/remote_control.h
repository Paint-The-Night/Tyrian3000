/*
 * Tyrian 3000: Remote Control
 * Copyright (C) 2026  Gary Perrigo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#ifndef REMOTE_CONTROL_H
#define REMOTE_CONTROL_H

#include "opentyr.h"

#include "SDL.h"

#include <stdbool.h>

/* Configure the optional remote-control server before startup completes. */
void remote_control_enable(const char *socket_path);
bool remote_control_is_enabled(void);
const char *remote_control_socket_path(void);

/* Runtime lifecycle hooks. */
bool remote_control_init(void);
void remote_control_shutdown(void);
void remote_control_pump(void);
void remote_control_on_frame(SDL_Surface *presented_surface);

/* Optional context string shown in get_state responses. */
void remote_control_set_ui_context(const char *context);

#endif /* REMOTE_CONTROL_H */
