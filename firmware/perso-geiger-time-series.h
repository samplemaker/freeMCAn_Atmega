#ifndef PERSO_GEIGER_TIME_SERIES_H
#define PERSO_GEIGER_TIME_SERIES_H

#include <avr/pgmspace.h>

enum {
  CHAR_1,
  CHAR_2,
  CHAR_3,
  CHAR_4,
  CHAR_UP,
  CHAR_DOWN,
  NUM_CHAR
};


//must not be located in PROGMEM otherwise it does not work during INIT
static const char font[NUM_CHAR][8]={
{0x0,0x10,0x8,0x4,0x2,0x1,0x0,0x0}, //'\'
{0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x0},  //'|'
{0x0,0x1,0x2,0x4,0x8,0x10,0x0,0x0}, //'/'
{0x0,0x0,0x0,0x1f,0x0,0x0,0x0,0x0}, //'-'
{0x4,0xe,0x1f,0x4,0x4,0x4,0x4,0x0}, // arrow up
{0x4,0x4,0x4,0x4,0x1f,0xe,0x4,0x0}  //arrow down
};

inline static void
set_font (void)
{
  for (uint8_t num=0; num<NUM_CHAR; num++){
     //set LCD address to Character Generator RAM
     lcd_command(0x40 + num*8);
     for (uint8_t i=0; i<8; i++) {
       lcd_data(font[num][i]);
     }
  }
  lcd_gotoxy(0,0);
}


inline static char
get_indicator (void)
{
  static uint8_t indicator_state = 0;
  switch (indicator_state) {
    case 0:
        indicator_state ++;
        return(CHAR_1);
    break;
    case 1:
        indicator_state ++;
        return(CHAR_2);
    break;
    case 2:
        indicator_state ++;
        return(CHAR_3);
    break;
    case 3:
        indicator_state = 0;
        return(CHAR_4);
    break;
    default:
        return('0');
    break;
  }
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
inline static void uint_to_ascii(char *out_str, uint32_t value, const uint8_t pos, const uint8_t print){
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

#endif /* !PERSO_GEIGER_TIME_SERIES_H */


