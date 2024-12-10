#ifndef CLASSIC64_EVENTS_H
#define CLASSIC64_EVENTS_H

#include "../ClassiCube/src/String.h"

void OnChatMessage(void* obj, const cc_string* msg, int msgType);
void OnHacksChanged(void* obj);
void OnKeyDown(void* obj, int key, cc_bool repeating);
void OnKeyUp(void* obj, int key);
void OnContextLost(void* obj);
void OnContextRecreated(void* obj);
void OnPluginMessage(void* obj, cc_uint8 channel, cc_uint8* data);

#endif
