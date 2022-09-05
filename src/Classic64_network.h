#ifndef CLASSIC64_NETWORK_H
#define CLASSIC64_NETWORK_H

#include "ClassiCube/Core.h"

// from Stream.h
cc_uint32 Stream_GetU32_BE(const cc_uint8* data);
void Stream_SetU32_BE(cc_uint8* data, cc_uint32 value);

#endif