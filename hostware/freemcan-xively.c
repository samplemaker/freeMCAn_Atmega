/** \file hostware/freemcan-xively.c
 * \brief Experimental xively export
 *
 * \author Copyright (C) 2014 samplemaker
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
 *
 */

#include <xively.h>
#include <xi_helpers.h>

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freemcan-xively.h"

#define CPM_PER_USV_TUBE 80.257 //[CPM/(uSv/h) ZP1320]

static const char device_key[]="xyz";
static const char channel[]="freemcan\0";
static const int feed_id = 123;

static xi_feed_t xi_feed;
static xi_context_t* xi_context;

const char *time_to(const time_t time)
{
  const struct tm *tm_ = localtime(&time);
  assert(tm_);
  static char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S%z", tm_);
  return buf;
}


void print_data(const personality_info_t *personality_info,
                const packet_value_table_t *value_table_packet)
{
/* const time_t start_time = (value_table_packet->token)?
    *((const time_t *)value_table_packet->token) : 0 ; */

  const size_t element_count = value_table_packet->element_count;
  const uint32_t elapsed_time = value_table_packet->duration +
    (value_table_packet->total_duration * (value_table_packet->element_count));
  const uint32_t tdur  = value_table_packet->total_duration;
  printf("timer:%d  interval:%d  elapsed:%d  ecounts:%zu\n\r",
         value_table_packet->duration,tdur,elapsed_time,element_count);
  const time_t start_time = value_table_packet->receive_time - elapsed_time;
  //const time_t start_time = time(NULL) - elapsed_time;
  if ((value_table_packet->reason == PACKET_VALUE_TABLE_INTERMEDIATE) && (element_count > 0)){
    for (size_t i = 0; i < element_count; i ++){
      const time_t ts = start_time + i * tdur;
      const char *st = time_to(ts);
      const float dose_rate = 60.0*(float)(value_table_packet->elements[i])/
                              ((float)(tdur)*CPM_PER_USV_TUBE);
      const float round_data = floorf(dose_rate*1000.0+0.5)/1000.0;
      printf("%zu\t%u\t%0.2f\t%ld\t%s\n\r", i, value_table_packet->elements[i],
             round_data, ts, st);
    }
  }
}


int push_xively(const personality_info_t *personality_info,
                 const packet_value_table_t *value_table_packet)
{
/* const time_t start_time = (value_table_packet->token)?
    *((const time_t *)value_table_packet->token) : 0 ; */

  const size_t element_count = value_table_packet->element_count;

  const uint32_t elapsed_time = value_table_packet->duration +
    (value_table_packet->total_duration * (value_table_packet->element_count));
  const uint32_t tdur  = value_table_packet->total_duration;

  printf("timer:%d  interval:%d  elapsed:%d  ecounts:%zu\n\r",
         value_table_packet->duration,tdur,elapsed_time,element_count);


  const time_t start_time = value_table_packet->receive_time - elapsed_time;
  //const time_t start_time = time(NULL) - elapsed_time;

  if ((value_table_packet->reason == PACKET_VALUE_TABLE_INTERMEDIATE) && (element_count > 0)){
   xi_context = xi_create_context(XI_HTTP,
                                  device_key,
                                  feed_id );
   if(xi_context == 0) return -1;
   memset(&xi_feed, 0, sizeof(xi_feed_t));
   xi_feed.feed_id = feed_id;

   /* send the datapoints in bundles of XI_MAX_DATAPOINTS */
   size_t j = 0;
   for (size_t i = 0; i < element_count; i ++){

      const time_t ts = start_time + i * tdur;
      const char *st = time_to(ts);
      const float dose_rate = 60.0*(float)(value_table_packet->elements[i])/
                              ((float)(tdur)*CPM_PER_USV_TUBE);
      const float round_data = floorf(dose_rate*1000.0+0.5)/1000.0;
      printf("%zu\t%u\t%0.2f\t%ld\t%s\n\r", i, value_table_packet->elements[i], round_data, ts, st);

      /* get the datastream pointer from the total hard coded
         xi_feed_t -> datastreams[ XI_MAX_DATASTREAMS ]  */
      xi_datastream_t* xi_datastream = &xi_feed.datastreams[j];
      /* copy "channel" string into "xi_datastream->datastream_id", but stops whenever
       * "\0" is reached or the "size" is exceeded. returns number of copied
       * characters or -1 if an error occurred. upload each datapoint in the same channel */
      xi_str_copy_untiln(xi_datastream->datastream_id, 
                         sizeof(xi_datastream->datastream_id), 
                         channel,'\0');
      /* get the datapoint from each datastream */
      xi_datapoint_t* xi_datapoint = &xi_datastream->datapoints[0]; 
      xi_set_value_i32(xi_datapoint, value_table_packet->elements[i]);
      // xi_set_value_f32(xi_datapoint, round_data);
      xi_datapoint->timestamp.timestamp = ts;
      /* only one datapoint per datastream */
      xi_datastream->datapoint_count = 1;

      /* at this point j is the index of the last element written from 0 onwards */
      j++;
      /* from now j contains the total number of written elements */

      if (j == XI_MAX_DATASTREAMS) {
        xi_feed.datastream_count = j;
        xi_feed_update(xi_context, &xi_feed);
        printf("Update return: %d (%s)\n\r", (int) xi_get_last_error(),
                xi_get_error_string(xi_get_last_error()) );
        if (xi_get_last_error()){
          return(-1);
        }
        j = 0;
      }
   }
   /* if there are still j elements left sent them */
   if (j > 0){
      xi_feed.datastream_count = j;
      xi_feed_update(xi_context, &xi_feed);
      printf("Update return: %d (%s)\n\r", (int) xi_get_last_error(),
             xi_get_error_string(xi_get_last_error()) );
      if (xi_get_last_error()){
        return(-1);
      }
   }
   xi_delete_context(xi_context);
  }
  return(0);
}
