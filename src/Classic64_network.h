#ifndef CLASSIC64_NETWORK_H
#define CLASSIC64_NETWORK_H

#include "../ClassiCube/src/Core.h"

// from Stream.h
cc_uint16 Stream_GetU16_BE(const cc_uint8* data);
cc_uint32 Stream_GetU32_BE(const cc_uint8* data);
void Stream_SetU16_BE(cc_uint8* data, cc_uint16 value);
void Stream_SetU32_BE(cc_uint8* data, cc_uint32 value);

void sendMarioColors();
void sendMarioTick();

#endif
