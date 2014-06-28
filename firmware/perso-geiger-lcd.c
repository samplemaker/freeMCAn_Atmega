/** \file firmware/perso-geiger-lcd.c
 * \brief Freemcan statistical package for LCD display
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
#include "perso-geiger-lcd.h"
#include "lcd.h"


typedef enum {
  STATE_LOW_RATE,
  STATE_HIGH_RATE
} state_range_t;

static state_range_t state_range = STATE_LOW_RATE;

/* expected averaging time for the various ranges to get an acceptable accuracy */
typedef enum {
  ELEMENTS_LOW_RATE = NUM_BUF_MAX,
  ELEMENTS_HIGH_RATE = 30,
} range_time_t;

/* threshold rate [PHYS_CPM] x RES_COUNT_RATE for switching between the ranges (autoranging)
 * ranges including hysteresis must not overlap */
typedef enum {
  RATE_LOWHIGH = (RES_COUNT_RATE*1050UL), //transition from range "low" to range "high"
  RATE_HIGHLOW = (RES_COUNT_RATE*700UL), //transition from range "high" to range "low"
} range_rate_t;


/* autoranging controls the record length and gives higher
 * dynamic response at higher count rates
 */
uint16_t
avrg_len(uint32_t count_rate_estimator){
   uint16_t num_elements = 0;

   switch (state_range) {
      case (STATE_LOW_RATE) :
         if (count_rate_estimator > RATE_LOWHIGH){
           state_range = STATE_HIGH_RATE;
           num_elements = ELEMENTS_HIGH_RATE;
           lcd_gotoxy(12,1);
           lcd_putc(CHAR_UP);
         }else{
           num_elements = ELEMENTS_LOW_RATE;
         };
      break;
      case (STATE_HIGH_RATE) :
         if (count_rate_estimator < RATE_HIGHLOW){
           state_range = STATE_LOW_RATE;
           num_elements = ELEMENTS_LOW_RATE;
           lcd_gotoxy(12,1);
           lcd_putc(CHAR_DOWN);
         }else{
           num_elements = ELEMENTS_HIGH_RATE;
         };
      break;
      default:
      break;
   }
   return(num_elements);
}

uint16_t
c_sqrt32 (uint32_t q)
{
  uint16_t r, mask;
  uint16_t i = 8*sizeof (r) -1;
  r = mask = 1 << i;
  for (; i > 0; i--){
     mask >>= 1;
     if (q < (uint32_t)(r)*(uint32_t)(r))
       r -= mask;
     else
       r += mask;
  }
  if (q < (uint32_t)(r)*(uint32_t)(r))
    r -= 1;
  return r;
}


uint32_t
get_stats(statistics_t * statistics,
          ringbuf_t * elements,
          const uint16_t num_proceed){

   /* calculate the sum over various buffer lengths of the recorded data */
   uint16_t num_proceed2 = (num_proceed >> 1);
   uint16_t i=0;
   uint16_t current_pos = elements->head_cpy;
   statistics->sum_total = 0;
   while (i < num_proceed) {
      statistics->sum_total += elements->rbuf[current_pos];
      if (current_pos){
        current_pos--;
      }
      else{
        current_pos = elements->num_max - 1;
      }
      i++;
      if (i == num_proceed2) statistics->sum_new2 = statistics->sum_total;
      if (i == 2*num_proceed2) statistics->sum_old2 = statistics->sum_total;
      if (i == statistics->num_short) statistics->sum_short = statistics->sum_total;
   }
   statistics->sum_old2 = statistics->sum_old2 - statistics->sum_new2;

   /* average CPM over the whole record */
   statistics->count_rate_estimator_lg = (RES_COUNT_RATE*60UL*statistics->sum_total)/
                                         (num_proceed);

   /* the absolute statistical error [%]: reltol = 100/sqrt(N) */
   statistics->reltol_lg = (100UL*60UL)/c_sqrt32(3600UL*(uint32_t)(statistics->sum_total));

   /* the local short time count rate over the newest
    * NUM_TIME_UNITS_ST samples */
   statistics->count_rate_estimator_st = (RES_COUNT_RATE*60UL*statistics->sum_short)/
                                         (statistics->num_short);
   return(statistics->count_rate_estimator_lg);
}


/* check wheather the short time measurement (i.e.
 * the last and newest NUM_TIME_UNITS_ST samples) deviate more than
 * certain amount from the long time count rate.
 * to keep it simple we simply calculate the expected
 * (standard deviation)^2 of the shot time measurement from the
 * hole record. hence we do not bother with any statistical
 * tests. for long record queues the test is good, for very small
 * short and long time measurement are identical or almost similar
 * for the transition region the error of the estimator must be also
 * taken into consideration
 */
uint8_t
test1( statistics_t * statistics ){

   const uint32_t thresh_test1 = statistics->count_rate_estimator_lg*
                                 RES_COUNT_RATE*60UL/
                                 statistics->num_short;

   const uint32_t diff_test1 = ((int32_t)(statistics->count_rate_estimator_lg)-
                                (int32_t)(statistics->count_rate_estimator_st))*
                                ((int32_t)(statistics->count_rate_estimator_lg)-
                                (int32_t)(statistics->count_rate_estimator_st));

/* promt short time excursion more than 4.47 stdevs (=sqrt(20UL)) */
   return (diff_test1 > (20UL*thresh_test1));
}


/* test to detect long term drifts in the count rate.
 * there are always two measurements available: the upper/second and the 
 * lower/first half of the recorded data. hence we have two measurements
 * with equal measurement time (num_proceed2) which can be
 * compared to each other.  since both records have same length (t1=t2) we
 * can just cancel out the time. hence
 * k*sqrt(N1+N2)>abs(N1-N2)
 * becomes:
 * k^2*(N1+N2)>(N1-N2)^2
 */
uint8_t
test2( statistics_t * statistics ){

   /* k = 2.65 ~ 99.2% confidence level; k^2=7; if necessary use integer fractions */
   const uint32_t thresh_test2 = (uint32_t)(statistics->sum_old2 +
                                  statistics->sum_new2)*(7UL);
   const uint32_t diff_test2 = (uint32_t)((statistics->sum_new2 - statistics->sum_old2)*
                               (statistics->sum_new2 - statistics->sum_old2));
   return(diff_test2 > thresh_test2);
}


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


