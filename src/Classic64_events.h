#ifndef CLASSIC64_EVENTS_H
#define CLASSIC64_EVENTS_H

#include "ClassiCube/String.h"

void OnChatMessage(void* obj, const cc_string* msg, int msgType);
void OnContextLost(void* obj);
void OnContextRecreated(void* obj);

#endif
