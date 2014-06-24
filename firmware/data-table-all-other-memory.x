/* Memory layout before this runs:
 *
 *     __noinit_end:    end of uninitialized data
 *
 *     _end:            end of whatever...
 *     __heap_start:    mallocable heap, growing upwards
 *
 *     __heap_end:
 *
 *     RAM_END:         stack, growing downwards
 *
 * Memory layout after this runs:
 *
 *     __noinit_end:    end of unitialized data
 *     data_table:      fixed size data table
 *
 *     data_table_end:
 *     _end:            end of whatever
 *     __heap_start:    mallocable heap, growing upwards
 *
 *     __heap_end:
 *
 *     RAM_END:         stack, growing downwards
 *
 * In both cases, neither malloc nor the stack pointer should go past
 * __heap_end, otherwise you need to verify they never do that at the
 * same time.
 */
/** \file firmware/table-element.h
 * \brief Table Element type definitions
 *
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
 * \defgroup table_element Table Element type
 * \ingroup firmware_generic
 * @{
 */
/** Histogram element size */
/** @} */
/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */

SECTIONS {
  /* align start and end of the table to borders which are compatible to uint8,16,24 and 32 */
  data_table = ALIGN(( __noinit_end ), 12) ;
  data_table_end = ( RAM_END - MAX_RUNTIME_STACK_SIZE - MALLOC_HEAP_SIZE - 11 ) ;
  data_table_end = ALIGN(( data_table_end ), 12) ;
  data_table_size = data_table_end - data_table ;
  /* Unused definitions for table size in different units
   * data_table_size_by_2 = data_table_size / 2 ;
   * data_table_size_by_3 = data_table_size / 3 ;
   * data_table_size_by_4 = data_table_size / 4 ;
   */
  _end = data_table_end ;
  __heap_start = _end ;
  __heap_end = __heap_start + MALLOC_HEAP_SIZE ;
}
INSERT AFTER .noinit ;
ASSERT ( ( __heap_end + MAX_RUNTIME_STACK_SIZE ) <= RAM_END, "(data+stack+table) size is too large for SRAM") ;
ASSERT ( data_table_size >= 1024, "data table size is smaller than 1K" ) ;
