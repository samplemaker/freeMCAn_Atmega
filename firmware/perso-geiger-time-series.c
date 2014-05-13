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

  //DDRD |= _BV(DDD6);
  //PORTD &= ~_BV(PD6);

  /* Init display */
  lcd_init(LCD_DISP_ON);
  lcd_clrscr();
  lcd_puts("CPM +-1SDEV");
}


/** External INT0, i.e. count a GM event */
ISR(INT0_vect)
{ 
  /* to calculate CPMs */
  total_counts ++;

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

    lcd_update = 1;

    /* to calculate CPMs */
    time_elapsed++;

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

/* const 80 ticks */
static inline uint8_t
sqrt16_floor (uint16_t q)
{
    uint8_t res = 0;
    uint8_t mask = 1 << 7;
 
    asm("0:	add  %[res], %[mask]"   "\n"
        "	mul  %[res], %[res]"    "\n"
        "	cp   %A[q], R0"         "\n"
        "	cpc  %B[q], R1"         "\n"
        "	brsh 1f"                "\n"
        "	sub  %[res], %[mask]"   "\n"
        "1:	lsr  %[mask]"           "\n"
        "	brne 0b"                "\n"
        "	clr  __zero_reg__"
        : [res] "+r" (res), [mask] "+r" (mask)
        : [q] "r" (q));
 
    return res;
}

inline static uint16_t c_sqrt32 (uint32_t q)
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

inline static uint32_t divu10(uint32_t n) {
  uint32_t q, r;
  q = (n >> 1) + (n >> 2);
  q = q + (q >> 4);
  q = q + (q >> 8);
  q = q + (q >> 16);
  q = q >> 3;
  r = n - q*10;
  return q + ((r + 6) >> 4);
}

void display_count_stats(void)
{
  /*idee: adaptive integrationszeit nach sqrt(N) */
  if (lcd_update){
    /* 775us @18.432MHz */
    //PORTD |= _BV(PD6);

    /* atomic */
    lcd_update = 0;

    /* make a local copy to be independant from the interrupt handler */
    uint32_t tmp1;
    uint32_t l_total_counts = total_counts;
    do {
      tmp1 = l_total_counts;
      l_total_counts = total_counts;
    } while (l_total_counts != tmp1);

    uint16_t tmp2;
    uint16_t l_time_elapsed = time_elapsed;
    do {
      tmp2 = l_time_elapsed;
      l_time_elapsed = time_elapsed;
    } while (l_time_elapsed != tmp2);

    /* visible characters per line */
    #define max_len 16
    char ch_str[max_len];

    /* in counts per minute */
    uint32_t int_rate = (uint32_t)((60UL*10UL*l_total_counts)/l_time_elapsed);
    /* convert int_rate into string assuming one decimal place */
    /* # of characters to display including decimal point */
    #define max_num_rate 7
    int8_t i = max_num_rate - 1;
    ch_str[i--] = int_rate % 10 + '0';
    int_rate /= 10;
    ch_str[i--] = '.';
    do {
       ch_str[i--] = int_rate % 10 + '0';
    } while ((int_rate /= 10) > 0);
    while (i >= 0) {
       ch_str[i--] = ' ';
    }

    uint16_t rel_tol;
    /* spent some extra accuracy for low number of counts */
    rel_tol = (uint16_t)((60UL*100UL*10UL)/c_sqrt32(3600UL*l_total_counts));

    /* convert standard deviation into string assuming one decimal place */
    i = max_len - 1;
    ch_str[i--] = '\0';
    ch_str[i--] = '%';
    ch_str[i--] = rel_tol % 10 + '0';
    rel_tol /= 10;
    ch_str[i--] = '.';
    do {
      ch_str[i--] = rel_tol % 10 + '0';
    } while ((rel_tol /= 10) > 0);
    ch_str[i--] = '-';
    ch_str[i--] = '+';
    while (i >= max_num_rate) {
       ch_str[i--] = ' ';
    }

    lcd_gotoxy(0,1);
    lcd_puts(ch_str);

    //PORTD &= ~_BV(PD6);
  }
}


void on_measurement_finished(void)
{
  beep_kill_all();
  timer1_init_quick();
}


void personality_start_measurement_sram(void)
{
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
