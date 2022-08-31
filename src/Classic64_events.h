#ifndef CLASSIC64_EVENTS_H
#define CLASSIC64_EVENTS_H

#include "ClassiCube/String.h"

void OnChatMessage(void* obj, const cc_string* msg, int msgType);
void OnKeyDown(void* obj, int key, cc_bool repeating);
void OnKeyUp(void* obj, int key);
void OnContextLost(void* obj);
void OnContextRecreated(void* obj);

#endif
