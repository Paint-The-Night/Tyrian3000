/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
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
#include "jukebox.h"

#include "font.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "loudness.h"
#include "mtrand.h"
#include "nortsong.h"
#include "opentyr.h"
#include "palette.h"
#include "sprite.h"
#include "starlib.h"
#include "vga256d.h"
#include "vga_palette.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>

static int wrap_song_index(int i)
{
	while (i < 0)
		i += MUSIC_NUM;
	while (i >= MUSIC_NUM)
		i -= MUSIC_NUM;
	return i;
}

void jukebox(void)  // FKA Setup.jukeboxGo
{
	bool trigger_quit = false,  // true when user wants to quit
	     quitting = false;
	
	bool hide_text = false;

	bool fade_looped_songs = true, fading_song = false;
	bool stopped = false;

	bool fx = false;
	int fx_num = 0;

	int palette_fade_steps = 15;

	int diff[256][3];
	init_step_fade_palette(diff, vga_palette, 0, 255);

	JE_starlib_init();

	int fade_volume = tyrMusicVolume;
	
	for (; ; )
	{
		if (!stopped && !audio_disabled)
		{
			if (songlooped && fade_looped_songs)
				fading_song = true;

			if (fading_song)
			{
				if (fade_volume > 5)
				{
					fade_volume -= 2;
				}
				else
				{
					fade_volume = tyrMusicVolume;

					fading_song = false;
				}

				set_volume(fade_volume, fxVolume);
			}

			if (!playing || (songlooped && fade_looped_songs && !fading_song))
				play_song(mt_rand() % MUSIC_NUM);
		}

		setDelay(1);

		SDL_FillRect(VGAScreenSeg, NULL, 0);

		// starlib input needs to be rewritten
		JE_starlib_main();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

			if (!hide_text)
			{
				const int list_x = 12;
				const int list_center_y = 96;
				const int list_line_h = 9;
				const int list_area_x1 = 8;
				const int list_area_x2 = 198;
				const int list_area_y1 = 14;
				const int list_area_y2 = 178;
				const int max_visible_rows = 17; // odd count so selected row can sit in the center
				const int visible_rows = MUSIC_NUM < max_visible_rows ? MUSIC_NUM : max_visible_rows;
				const int half_rows = visible_rows / 2;
				const int selected = song_playing;

				// Darken song-list region while preserving animated stars.
				JE_barShade(VGAScreen, list_area_x1, list_area_y1, list_area_x2, list_area_y2);
				JE_barShade(VGAScreen, list_area_x1, list_area_y1, list_area_x2, list_area_y2);

				for (int row = -half_rows; row <= half_rows; ++row)
				{
					const int song_index = wrap_song_index(selected + row);
					const int y = list_center_y + row * list_line_h;
					const int row_y1 = y - 1;
					const int row_y2 = y + 7;
					char entry[80];
					snprintf(entry, sizeof(entry), "%2d %s", song_index + 1, musicTitle[song_index]);

					const int distance = abs(row);
					const bool current = row == 0;
					const Uint8 hue = current ? 15 : 1;
					Sint8 value;
					if (current)
					{
						value = 4;
					}
					else
					{
						int faded_value = -1 - distance;
						if (faded_value < -6)
							faded_value = -6;
						value = (Sint8)faded_value;
					}

					if (current)
					{
						JE_barShade(VGAScreen, list_area_x1, row_y1, list_area_x2, row_y2);
						draw_font_hv(VGAScreen, list_x, y, entry, small_font, left_aligned, 1, 4);
					}
					else
					{
						draw_font_hv(VGAScreen, list_x, y, entry, small_font, left_aligned, hue, value);
					}
				}

				draw_font_hv(VGAScreen, 2, list_center_y, ">", small_font, left_aligned, 15, 4);
				draw_font_hv(VGAScreen, 198, 156, "SPACE: Hide Text", small_font, left_aligned, 1, 0);
				draw_font_hv(VGAScreen, 198, 166, "ESC: Quit", small_font, left_aligned, 1, 0);
				draw_font_hv(VGAScreen, 198, 176, "Up/Down: Select", small_font, left_aligned, 1, 0);
				draw_font_hv(VGAScreen, 198, 186, "S: Stop  R: Restart", small_font, left_aligned, 1, 0);

				if (fx)
				{
					char fx_buffer[60];
					snprintf(fx_buffer, sizeof(fx_buffer), "FX %d %s", fx_num + 1, soundTitle[fx_num]);
					draw_font_hv(VGAScreen, 198, 12, fx_buffer, small_font, left_aligned, 13, 2);
				}
			}

		if (palette_fade_steps > 0)
			step_fade_palette(diff, palette_fade_steps--, 0, 255);
		
		JE_showVGA();

		wait_delay();

		// quit on mouse click
		Uint16 x, y;
		if (JE_mousePosition(&x, &y) > 0)
			trigger_quit = true;

		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_ESCAPE: // quit jukebox
			case SDL_SCANCODE_Q:
				trigger_quit = true;
				break;

			case SDL_SCANCODE_SPACE:
				hide_text = !hide_text;
				break;

			case SDL_SCANCODE_F:
				fading_song = !fading_song;
				break;
			case SDL_SCANCODE_N:
				fade_looped_songs = !fade_looped_songs;
				break;

			case SDL_SCANCODE_SLASH: // switch to sfx mode
				fx = !fx;
				break;
			case SDL_SCANCODE_COMMA:
				if (fx && --fx_num < 0)
					fx_num = SOUND_COUNT - 1;
				break;
			case SDL_SCANCODE_PERIOD:
				if (fx && ++fx_num >= SOUND_COUNT)
					fx_num = 0;
				break;
			case SDL_SCANCODE_SEMICOLON:
				if (fx)
					JE_playSampleNum(fx_num + 1);
				break;

			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_UP:
				play_song((song_playing > 0 ? song_playing : MUSIC_NUM) - 1);
				stopped = false;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_RIGHT:
			case SDL_SCANCODE_DOWN:
				play_song((song_playing + 1) % MUSIC_NUM);
				stopped = false;
				break;
			case SDL_SCANCODE_S: // stop song
				stop_song();
				stopped = true;
				break;
			case SDL_SCANCODE_R: // restart song
				restart_song();
				stopped = false;
				break;

			default:
				break;
			}
		}
		
		// user wants to quit, start fade-out
		if (trigger_quit && !quitting)
		{
			palette_fade_steps = 15;
			
			SDL_Color black = { 0, 0, 0 };
			init_step_fade_solid(diff, black, 0, 255);
			
			quitting = true;
		}
		
		// if fade-out finished, we can finally quit
		if (quitting && palette_fade_steps == 0)
			break;
	}

	set_volume(tyrMusicVolume, fxVolume);
}
