/** \file firmware/main.c
 * \brief The firmware for ATmega devices
 *
 * \author Copyright (C) 2009 samplemaker
 * \author Copyright (C) 2010 samplemaker
 * \author Copyright (C) 2010 Hans Ulrich Niedermann <hun@n-dimensional.de>
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
 * \defgroup firmware Firmware
 *
 * \defgroup firmware_memories Memory types and layout
 * \ingroup firmware
 *
 * There can be a number of kinds of variables.
 *
 *   a) Uninitialized non-register variables.  Those are placed in the
 *      .bss section in the ELF file, and in the SRAM on the device.
 *      The whole SRAM portion corresponding to the .bss section is
 *      initialized to zero bytes in the startup code, so we do not
 *      need to initialized those variables anywhere.
 *
 *   b) Initialized non-register variables.  Those are placed in the
 *      .data section in the ELF file, and in the SRAM on the device.
 *      The whole SRAM portion corresponding to the .data section is
 *      initialized to their respective byte values by autogenerated
 *      code executed before main() is run.
 *
 *   c) Initialized constants in the .text section.  Those are placed
 *      into the program flash on the device, and due to the AVR's
 *      Harvard architecture, need special instructions to
 *      read. Unused so far.
 *
 *   d) Register variables.  We only use them in the uninitialized
 *      variety so far for the assembly language version
 *      ISR(ADC_vect), if you choose to compile and link that.
 *
 *   e) EEPROM variables.  We are not using those anywhere yet.
 *
 * All in all, this means that for normal memory variables,
 * initialized or uninitialized, we do not need to initialize anything
 * at the start of main().
 *
 * Also note that the ATmega644 has 4K of SRAM. With an ADC resolution
 * of 10 bit, we need to store 2^10 = 1024 = 1K values in our
 * histogram table. This results in the following memory sizes for the
 * histogram table:
 *
 *    uint16_t: 2K
 *    uint24_t: 3K
 *    uint32_t: 4K
 *
 * We could fit the global variables into otherwise unused registers
 * to free some more SRAM, but we cannot move the stack into register
 * space. This means we cannot use uint32_t counters in the table -
 * the absolute maximum sized integer we can use is our self-defined
 * "uint24_t" type.
 *
 * \addtogroup firmware
 * @{
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "compiler.h"
#include "global.h"
#include "uart-comm.h"
#include "uart-printf.h"
#include "frame-comm.h"
#include "packet-comm.h"
#include "frame-defs.h"
#include "packet-defs.h"
#include "wdt-softreset.h"
#include "measurement-timer.h"
#include "send-table.h"
#include "main.h"
#include "data-table.h"


/* Only try compiling for supported MCU types */
#if defined(__AVR_ATmega644__) || defined(__AVR_ATmega644P__)
#else
# error Unsupported MCU!
#endif


volatile uint8_t measurement_finished;


/** Define AVR device fuses.
 *
 * CAUTION: These values are highly device dependent.
 *
 * We avoid C99 initializers to make sure that we do initialize each
 * and every fuse value in the structure. */
FUSES = {
  /* 0xd7 = low */ (FUSE_SUT1 & FUSE_CKSEL3),
  /* 0x99 = high */ (FUSE_JTAGEN & FUSE_SPIEN & FUSE_BOOTSZ1 & FUSE_BOOTSZ0),
  /* 0xfc = extended */ (FUSE_BODLEVEL1 & FUSE_BODLEVEL0)
};


/** Configure unused pins
 */
void io_init_unused_pins(void)
  __attribute__ ((naked))
  __attribute__ ((section(".init5")));
void io_init_unused_pins(void)
{
    /** \todo configure unused pins */
}


uint8_t personality_param_sram[MAX_PARAM_LENGTH];
uint8_t personality_param_eeprom[MAX_PARAM_LENGTH] EEMEM;


void params_copy_from_eeprom_to_sram(void)
{
  eeprom_read_block(personality_param_sram, personality_param_eeprom,
                    MAX_PARAM_LENGTH);
}


void general_personality_start_measurement_sram(void)
{
  sei();
  personality_start_measurement_sram();
}


void general_personality_start_measurement_eeprom(void)
{
  params_copy_from_eeprom_to_sram();
  general_personality_start_measurement_sram();
}


/** List of states for firmware state machine
 *
 * \see communication_protocol
 */
typedef enum {
  STP_READY,
  STP_MEASURING,
  STP_DONE,
  STP_ERROR
  /* STP_RESET not actually modelled as a state */
} firmware_packet_state_t;


/** Firmware FSM event handler for finished measurement */
inline static
firmware_packet_state_t eat_measurement_finished(const firmware_packet_state_t pstate)
{
  switch (pstate) {
  case STP_MEASURING:
    /* end measurement */
    cli();
    send_table(PACKET_VALUE_TABLE_DONE);
    on_measurement_finished();
    return STP_DONE;
    break;
  default:
    send_text_P(PSTR("invalid state transition"));
    wdt_soft_reset();
    break;
  }
}


/** Define static string in a single place */
const char PSTR_DONE[]      PROGMEM = "DONE";

/** Define static string in a single place */
const char PSTR_MEASURING[] PROGMEM = "MEASURING";

/** Define static string in a single place */
const char PSTR_READY[]     PROGMEM = "READY";

/** Define static string in a single place */
const char PSTR_RESET[]     PROGMEM = "RESET";


/** Firmware FSM event handler for receiving a command packet from the host
 *
 */
inline static
firmware_packet_state_t eat_packet(const firmware_packet_state_t pstate,
                                   const uint8_t cmd, const uint8_t length)
{
  /* temp vars */
  const frame_cmd_t c = (frame_cmd_t)cmd;

  uprintf("EAT PACKET: %c", cmd);

  firmware_packet_state_t next_pstate = STP_ERROR;

  switch (pstate) {
  case STP_ERROR:
  goto_STP_ERROR:
    send_text_P(PSTR("STP_ERROR again"));
    wdt_soft_reset();
    break;
  case STP_READY:
    switch (c) {
    case FRAME_CMD_PERSONALITY_INFO:
      send_personality_info();
      /* fall through */
    case FRAME_CMD_ABORT:
    case FRAME_CMD_INTERMEDIATE:
    case FRAME_CMD_STATE:
      send_state_P(PSTR_READY);
      next_pstate = STP_READY;
      break;
    case FRAME_CMD_PARAMS_TO_EEPROM:
      /* The param length has already been checked by the frame parser */
      send_state_P(PSTR("PARAMS_TO_EEPROM"));
      eeprom_update_block(personality_param_sram,
                          personality_param_eeprom, length);
      send_state_P(PSTR_READY);
      next_pstate = STP_READY;
      break;
    case FRAME_CMD_PARAMS_FROM_EEPROM:
      params_copy_from_eeprom_to_sram();
      send_eeprom_params_in_sram();
      send_state_P(PSTR_READY);
      next_pstate = STP_READY;
      break;
    case FRAME_CMD_MEASURE:
      /* The param length has already been checked by the frame parser */
      general_personality_start_measurement_sram();
      send_state_P(PSTR_MEASURING);
      next_pstate = STP_MEASURING;
      break;
    case FRAME_CMD_RESET:
      send_state_P(PSTR_RESET);
      wdt_soft_reset();
      break;
    }
    break;
  case STP_MEASURING:
    switch (c) {
    case FRAME_CMD_INTERMEDIATE:
      /** The value table will be updated asynchronously from ISRs
       * like ISR(ADC_vect) or ISR(TIMER_foo), i.e. independent from
       * this main loop.  This will cause glitches in the intermediate
       * values as the values are larger than 1 byte.  However, we
       * have decided that for *intermediate* results, those glitches
       * are acceptable.
       *
       * Keeping interrupts enabled has the additional advantage that
       * the measurement continues during send_table(), so we need not
       * concern ourselves with pausing the measurement timer, or with
       * making sure we properly reset the hardware which triggered
       * our ISR within the appropriate time range or anything
       * similar.
       *
       * If you decide to bracket the send_table() call with a
       * cli()/sei() pair, be aware that you need to solve the issue
       * of resetting the hardware properly. For example, with the
       * adc-int-mca personality, resetting the peak hold capacitor on
       * resume if an event has been detected by the analog circuit
       * while we had interrupts disabled and thus ISR(ADC_vect) could
       * not reset the peak hold capacitor.
       */
      send_table(PACKET_VALUE_TABLE_INTERMEDIATE);
      send_state_P(PSTR_MEASURING);
      next_pstate = STP_MEASURING;
      break;
    case FRAME_CMD_PERSONALITY_INFO:
      send_personality_info();
      /* fall through */
    case FRAME_CMD_PARAMS_TO_EEPROM:
    case FRAME_CMD_PARAMS_FROM_EEPROM:
    case FRAME_CMD_MEASURE:
    case FRAME_CMD_RESET:
    case FRAME_CMD_STATE:
      send_state_P(PSTR_MEASURING);
      next_pstate = STP_MEASURING;
      break;
    case FRAME_CMD_ABORT:
      send_state_P(PSTR_DONE);
      cli();
      send_table(PACKET_VALUE_TABLE_ABORTED);
      on_measurement_finished();
      send_state_P(PSTR_DONE);
      next_pstate = STP_DONE;
      break;
    }
    break;
  case STP_DONE:
    switch (c) {
    case FRAME_CMD_PERSONALITY_INFO:
      send_personality_info();
      /* fall through */
    case FRAME_CMD_STATE:
      send_state_P(PSTR_DONE);
      next_pstate = STP_DONE;
      break;
    case FRAME_CMD_RESET:
      send_state_P(PSTR_RESET);
      wdt_soft_reset();
      break;
    default:
      send_table(PACKET_VALUE_TABLE_RESEND);
      send_state_P(PSTR_DONE);
      next_pstate = STP_DONE;
      break;
    }
    break;
  }
  uprintf("Next pstate: %d", (int)next_pstate);
  if (STP_ERROR == next_pstate) {
    goto goto_STP_ERROR;
  }
  return next_pstate;
}


/** AVR firmware's main "loop" function
 *
 * Note that we create a "loop" by having the watchdog timer reset the
 * AVR device when one loop iteration is finished. This will cause the
 * system to start again with the hardware and software in the defined
 * default state.
 *
 * This function implements the finite state machine (FSM) as
 * described in \ref embedded_fsm.  The "ST_foo" and "ST_FOO"
 * definitions from #firmware_state_t refer to the states from that FSM
 * definition.
 *
 * Note that the ST_MEASURING state had to be split into two:
 * ST_MEASURING which prints its name upon entering and immediately
 * continues with ST_MEASURING_nomsg, and ST_MEASURING_nomsg which
 * does not print its name upon entering and is thus feasible for a
 * busy polling loop.
 *
 * avr-gcc knows that int main(void) ending with an endless loop and
 * not returning is normal, so we can avoid the
 *
 *    int main(void) __attribute__((noreturn));
 *
 * declaration and compile without warnings (or an unused return instruction
 * at the end of main).
 */
int main(void)
{
  /** No need to initialize global variables here. See \ref
   *  firmware_memories.
   */

  /* ST_booting */

  /** We try not to explicitly call initialization functions at the
   * start of main().  The idea is to implement the initialization
   * functions as ((naked)) and put them in the ".initN" sections so
   * they are called automatically before main() is run.
   */

  send_personality_info();
  send_state_P(PSTR_READY);

  /** Frame parser FSM state */
  enum {
    STF_MAGIC,
    STF_COMMAND,
    STF_LENGTH,
    STF_PARAM,
    STF_CHECKSUM,
  } fstate = STF_MAGIC;

  /** Index into magic/data */
  uint8_t idx = 0;
  /** Stored data for current frame */
  uint8_t cmd = 0;
  /** Stored data for current frame */
  uint8_t len = 0;

  /* Packet FSM State */
  firmware_packet_state_t pstate = STP_READY;

  /* Firmware FSM loop */
  while (1) {
    if (measurement_finished) {
      pstate = eat_measurement_finished(pstate);
    } else if (bit_is_set(UCSR0A, RXC0)) {

      /* A byte arrived via UART, so fetch it */
      const char ch = uart_getc();
      const uint8_t byte = (uint8_t)ch;

      typeof(fstate) next_fstate = fstate;

      switch (fstate) {
      case STF_MAGIC:
        if (byte == FRAME_MAGIC_STR[idx++]) {
          uart_recv_checksum_update(byte);
          if (idx < 4) {
            next_fstate = STF_MAGIC;
          } else {
            next_fstate = STF_COMMAND;
          }
        } else {
          /* syncing, not an error */
          goto restart;
        }
        break;
      case STF_COMMAND:
        uart_recv_checksum_update(byte);
        cmd = byte;
        next_fstate = STF_LENGTH;
        break;
      case STF_LENGTH:
        uart_recv_checksum_update(byte);
        len = byte;
        if (len == 0) {
          next_fstate = STF_CHECKSUM;
        } else if ((len >= personality_param_size) &&
                   (len < MAX_PARAM_LENGTH)) {
          idx = 0;
          next_fstate = STF_PARAM;
        } else {
          /* whoever sent us that wrongly sized data frame made an error */
          /** \todo Find a way to report errors without resorting to
           *        sending free text. */
          send_text_P(PSTR("param length mismatch"));
          goto error_restart_nomsg;
        }
        break;
      case STF_PARAM:
        uart_recv_checksum_update(byte);
        personality_param_sram[idx++] = byte;
        if (idx < len) {
          next_fstate = STF_PARAM;
        } else {
          next_fstate = STF_CHECKSUM;
        }
        break;
      case STF_CHECKSUM:
        if (uart_recv_checksum_check(byte)) {
          /* checksum successful */
          pstate = eat_packet(pstate, cmd, len);
          goto restart;
        } else {
          /** \todo Find a way to report checksum failure without
           *        resorting to sending free text. */
          send_text_P(PSTR("checksum fail"));
          goto error_restart_nomsg;
        }
        break;
      }
      goto skip_errors;

    error_restart_nomsg:

    restart:
      next_fstate = STF_MAGIC;
      idx = 0;
      uart_recv_checksum_reset();

    skip_errors:
      fstate = next_fstate;
      /*
    } else if (switch_is_pressed) {
      eat_switch_pressed();
      */
    }

  } /* while (1) main event loop */

} /* int main(void) */


/** @} */


/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
