#ifndef GATEWAY_WIRE_H
#define GATEWAY_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define GW_MGMT_PORT 5001U
#define GW_PACKET_SIZE 64U
#define GW_DISCOVER 1U
#define GW_INFO 2U
#define GW_SUBSCRIBE 3U
#define GW_SUBSCRIBED 4U
#define GW_CAN_EVENT 5U
#define GW_UNSUBSCRIBE 6U
#define GW_CAN_GET 7U
#define GW_CAN_SET 8U
#define GW_CAN_STATE 9U
static inline uint32_t gw_get32(const uint8_t* p) {
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static inline void gw_put32(uint8_t* p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t) (v >> (i * 8));
}
static inline void gw_packet(uint8_t* p, uint8_t type, uint32_t request, const uint8_t* id) {
    memset(p, 0, GW_PACKET_SIZE);
    memcpy(p, "EMM1", 4);
    p[4] = 1;
    p[5] = type;
    p[6] = GW_PACKET_SIZE;
    gw_put32(p + 8, request);
    if (id)
        memcpy(p + 12, id, 16);
}
static inline bool gw_packet_valid(const uint8_t* p, size_t n) {
    return n == GW_PACKET_SIZE && memcmp(p, "EMM1", 4) == 0 && p[4] == 1 && p[5] >= GW_DISCOVER &&
           p[5] <= GW_CAN_STATE && p[6] == GW_PACKET_SIZE && p[7] == 0;
}
#endif
