/** \file firmware/freemcan-lcd.c
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
 * \defgroup lcd
 * \ingroup firmware
 *
 * 
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "freemcan-lcd.h"
#include "lcd.h"


char
get_indicator (void)
{
  static st_indicator_t st_indicator = CHAR_1;

  switch (st_indicator) {
    case CHAR_1:
        st_indicator = CHAR_2;
        return(CHAR_1);
    break;
    case CHAR_2:
        st_indicator = CHAR_3;
        return(CHAR_2);
    break;
    case CHAR_3:
        st_indicator = CHAR_4;
        return(CHAR_3);
    break;
    case CHAR_4:
        st_indicator = CHAR_1;
        return(CHAR_4);
    break;
    default:
        return('0');
    break;
  }
}

void
set_font (void)
{
  for (uint8_t num=0; num<NUM_CHAR; num++){
     //set LCD address to Character Generator RAM
     lcd_command(0x40 + num*8);
     //write char pattern
     for (uint8_t i=0; i<8; i++) {
        /* read from ram */
        //lcd_data(font[num][i]);
        /* from flash */
        lcd_data(pgm_read_byte(&font[num][i]));
     }
  }
  /* reset the LCD */
  lcd_gotoxy(0,0);
}



/* keep this in ram */
static const uint32_t base_10[] = {100000, 10000, 1000, 100, 10};

/** Integer to ascii conversion (decimal system)
 *
 * pos ~ position of the decimal point
 * 0: no decimal point
 * 1: one digit behind the decimal point
 * ...
 * print ~ number of digits to be print
 * 1: print one digit (~ '.9' or '9')
 * 2: print two digits (~ '0.9' or ' 9' or '99')
 * 3: print three digits (~ ' 0.9' or '  9' or ' 99')
 * ... (maximum = 6)
 */
void 
uint_to_ascii(char *out_str, uint32_t value,
              const uint8_t pos, const uint8_t print){
  uint8_t num;
  uint8_t tmp = 0;
  /* number of elements in base_10[] */
  const uint8_t digits = 5;

  for (uint8_t i = (digits+1-print) ; i < digits; i++){
     num = 0;
     while (value >= base_10[i]){
       value -= base_10[i];
       num++;
     }
     tmp += num;
     if ((i == (digits - pos + 1)) && (pos > 1)) *out_str++ = '.';
     if (tmp > 0){
       *out_str++ = num | 0x30;
     }
     else{
       if (i < (digits - pos)){
         *out_str++ = ' ';
       }
       else{
         *out_str++ = '0';
       }
     }
  }
  if ((pos == 1)) *out_str++ = '.';
  *out_str++ = (char)(value) | 0x30;
  *out_str++ = '\0';
}


