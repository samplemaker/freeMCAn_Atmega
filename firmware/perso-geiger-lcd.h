/** \file firmware/perso-geiger-lcd.h
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
 * \defgroup firmware_lcd
 * \ingroup firmware
 *
 * 
 */


#ifndef PERSO_GEIGER_LCD_H
#define PERSO_GEIGER_LCD_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

//#define CPM_PER_USV_TUBE 80.257 //[CPM/(uSv/h) ZP1320]
//#define CPM_PER_USV_TUBE 149.99 //[CPM/(uSv/h) ZP1401]
#define CPM_PER_USV_TUBE 694.44 //[CPM/(uSv/h) SI8B]
//#define CPM_PER_USV_TUBE 18234.86 //[CPM/(uSv/h) 44-2]

/* record time for the short time measurement.
 * should be around 5 .. 10
 * automess: 8
 * gammascout: 5 .. 6
 * Attention: This number must be even.
 * Note: Can save two divisions if this number:60UL gets an integer
 */
#define NUM_BUF_MIN 8UL

/* m*stdev for an N count measurement equals m*sqrt(N). reltol = m*sqrt(N)/N = m/sqrt(N).
 * To have 99.7% (3.0*stdev) of the measurements within +-25% of the true count rate 
 * (i.e. we want to detect 25% excursions in the count rate with a very high probability)
 * we have to record 0.25 = 3.0/sqrt(N) => 144 counts.
 *
 * Example SI8B:
 * I.e. 0.15uSv/h ~ 104CPM=1.74CPS => Record time ~ 144cnts/1.74cps=83sec
 *
 * Attention: This number must be even.
 */

#define NUM_BUF_MAX 80UL

/* Internal resolution for the count rate in CPM */
#define RES_COUNT_RATE 10UL

/* convert to 100xPHYS_DOSE[uSv] */
#define RES_DOSE 1000

#define MAX_EQ(A, B) (((A) <= (B)) ? (B) : (A))
#define MIN_EQ(A, B) (((A) <= (B)) ? (A) : (B))
#define NUMELEM(X) ( sizeof(X)/sizeof(X[0]) )

typedef struct {
  uint32_t sum_new2;
  uint32_t sum_old2;
  uint32_t sum_short;
  uint32_t sum_total;
  uint32_t count_rate_estimator_lg;
  uint32_t count_rate_estimator_st;
  uint16_t reltol_lg;
  size_t num_short;
} statistics_t;


typedef struct {
  size_t num_max;
  volatile size_t head;
  volatile size_t count;
  size_t head_cpy;
  size_t count_cpy;
  volatile uint16_t * volatile rbuf;
} ringbuf_t;


uint16_t avrg_len(uint32_t count_rate_estimator);
uint32_t get_stats(statistics_t * statistics,
                   ringbuf_t * elements,
                   const uint16_t num_proceed);
uint8_t test1( statistics_t * statistics );
uint8_t test2( statistics_t * statistics );

inline static void
update_ringbuffer(ringbuf_t * elements, uint32_t accu_counts){
  if (elements->head < (elements->num_max - 1)){
    elements->head++;
  }
  else{
    elements->head = 0;
  }
  elements->rbuf[elements->head] = accu_counts;
  if (elements->count < elements->num_max){
    elements->count++;
  }
  else{
    elements->count = elements->num_max;
  };
}

inline static
uint32_t cpm2doserate(uint32_t count_rate){
 return((RES_DOSE*count_rate)/(uint32_t)(CPM_PER_USV_TUBE*(double)(RES_COUNT_RATE)));
}

#endif /* !PERSO_GEIGER_LCD_H */

