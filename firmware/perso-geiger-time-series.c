/** \file firmware/perso-geiger-time-series.c
 * \brief Personality: Geiger Counter Recording Time Series
 *
 * \author Copyright (C) 2010 samplemaker
 * \author Copyright (C) 2010 Hans Ulrich Niedermann <hun@n-dimensional.de>
 * \author Copyright (C) 1998, 1999, 2000, 2007, 2008, 2009 Free Software Foundation, Inc.
 *         (for the assembly code in ts_init() to clear data_table)
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
 * \defgroup geiger_time_series Personality: Geiger Counter Recording Time Series
 * \ingroup firmware_personality_groups
 *
 * Geiger Counter recording time series
 *
 * @{
 */


#include <stdlib.h>
#include <string.h>


#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

/** Histogram element size */
#define BITS_PER_VALUE 16

#include "lcd.h"
#include "compiler.h"
#include "global.h"
#include "packet-comm.h"
#include "timer1-measurement.h"
#include "uart-printf.h"
#include "main.h"
#include "table-element.h"
#include "data-table.h"
#include "beep.h"
#include "init-functions.h"
#include "perso-geiger-time-series.h"

#ifndef F_CPU
# error Need F_CPU defined for util/delay.h
#endif
#include <util/delay.h>


/** The table
 *
 * Note that we have the table location and size determined by the
 * linker script time-series-table.x.
 */
extern volatile table_element_t table[] asm("data_table");


/** End of the table: Still needs rounding */
extern volatile table_element_t data_table_end[];


/** Pseudo symbol - just use its address */
extern volatile char data_table_size[];


/* LCD */
uint32_t total_counts;
uint16_t time_elapsed;
uint8_t lcd_update;

/** Data table info
 *
 * \see data_table
 */
data_table_info_t data_table_info = {
  /** Actual size of #data_table in bytes
   * We update this value whenever new time series data has been
   * recorded. The initial value is "one element".
   */
  sizeof(table[0]),
  /** Type of value table we send */
  VALUE_TABLE_TYPE_TIME_SERIES,
  /** Table element size */
  BITS_PER_VALUE
};


/** See * \see data_table */
PERSONALITY("geiger-time-series",
            2,0,
            1,
            ((size_t)(&data_table_size)),
            BITS_PER_VALUE);


/** End of the table: Never write to *table_cur when (table_cur>=table_end)! */
volatile table_element_t *volatile table_end =
  (table_element_t volatile *)((char *)data_table_end -
                                   (sizeof(table_element_t)-1));

/** Pointer to the current place to store the next value at */
volatile table_element_t *volatile table_cur = table;


#define MAX_EQ(A, B) (((A) <= (B)) ? (B) : (A))
#define MIN_EQ(A, B) (((A) <= (B)) ? (A) : (B))
#define NUMELEM(X) ( sizeof(X)/sizeof(X[0]) )

//#define CPM_PER_USV_TUBE 80.257 //[CPM/(uSv/h) ZP1320]
#define CPM_PER_USV_TUBE 149.99 //[CPM/(uSv/h) ZP1401]
//#define CPM_PER_USV_TUBE 694.44 //[CPM/(uSv/h) SI8B]
//#define CPM_PER_USV_TUBE 18234.86 //[CPM/(uSv/h) 44-2]

/* record time for the short time measurement.
 * should be around 5 .. 10
 */
#define NUM_TIME_UNITS_ST 8UL

/* m*stdev for an N count measurement equals m*sqrt(N). reltol = m*sqrt(N)/N = m/sqrt(N).
 * To have 90% (1.65 stdev) of the measurements within +-15% of the true count rate 
 * we have to record 0.15 = 1.65/sqrt(N) => 121 counts
 * Example ZP1320:
 * I.e. 0.5uSv/h ~ 40CPM=0.66CPS => Record time ~ 121/0.66=180sec
*/
#define NUM_BUF_MAX 120UL
static volatile uint16_t buf[NUM_BUF_MAX];

/* Internal resolution for the count rate in CPM */
#define RES_COUNT_RATE 100UL

/* convert to 100xPHYS_DOSE[uSv] */
#define RES_DOSE 1000

static volatile uint32_t num_counts;
static volatile uint32_t pos_wr;
static volatile uint32_t units_recorded;  

/* atomics */
static volatile uint8_t do_count_stats;

//must not be located in PROGMEM
static const char dose_str[] = {"uSv/h\0"};

typedef enum {
  STATE_LOW_RATE,
  STATE_HIGH_RATE
} state_range_t;

static state_range_t state_range = STATE_LOW_RATE;

/* expected averaging time for the various ranges to get an acceptable accuracy */
typedef enum {
  TIME_LOW_RATE = NUM_BUF_MAX,
  TIME_HIGH_RATE = 30,
} range_time_t;

/* threshold rate [PHYS_CPM] x RES_COUNT_RATE for switching between the ranges (autoranging)
 * ranges including hysteresis must not overlap */
typedef enum {
  RATE_LOWHIGH = (RES_COUNT_RATE*80UL), //transition from range "low" to range "high"
  RATE_HIGHLOW = (RES_COUNT_RATE*40UL), //transition from range "high" to range "low"
} range_rate_t;


inline static
uint32_t cpm2doserate(uint32_t count_rate){
 return((RES_DOSE*count_rate)/(uint32_t)(CPM_PER_USV_TUBE*(double)(RES_COUNT_RATE)));
}


/** Print some status messages for debugging */
INIT_FUNCTION(init8, data_table_print_status)
{
#ifdef VERBOSE_STARTUP_MESSAGES
  uprintf("<data_table_print_status>");
  uprintf("%-25s %p", "table",      table);
  uprintf("%-25s %p", "table_cur",  table_cur);
  uprintf("%-25s %p", "table_end",  table_end);
  const size_t UV(sizeof_table) = ((char*)table_end) - ((char*)table_cur);
  uprintf("%-25s 0x%x = %d >= %d * %d",
          "table_end - table_cur",
          _UV(sizeof_table), _UV(sizeof_table),
          _UV(sizeof_table)/sizeof(*table_cur), sizeof(*table_cur));
  uprintf("</data_table_print_status>");
#endif
}


/** Initialize peripherals
 *
 * Set up output pins to Pollin Eval Board speaker and LED2.
 */
INIT_FUNCTION(init5, personality_io_init)
{
  /* configure pin 18 as an output                               */
  DDRD |= (_BV(DDD4));
  /* Clear LED on PD4. Will be toggled when a GM event is detected. */
  PORTD &= ~_BV(PD4);

  DDRD |= _BV(DDD6);
  PORTD &= ~_BV(PD6);

  /* Init display */
  lcd_init(LCD_DISP_ON);
  set_font();
  lcd_clrscr();
}


/** External INT0, i.e. count a GM event */
ISR(INT0_vect)
{ 
  /* LCD */
  num_counts++;

  /* activate loudspeaker */
  _beep();
  /* toggle output pin with LED */
  PIND |= _BV(PD4);

  if (table_cur < table_end) {
    table_element_inc(table_cur);
  }
  /* debounce any pending unwanted interrupts caused bouncing
     during transition */
  EIFR |= _BV(INTF0);
}


volatile uint16_t timer1_count;
volatile uint16_t orig_timer1_count;


ISR(TIMER1_COMPA_vect)
{
  /* toggle a sign PORTD ^= _BV(PD5); (done automatically) */

  if (GF_IS_CLEARED(GF_MEASUREMENT_FINISHED)) {

    /* LCD */
    do_count_stats = 1;
    if (pos_wr < ((NUM_BUF_MAX) - 1)){
      pos_wr++;
    }
    else{
      pos_wr = 0;
    }
    buf[pos_wr] = num_counts;
    num_counts = 0;
    if (units_recorded < (NUM_BUF_MAX)){
      units_recorded++;
    }
    else{
      units_recorded = (NUM_BUF_MAX);
    };

    /** We do not touch the measurement_finished flag ever again after
     * setting it. */
    timer1_count--;
    if (timer1_count == 0) {
      /* Timer has elapsed. Advance to next counter element in time
       * series, and restart the timer countdown. */
      table_cur++;
      if (table_cur < table_end) {
        data_table_info.size += sizeof(*table_cur);
        timer1_count = orig_timer1_count;
      } else {
        GF_SET(GF_MEASUREMENT_FINISHED);
      }
    }
  }
}


/** Setup of INT0
 *
 * INT0 via pin 16 is configured but not enabled
 * Trigger on falling edge
 * Enable pull up resistor on Pin 16 (20-50kOhm)
 */
inline static
void trigger_src_conf(void)
{

    /* Configure INT0 pin 16 as input */
    /* Reset Int0 pin 16 bit DDRD in port D Data direction register */
    DDRD &= ~(_BV(DDD2));
    /* Port D data register: Enable pull up on pin 16, 20-50kOhm */
    PORTD &= ~_BV(PD2);

    /* Disable interrupt "INT0" (clear interrupt enable bit in
     * external interrupt mask register) otherwise an interrupt may
     * occur during level and edge configuration (EICRA)  */
    EIMSK &= ~(_BV(INT0));
    /* Level and edges on the external pin that activates INT0
     * is configured now (interrupt sense control bits in external
     * interrupt control register A). Disable everything.  */
    EICRA &= ~(_BV(ISC01) | _BV(ISC00));
    /* Now enable interrupt on falling edge.
     * [ 10 = interrupt on rising edge
     *   11 = interrupt on falling edge ] */
    EICRA |=  _BV(ISC01);
    /* Clear interrupt flag by writing a locical one to INTFn in the
     * external interrupt flag register.  The flag is set when a
     * interrupt occurs. if the I-flag in the sreg is set and the
     * corresponding flag in the EIFR the program counter jumps to the
     * vector table  */
    EIFR |= _BV(INTF0);
    /* reenable interrupt INT0 (External interrupt mask
     * register). we do not want to jump to the ISR in case of an interrupt
     * so we do not set this bit  */
    EIMSK |= (_BV(INT0));
}


void display_count_stats(void)
{
  char display[17];
  uint32_t pos_rd;
  uint32_t units_recorded_cpy;
  static uint32_t count_rate_estimator_lg = 0;

  if (do_count_stats){
    do_count_stats = 0;

    ATOMIC_BLOCK (ATOMIC_RESTORESTATE){
      pos_rd = pos_wr; 
      units_recorded_cpy = units_recorded;
    }
 
    if (units_recorded_cpy < (NUM_TIME_UNITS_ST)){
      count_rate_estimator_lg = 0;
    }
    else{

      uint32_t units_to_consider = 0;
      /* autoranging controls the max possible record length and gives higher
       * dynamic response at higher count rates */
      switch (state_range) {
        case (STATE_LOW_RATE) :
           /* possibly not yet recorded enough samples at boot time */
           units_to_consider = MIN_EQ(TIME_LOW_RATE, units_recorded_cpy);
           if (count_rate_estimator_lg > RATE_LOWHIGH){
             state_range = STATE_HIGH_RATE;
             units_to_consider = MIN_EQ(TIME_HIGH_RATE, units_recorded_cpy);
             lcd_gotoxy(4,1);
             lcd_putc(CHAR_UP);
           };
        break;
        case (STATE_HIGH_RATE) :
           units_to_consider = MIN_EQ(TIME_HIGH_RATE, units_recorded_cpy);
           if (count_rate_estimator_lg < RATE_HIGHLOW){
             state_range = STATE_LOW_RATE;
             units_to_consider = MIN_EQ(TIME_LOW_RATE, units_recorded_cpy);
             lcd_gotoxy(4,1);
             lcd_putc(CHAR_DOWN);
           };
        break;
        default:
        break;
      }

      /* calculate the sum over various buffer lengths */
      uint32_t sum_new2=0, sum_old2=0, sum_short=0;
      uint16_t units_to_consider2 = (units_to_consider >> 1);
      uint32_t sum_total=0;
      uint16_t i=0;
      uint16_t current_pos = pos_rd;
      while (i < units_to_consider) {
        sum_total += buf[current_pos];
        if (current_pos){
          current_pos--;
        }
        else{
          current_pos = NUMELEM(buf) - 1;
        }
        i++;
        if (i == units_to_consider2) sum_new2 = sum_total;
        if (i == 2*units_to_consider2) sum_old2 = sum_total;
        if (i == NUM_TIME_UNITS_ST) sum_short = sum_total;
      }
      sum_old2 = sum_old2 - sum_new2;

      /* CPM over the whole record */
      count_rate_estimator_lg = (RES_COUNT_RATE*60UL*sum_total)/
                                (units_to_consider);

      /* average and deviation for the past NUM_TIME_UNITS_ST counts
       * this is to detect a promt count rate excursion */
      const uint32_t count_rate_estimator_st = (RES_COUNT_RATE*60UL*sum_short)/
                                               (NUM_TIME_UNITS_ST);
      /* standard deviation of the short time measurement calculated
       * from the long time count rate */
      const uint16_t stdev_st = c_sqrt32((count_rate_estimator_lg*
                                          RES_COUNT_RATE*60UL)/
                                          NUM_TIME_UNITS_ST);
      /* for simplicity we assume that the short time stdev is much
       * greater than the long term */
      const int32_t count_rate_diff = (int32_t)(count_rate_estimator_lg)-
                                      (int32_t)(count_rate_estimator_st);

      /* two measurements with equal measurement time (units_to_consider2)
       * this is to detect long term drifts (~ compare the first half of
       * the record with the second half). since both records have same
       * length we don't need to bother with count rates.
       * statistical test with 99.2% ~ sqrt(7)=k_[1-gamma]/2 */
      const uint16_t counts_thresh2 = (c_sqrt32((sum_old2+sum_new2)*7UL));
      int32_t counts_diff2 = (((int32_t)(sum_new2) - (int32_t)(sum_old2)));

      if (labs(counts_diff2) > counts_thresh2){
        ATOMIC_BLOCK (ATOMIC_RESTORESTATE){
           units_recorded = NUM_TIME_UNITS_ST;
        }
        lcd_gotoxy(2,1);
        lcd_putc('L');
      }
      /* promt short time excursion more than 4 stdevs */
      if (labs(count_rate_diff) > 4*stdev_st) {
        ATOMIC_BLOCK (ATOMIC_RESTORESTATE){
           units_recorded = NUM_TIME_UNITS_ST;
        }
        lcd_gotoxy(2,1);
        lcd_putc('S');
      }

      const uint32_t doserate = cpm2doserate(count_rate_estimator_lg);
      if (doserate < 10*RES_DOSE){
        /* 3 decimal places 4 digits: 0.000 .. 9.999; RES_DOSE=10^3 */
        uint_to_ascii(display, doserate, 3, 4);
        lcd_gotoxy(0,0);
        lcd_puts(display);
      }
      else{
        /* 2 decimal places 4 digits: 10.00 .. 99.99; RES_DOSE=10^3 */
        uint_to_ascii(display, doserate/10, 2, 4);
        lcd_gotoxy(0,0);
        lcd_puts(display);
      }

      /* 1 decimal place 6 digits; RES_COUNT_RATE=10^2  */
      uint_to_ascii(display, count_rate_estimator_lg, 2, 6);
      lcd_gotoxy(9,1);
      lcd_puts(display);

      uint_to_ascii(display, units_to_consider, 0, 3);
      lcd_gotoxy(13,0);
      lcd_puts(display);

      lcd_gotoxy(0,1);
      lcd_putc(get_indicator());
    }
  }
}


void on_measurement_finished(void)
{
  beep_kill_all();
  timer1_init_quick();
}


void personality_start_measurement_sram(void)
{
  lcd_gotoxy(5,0);
  lcd_puts(dose_str);

  const void *voidp = &pparam_sram.params[0];
  const uint16_t *timer1_value = voidp;
  trigger_src_conf();
  timer1_init(*timer1_value);
}


/** @} */

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
