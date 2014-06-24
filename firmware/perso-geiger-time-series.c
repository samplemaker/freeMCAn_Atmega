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

#include "compiler.h"
#include "global.h"
#include "packet-comm.h"
#include "timer1-measurement.h"
#include "uart-comm.h"
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


#define TELEMENT_SIZE (sizeof(table_element_t))

extern volatile char strt_ptr[] asm("data_table");
extern volatile char end_ptr[] asm("data_table_end");
extern char data_table_size[];
const size_t table_size = (size_t) data_table_size;

static volatile table_element_t num_counts;

static volatile uint32_t ofs_wr;



/** Data table info
 *
 * \see data_table
 */
data_table_info_t data_table_info = {
  /** Actual size of #data_table in bytes
   * We update this value whenever new time series data has been
   * recorded. The initial value is "one element".
   */
  0,
  /** Type of value table we send */
  VALUE_TABLE_TYPE_TIME_SERIES,
  /** Table element size */
  BITS_PER_VALUE
};


/** See * \see data_table */
PERSONALITY("geiger-time-series",
            2,0,
            1,
            0,
            BITS_PER_VALUE);


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
}


/** External INT0, i.e. count a GM event */
ISR(INT0_vect)
{
  /* activate loudspeaker */
  _beep();
  /* toggle output pin with LED */
  PIND |= _BV(PD4);

  table_element_inc(&num_counts);

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
    /** We do not touch the measurement_finished flag ever again after
     * setting it. */
    timer1_count--;

    if (timer1_count == 0) {

      table_element_copy((volatile table_element_t *)(strt_ptr + ofs_wr),
                         &num_counts);
      table_element_zero(&num_counts);
      data_table_info.size += (TELEMENT_SIZE);
      if (ofs_wr < ((table_size) - (TELEMENT_SIZE))){
        ofs_wr += (TELEMENT_SIZE);
      }
      else{
        ofs_wr = 0;
      }

      if (data_table_info.size < (table_size)) {
        timer1_count = orig_timer1_count;
      } else {
        //in case of overflow overwrite the record data
        timer1_count = orig_timer1_count;
        data_table_info.size = table_size;
        //GF_SET(GF_MEASUREMENT_FINISHED);
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



//send the bloody data
void send_data_from_ringbuf(void)
{
  size_t size_to_send;
  volatile uint32_t ofs_wr_cpy;
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE){
    size_to_send = data_table_info.size;
    ofs_wr_cpy = ofs_wr;
    data_table_info.size = 0;
  }
  const volatile char * wr_ptr = strt_ptr + ofs_wr_cpy;
  const size_t size_new = wr_ptr - strt_ptr; // = ofs_wr_cpy
  if (size_new > size_to_send){
     uart_putb((const void *)(wr_ptr - size_to_send), size_to_send);
  }
  else
  {
     const size_t size_old = size_to_send - size_new;
     const volatile char * rd_ptr = end_ptr - size_old;
     uart_putb((const void *)rd_ptr, size_old);
     uart_putb((const void *)strt_ptr, size_new);
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
