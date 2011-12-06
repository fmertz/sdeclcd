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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <sys/errno.h>
//#include <sys/io.h>
#include <time.h>
#include "port.h"
#include "timing.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "shared/str.h"
#include "lcd.h"
#include "sdeclcd.h"
#include "report.h"
#include "adv_bignum.h"


#ifndef LPT_DEFAULT
#define LPT_DEFAULT 0x378
#endif

#ifndef NUM_CCs
#define NUM_CCs 8
#endif

/**
 * Define the various modes we support for in-memory character storage
 */
typedef enum
{
  CCMODE_STANDARD,    /* only char 0 is used for heartbeat */
  CCMODE_VBAR,        /* We dont use vbars as of 2-16-08 */
  CCMODE_HBAR,        
  CCMODE_BIGNUM       /* We don't use hbars as of 2-16-08 */
} CCMode;

typedef struct driver_private_data {
        CCMode ccmode;           /* custom character mode for current display */
        CCMode last_ccmode;      /* custom character set that is loaded in the display */
	unsigned int port;       /* port that we talk with the device */
	unsigned int bklgt;      /* backlight is on or off */
	unsigned int charattrib; 
	unsigned int flags;      /* not used */
	unsigned int cellwidth;  /* Cell Width */
	unsigned int cellheight; /* Cell Height */
	unsigned int dispwidth;  /* Display Width */
	unsigned int dispheight; /* Display Height */
        unsigned int lastcharloc; /*Last memory location we wrote to the LCD. */
	char *framebuf;           /* Frame buffer as written to the LCD */
	char *framenew;           /* Frame buffer to be written to the LCD */
        char *frameicon;          /* Frame buffer for custom icons stored in CGMemory */
} PrivateData;

MODULE_EXPORT char *api_version = API_VERSION;
MODULE_EXPORT int stay_in_foreground = 0;
MODULE_EXPORT int supports_multiple = 0;
MODULE_EXPORT char *symbol_prefix = "sdeclcd_";

// Notes: 
//    -- 0001 Strobe bit, Control bit 0 = LCD Backlight (0: on 1: off)
//    -- 0010 Autofeed  , Control bit 1 = Enable Signal - LCD Do something with whats in mem
//    -- 0100 Unknown, maybe write?
//    -- 1000 Select In , Control bit 3 = Register Select - SELECT Function or data in memory

/**
 * write data to the LCD. This is bits 8 - 0 on the data port.
 */
void
sdeclcd_writedata (PrivateData *p, char data) {
    
    //PrivateData *p = (PrivateData *) drvthis->private_data;

    // We don't care about high bits; so we ignore those with an 0xF0 &
    // The LCD executes commands using the second bit on the control port, not strobe bit like most.
    // this is likely due to the specific cabling of the Firebox LCD.
    // Backlight is controlled by the strobe control bit. See notes above.

    port_out(p->port+2, ((port_in(p->port+2) & 0xF0) | (0x00 + (1 ^ p->bklgt))));
    port_out(p->port, data);
    port_out(p->port+2, ((port_in(p->port+2) & 0xF0) | (0x02 + (1 ^ p->bklgt))));
    timing_uPause(40);    
}

/**
 * Issue a command to the LCD.  Difference from above is we change the Select In/Register select bit on the LCD display
 */
void
sdeclcd_writecmd (PrivateData *p, char cmd) {
	
    //PrivateData *p = (PrivateData *) drvthis->private_data;

    port_out(p->port+2, ((port_in(p->port+2) & 0xF0) | (0x08 + (1 ^ p->bklgt)))); //c8      
    port_out(p->port, cmd);
    port_out(p->port+2, ((port_in(p->port+2) & 0xF0) | (0x0a + (1 ^ p->bklgt)))); //ca
    timing_uPause(40);	
}

/**
 * Write a custom icon to the LCDs CGRam
 */
void
sdeclcd_writecgram(PrivateData *p, int n, char data[]) {

    //report(RPT_DEBUG, "%s: writing GC_RAM data", "sdeclcd");
    
    // 0x40 - this is the command to set a char in CGRAM
    // Specific locations for each CG Storage area is 8 bits apart.

    int memaddr = 0x40+(n*8);
    int pxlline = 0;
    int i = 0;

    if (n >= NUM_CCs)
        return;
    if (!data)
        return;

    for (i=0; i<p->cellheight; i++) {
        sdeclcd_writecmd(p, memaddr+i);
        sdeclcd_writedata(p, data[i]);
    }
}

/**
 * Write something to the LCD. Uses the above functions.
 * Performance is improved below using the theroy that if we write something to the LCD
 * chances are that we will write to the next cell on the right. thus the mode we put the lcd
 * in is to auto-move curor to the right. We check to see if the next address is one to the right,
 * if it is we skip the step to issue a curor move command. Almost 2x performance :)
 */
void
sdeclcd_writeout(PrivateData *p, int x, int y, char data) {

    int lcdmemreg = 0x80+(0x40*y)+x;

    if (p->lastcharloc != lcdmemreg-1)
       sdeclcd_writecmd(p, lcdmemreg);
    p->lastcharloc = lcdmemreg;
    sdeclcd_writedata(p, data);

}

/**
 * This is all of the commands to init/reset the display. This is only run once the driver is loaded.
 */
void
sdeclcd_displayinit(PrivateData *p) {

    char init[9];
    init[0] = 0x30; //Function set
    init[1] = 0x30; //Function set
    init[2] = 0x30; //Function set
    init[3] = 0x38; // Function set. We setup the display length and bit mode we use (8 or 4) 0011|1000
    init[4] = 0x08; // Display on 0000|1000
    init[5] = 0x01; // Clear LCD
    init[6] = 0x06; // set entry mode here. 0000|0110
    init[7] = 0x0c; // Display on and change Cursor modes 0000|1100
    init[8] = 0x14; // 0001|0100

    int c;
    for (c=0;c<9;c++) {
      sdeclcd_writecmd(p, init[c]);
      timing_uPause(460); //we need to slow down for init just a bit.
    }

}

/**
 * Initialize var's alloc memory, setup pointers and init display
 */
MODULE_EXPORT int 
sdeclcd_init (Driver *drvthis)
{
    PrivateData *p;
    int i;

    /* Allocate and store private data */
    p = (PrivateData *) calloc(1, sizeof(PrivateData));
    if (p == NULL)
   	  return -1;
    if (drvthis->store_private_ptr(drvthis, p))
          return -1;

    /* Initialize private data and read cofig */
    p->ccmode = p->last_ccmode = CCMODE_STANDARD;
    p->port = drvthis->config_get_int(drvthis->name, "Port", 0, LPT_DEFAULT);
    p->bklgt = drvthis->config_get_bool(drvthis->name, "BackLight", 0, 0);
    p->flags = 0;
    p->cellwidth = 5;
    p->cellheight = 7;
    p->dispwidth = 20;
    p->dispheight = 2;
    p->lastcharloc = 0x80;
    p->framebuf = (char *) calloc(p->dispwidth * p->dispheight, sizeof(char)); // Current LCD display
    p->framenew = (char *) calloc(p->dispwidth * p->dispheight, sizeof(char)); // Updates prior to flush to LCD display (new data)
    p->frameicon = (char *) calloc(NUM_CCs*p->cellheight, sizeof(char));
    memset(p->framebuf, ' ', p->dispwidth * p->dispheight);
    memset(p->framenew, ' ', p->dispwidth * p->dispheight);
    memset(p->frameicon, ' ', NUM_CCs);
	  
    if (p->framebuf == NULL || p->framenew == NULL) {
      report(RPT_ERR, "%s: unable to allocate framebuffer", drvthis->name);
      return -1;
    }

    // Initialize the Port
    if (port_access_multiple(p->port,3)) {
      report(RPT_ERR, "%s: cannot get IO-permission for 0x%03X! Are we running as root?", drvthis->name, p->port);
      return -1;
    }

    sdeclcd_displayinit(p);

    return 0;
    
}

/**
 * Free memory and go home for the day.
 */
MODULE_EXPORT void
sdeclcd_close(Driver *drvthis)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;

    if (p != NULL) {
        if (p->framebuf)
            free(p->framebuf);

        if (p->framenew)
            free(p->framenew);

        if (p->frameicon)
            free(p->frameicon);

        free(p);
    }
	
    if (!port_deny_multiple(p->port,3)) {
        report(RPT_ERR, "%s: cannot get IO-permission for 0x%03X! Are we running as root?", drvthis->name, p->port);
    }
  	
    drvthis->store_private_ptr(drvthis, NULL);

}





/**
 * Returns the display width
 */
MODULE_EXPORT int
sdeclcd_width (Driver *drvthis) {
	
	PrivateData *p = (PrivateData *) drvthis->private_data;
	
	return p->dispwidth;

}

/**
 * Returns the display height
 */
MODULE_EXPORT int
sdeclcd_height (Driver *drvthis) {
	
	PrivateData *p = (PrivateData *) drvthis->private_data;
	
	return p->dispheight;
	
}

/**
 * Flush the contents of the framebuffer out to the lcd and set the lcd's 'current' framebuffer equal
 * to that of the flushed FB.
 */
MODULE_EXPORT void
sdeclcd_flush (Driver *drvthis) {
    PrivateData *p = (PrivateData *) drvthis->private_data;
    int c,l;
    c = 0;
    l = (p->dispwidth * p->dispheight);

    for(c=0; c<l; c++) {
      if(p->framebuf[c] != p->framenew[c]) {
        //Need to update this location onto lcd.
        // Calculate x y cord's
        p->framebuf[c] = p->framenew[c];
        sdeclcd_writeout(p, (c%(p->dispwidth)), (c/(p->dispwidth)), p->framenew[c]);
      }
    }
}

/**
 * Write an string to the LCD
 */
MODULE_EXPORT void
sdeclcd_string (Driver *drvthis, int x, int y, char string[]) {
  
  PrivateData *p = (PrivateData *) drvthis->private_data;
	int i;

	x--;  // Convert 1-based coords to 0-based
	y--;

	if ((y < 0) || (y >= p->dispheight))
		return;

	for (i = 0; (string[i] != '\0') && (x < p->dispwidth); i++, x++) {
		if (x >= 0)	// no write left of left border
			p->framenew[(y * p->dispwidth) + x] = string[i];
	}
        
}


/**
 * Write a character to the LCD at x,y
 */
MODULE_EXPORT void
sdeclcd_chr (Driver *drvthis, int x, int y, char c) {
  
  PrivateData *p = (PrivateData *) drvthis->private_data;
	
	y--;
	x--;
        report(RPT_DEBUG, "%s: Writing char %c at %i %i",drvthis->name,c,x,y);

	if ((x >= 0) && (y >= 0) && (x < p->dispwidth) && (y < p->dispheight))
		p->framenew[(y * p->dispwidth) + x] = c;

}

/**
 * Clear the framebuffer, clears the display on next write.
 * We could use the hardware function which will provide better performance.
 */
MODULE_EXPORT void
sdeclcd_clear (Driver *drvthis) {

  PrivateData *p = (PrivateData *) drvthis->private_data;
	
  memset(p->framenew, ' ', p->dispwidth * p->dispheight);
	
}

/**
 * width of a single cell in the LCD
 */
MODULE_EXPORT int
sdeclcd_cellwidth (Driver *drvthis)
{
    PrivateData *p = drvthis->private_data;
    return p->cellwidth;
}

/**
 * height of a single cell in the LCD
 */
MODULE_EXPORT int
sdeclcd_cellheight (Driver *drvthis)
{
    PrivateData *p = drvthis->private_data;
    return p->cellheight;
}

/**
 * Sets a custom char via pixel on/off bitmaps.
 */
MODULE_EXPORT void
sdeclcd_set_char (Driver *drvthis, int n, unsigned char *dat)
{
	PrivateData *p = drvthis->private_data;
	unsigned char out[p->cellheight];
	unsigned char mask = (1 << p->cellwidth) - 1;
	int row;
        int cachestale = 0;

	if ((n < 0) || (n >= NUM_CCs))
		return;
	if (!dat)
		return;

	for (row = 0; row < p->cellheight; row++) {
		out[row] = dat[row] & mask;
                if (p->frameicon[(n*p->cellheight)+row] != (dat[row] & mask)) {
                    cachestale = 1;
                    p->frameicon[(n*p->cellheight)+row] = dat[row] & mask;
                }
	}
        
        if (cachestale == 1) {
            sdeclcd_writecgram(p, n, out);
            report(RPT_DEBUG, "%s: Icon Cache was stale", drvthis->name);
        }
        else
            report(RPT_DEBUG, "%s: Used Icon Cache", drvthis->name);
}

/**
 * Write a special icon to the LCD's CGRam
 */
MODULE_EXPORT int
sdeclcd_icon (Driver *drvthis, int x, int y, int icon)
{
    //PrivateData *p = drvthis->private_data;
    report(RPT_DEBUG, "%s: icon -- we ran :]", drvthis->name);
    static unsigned char heart_open[] = 
		{ b__XXXXX,
		  b__X_X_X,
                  b_______,
		  b_______,
		  b__X___X,
		  b__XX_XX,
		  b__XXXXX };
	static unsigned char heart_filled[] = 
		{ b__XXXXX,
		  b__X_X_X,
		  b___X_X_,
		  b___XXX_,
		  b__X_X_X,
		  b__XX_XX,
		  b__XXXXX };
	static unsigned char arrow_up[] = 
		{ b____X__,
		  b___XXX_,
		  b__X_X_X,
		  b____X__,
                  b____X__,
		  b____X__,
		  b_______ };
	static unsigned char arrow_down[] = 
		{ b____X__,
		  b____X__,
		  b____X__,
		  b__X_X_X,
		  b___XXX_,
		  b____X__,
		  b_______ };

    switch (icon) {
	case ICON_BLOCK_FILLED:
	    sdeclcd_chr(drvthis, x, y, 255);
	    break;
	case ICON_HEART_FILLED:
	    sdeclcd_set_char(drvthis, 0, heart_filled);
	    sdeclcd_chr(drvthis, x, y, 0);
	    break;
	case ICON_HEART_OPEN:
	    sdeclcd_set_char(drvthis, 1, heart_open);
	    sdeclcd_chr(drvthis, x, y, 1);
	    break;
	default:
	    return -1;
    }
    return 0;
}

/**
 * Draw our heart beat. I use 2 ram locations because i dont want to always have to write to the ram
 * on the LCD. Note heart_filled is memory 0 loc and heart_open is in memory 1 loc. (see above function)
 */
MODULE_EXPORT void
sdeclcd_heartbeat( Driver * drvthis, int type ) {
    
    //report(RPT_DEBUG, "%s: heartbeat -- using CG RAM icons :]", drvthis->name);
    PrivateData *p = drvthis->private_data;

    if (type <= 0 || type > 2)
        return;
    
    type--;
    p->framenew[p->dispwidth-1] = type;

}


/**
 * This is NOT implemented yet! Used from the curses driver.
 */
MODULE_EXPORT void
sdeclcd_vbar (Driver *drvthis, int x, int y, int len, int promille, int options)
{
	PrivateData *p = drvthis->private_data;
	char ascii_map[] = { ' ', ' ', '-', '-', '=', '=', '#', '#' };
	char *map = ascii_map;
	int pixels = ((long) 2 * len * p->cellheight) * promille / 2000;
	int pos;


	if ((x <= 0) || (y <= 0) || (x > p->dispwidth))
		return;

	// x and y are the start position of the bar.
	// The bar by default grows in the 'up' direction
	// (other direction not yet implemented).
	// len is the number of characters that the bar is long at 100%
	// promille is the number of promilles (0..1000) that the bar should be filled.
	//

	for (pos = 0; pos < len; pos++) {

		if (y - pos <= 0)
			return;

		if (pixels >= p->cellheight) {
			// write a "full" block to the screen... 
			sdeclcd_chr(drvthis, x, y-pos, '#');
		}
		else if (pixels > 0) {
			// write a partial block...
			sdeclcd_chr(drvthis, x, y-pos, map[len-1]);
			break;
		}
		else {
			; // write nothing (not even a space)
		}

		pixels -= p->cellheight;
	}
}



/**
 * Gets ready to draw the horizontal bars. The concept of this came from lcterm.c
 */
static void
sdeclcd_init_hbar (Driver *drvthis)
{
  PrivateData *p = (PrivateData *) drvthis->private_data;
  static unsigned char hbar_1[] = 
    { b_______,
      b__X____,
      b__X____,
      b__X____,
      b__X____,
      b__X____,
      b_______ };
  
  static char hbar_2[] = 
    { b_______,
      b__XX___,
      b__XX___,
      b__XX___,
      b__XX___,
      b__XX___,
      b_______ };
  
  static char hbar_3[] =
    { b_______,
      b__XXX__,
      b__XXX__,
      b__XXX__,
      b__XXX__,
      b__XXX__,
      b_______ };

  static char hbar_4[] =
    { b_______,
      b__XXXX_,
      b__XXXX_,
      b__XXXX_,
      b__XXXX_,
      b__XXXX_,
      b_______ };

  static char hbar_5[] =
    { b_______,
      b__XXXXX,
      b__XXXXX,
      b__XXXXX,
      b__XXXXX,
      b__XXXXX,
      b_______ };

  if (p->last_ccmode == CCMODE_HBAR) /* Work already done */
    return;

  if (p->ccmode != CCMODE_STANDARD) {
    /* Not supported (yet) */
    report(RPT_WARNING, "%s: init_hbar: cannot combine two modes using user-defined characters",
		    drvthis->name);
    return;
  }

  p->ccmode = p->last_ccmode = CCMODE_HBAR;
  
  // I offset my memory locations because mem loc 0 and 1 are used for the heart beat icon.
  sdeclcd_set_char(drvthis, 2, hbar_1);
  sdeclcd_set_char(drvthis, 3, hbar_2);
  sdeclcd_set_char(drvthis, 4, hbar_3);
  sdeclcd_set_char(drvthis, 5, hbar_4);
  sdeclcd_set_char(drvthis, 6, hbar_5);

}

/*
/////////////////////////////////////////////////////////////////
// Draws a horizontal bar to the right.  taken from curses
//
MODULE_EXPORT void
sdeclcd_hbar (Driver *drvthis, int x, int y, int len, int promille, int options)
{
	PrivateData *p = drvthis->private_data;

	int pixels = (((long) len * p->cellwidth) * promille) / 1000;
	int pos;

	if ((x <= 0) || (y <= 0) || (y > p->dispheight))
		return;

	// x and y are the start position of the bar.
	// The bar by default grows in the 'right' direction
	// (other direction not yet implemented).
	// len is the number of characters that the bar is long at 100%
	// promille is the number of promilles (0..1000) that the bar should be filled.
	
        report(RPT_DEBUG, "%s: Parms for hbar: x:%i y:%i len:%i pro:%i",drvthis->name,x,y,len,promille);

	for (pos = 0; pos < len; pos++) {
                report(RPT_DEBUG, "%s: hbar pixels: %i",drvthis->name, pixels);
		if (x + pos > p->dispwidth)
			return;
                        //sdeclcd_chr(drvthis, x, y,'@');
                        //sdeclcd_chr(drvthis, 19, y,'@');

		if (pixels >= p->cellwidth * 2/3) {
			// write a "full" block to the screen... 
                        report(RPT_DEBUG, "%s: hbar found 2/3 x:%i y:%i", drvthis->name,x+pos, y);
			sdeclcd_chr(drvthis, x+pos, y, '=');
                        
                        
		}
		else if (pixels > p->cellwidth * 1/3) {
			// write a partial block... 
                        report(RPT_DEBUG, "%s: hbar found 1/3 x:%i y:%i", drvthis->name,x+pos, y);
			sdeclcd_chr(drvthis, x+pos, y, '-');
			break;
		}
		else {
			; // write nothing (not even a space)
		}

		pixels -= p->cellwidth;
                if (pixels <= 0)
                    break;
	}
}*/


/**
 * Draws a horizontal bar from left to right. Taken from lcterm.c
 */
MODULE_EXPORT void
sdeclcd_hbar (Driver *drvthis, int x, int y, int len, int promille, int options)
{
  PrivateData *p = drvthis->private_data;
  sdeclcd_init_hbar(drvthis);
  lib_hbar_static(drvthis, x, y, len, promille, options, p->cellwidth, 1);
}

