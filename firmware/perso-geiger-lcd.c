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
#include "freemcan-lcd.h"
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
size_t
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
          const size_t num_proceed){

   /* calculate the sum over various buffer lengths of the recorded data */
   size_t num_proceed2 = (num_proceed >> 1);
   size_t i=0;
   size_t current_pos = elements->head_cpy;
   uint32_t sum_total = 0;
   while (i < num_proceed) {
      sum_total += elements->rbuf[current_pos];
      if (current_pos){
        current_pos--;
      }
      else{
        current_pos = elements->num_max - 1;
      }
      i++;
      if (i == num_proceed2) statistics->sum_new2 = sum_total;
      if (i == 2*num_proceed2) statistics->sum_old2 = sum_total;
      if (i == statistics->num_short) statistics->sum_short = sum_total;
   }
   statistics->sum_old2 = statistics->sum_old2 - statistics->sum_new2;
   statistics->sum_total = sum_total;

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


/* check wheather the short time measurement (i.e. the last and newest num_short
 * samples) deviate more than a certain amount from the long time count rate.
 * to keep it simple we only calculate the expected (standard deviation)^2
 * from the short measurement and check the difference to the count rate estimator.
 * hence the test is good for long time averaging count rate estimators. the test
 * is bad somewhere in the transistion region. for small averaging times the 
 * fluctuations between estimator and short time measurement are identical.
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


