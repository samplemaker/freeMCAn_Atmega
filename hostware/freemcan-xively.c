#include <xively.h>
#include <xi_helpers.h>

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freemcan-xively.h"

#define CPM_PER_USV_TUBE 80.257 //[CPM/(uSv/h) ZP1320]

static const char device_key[]=" ";
static const char channel[]="freemcan\0";
static const int feed_id = 2;

static xi_context_t* xi_context;


int set_up_xively(){

   xi_context = xi_create_context(XI_HTTP,
                                  device_key,
                                  feed_id );
   if(xi_context == 0) return -1;
   else return 0;
}



void close_xively(void){
   xi_delete_context(xi_context);
}


int put_point(const float data, const time_t timestamp){

    xi_datapoint_t datapoint;
    xi_set_value_f32(&datapoint, data);
    datapoint.timestamp.timestamp = timestamp;
    xi_datastream_update(xi_context,
                         feed_id,
                         channel,
                         &datapoint);
 
    printf( "Xively-Transfer:%d :%s\n\r", 
            (int) xi_get_last_error(),
            xi_get_error_string(xi_get_last_error()) );

   return((int) xi_get_last_error());
}

const char *time_to(const time_t time)
{
  const struct tm *tm_ = localtime(&time);
  assert(tm_);
  static char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S%z", tm_);
  return buf;
}


void push_xively(const personality_info_t *personality_info,
                 const packet_value_table_t *value_table_packet)
{
/* const time_t start_time = (value_table_packet->token)?
    *((const time_t *)value_table_packet->token) : 0 ; */

  const size_t element_count = value_table_packet->element_count;

  const uint32_t elapsed_time = value_table_packet->duration +
    (value_table_packet->total_duration * (value_table_packet->element_count));
  const uint32_t tdur  = value_table_packet->total_duration;

  printf("timer:%d  interval:%d  elapsed:%d  counts:%d\n\r",
         value_table_packet->duration,tdur,elapsed_time,(int)(element_count));

  const time_t start_time = value_table_packet->receive_time - elapsed_time;

  if ((value_table_packet->reason == PACKET_VALUE_TABLE_INTERMEDIATE) && (element_count > 0)){


/* Problem fixed feed length: See
#define XI_MAX_DATASTREAMS  16
xi_feed_t
*/

  if (!set_up_xively()){

    for (size_t i=0; i<element_count; i++) {
       const time_t ts = start_time + i * tdur;
       const char *st = time_to(ts);
       printf("%zu\t%u\t%ld\t%s\n\r", i, value_table_packet->elements[i], ts, st);
       const float dose_rate = 60.0*(float)(value_table_packet->elements[i])/((float)(tdur)*CPM_PER_USV_TUBE);
       const float rnd_data = floorf(dose_rate * 1000.0 + 0.5) / 1000.0;
       put_point(rnd_data, ts);
    }
    close_xively();
  }

  } 
}
