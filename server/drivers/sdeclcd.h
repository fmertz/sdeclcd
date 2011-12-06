/**
 *  This is the LCDproc driver for SDEC LCD Devices, 
 *  like the one found in the FireBox firewall device.
 *
 *  Copyright(C) 2008 Jayson Kubilis (jekubili AT ktechs dot net)
 *
 *    based on GPL'ed code:
 *
 *       + files from LCDproc source tree
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 *
 *  Thanks for playing!
 */

#ifndef SDECLCD_H
#define SDECLCD_H

#include "lcd.h"

MODULE_EXPORT int sdeclcd_init (Driver *drvthis);
MODULE_EXPORT void sdeclcd_close (Driver *drvthis);
MODULE_EXPORT int sdeclcd_width (Driver *drvthis);
MODULE_EXPORT int sdeclcd_height (Driver *drvthis);
MODULE_EXPORT void sdeclcd_flush (Driver *drvthis);
MODULE_EXPORT void sdeclcd_string (Driver *drvthis, int x, int y, char string[]);
MODULE_EXPORT void sdeclcd_chr (Driver *drvthis, int x, int y, char c);
MODULE_EXPORT void sdeclcd_clear (Driver *drvthis);
MODULE_EXPORT int sdeclcd_cellwidth (Driver *drvthis);
MODULE_EXPORT int sdeclcd_cellheight (Driver *drvthis);
MODULE_EXPORT void sdeclcd_set_char (Driver *drvthis, int n, unsigned char *dat);
MODULE_EXPORT int sdeclcd_icon (Driver *drvthis, int x, int y, int icon);
MODULE_EXPORT void sdeclcd_heartbeat( Driver * drvthis, int type );
MODULE_EXPORT void sdeclcd_vbar (Driver *drvthis, int x, int y, int len, int promille, int options);
MODULE_EXPORT void sdeclcd_hbar (Driver *drvthis, int x, int y, int len, int promille, int options);
MODULE_EXPORT void sdeclcd_num (Driver *drvthis, int x, int num);

//
//MODULE_EXPORT void sdeclcd_old_vbar (Driver *drvthis, int x, int len);
//MODULE_EXPORT void sdeclcd_old_hbar (Driver *drvthis, int x, int y, int len);
//MODULE_EXPORT void sdeclcd_old_icon (Driver *drvthis, int which, char dest);

// added keypad 
MODULE_EXPORT const char *sdeclcd_get_key(Driver *drvthis);
#endif
