#ifndef FREEMCAN_XIVELY_H
#define FREEMCAN_XIVELY_H

#include "freemcan-packet.h"

void push_xively(const personality_info_t *personality_info,
                 const packet_value_table_t *value_table_packet);

#endif /* !FREEMCAN_XIVELY_H */

