/** \file firmware/freemcan-lcd.h
 * \brief Freemcan common LCD interface
 * \author Copyright (C) 2014 Samplemaker
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 *
 * \defgroup firmware_lcd
 * \ingroup firmware
 *
 * 
 */


#ifndef FREEMCAN_LCD_H
#define FREEMCAN_LCD_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <avr/pgmspace.h>

typedef enum {
  CHAR_1,
  CHAR_2,
  CHAR_3,
  CHAR_4,
  CHAR_UP,
  CHAR_DOWN,
  NUM_CHAR
} st_indicator_t;


static const char PROGMEM font[NUM_CHAR][8]={
{0x0,0x10,0x8,0x4,0x2,0x1,0x0,0x0}, //'\'
{0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x0},  //'|'
{0x0,0x1,0x2,0x4,0x8,0x10,0x0,0x0}, //'/'
{0x0,0x0,0x0,0x1f,0x0,0x0,0x0,0x0}, //'-'
{0x4,0xe,0x1f,0x4,0x4,0x4,0x4,0x0}, // arrow up
{0x4,0x4,0x4,0x4,0x1f,0xe,0x4,0x0}  //arrow down
};


char get_indicator (void);
void set_font (void);
void uint_to_ascii(char *out_str, uint32_t value,
                   const uint8_t pos, const uint8_t print);


#endif /* !FREEMCAN_LCD_H */

