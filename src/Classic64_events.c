#include "Classic64_events.h"

#include "Classic64.h"
#include "Classic64_settings.h"

#include "../ClassiCube/src/Entity.h"
#include "../ClassiCube/src/Graphics.h"
#include "../ClassiCube/src/Model.h"
#include "../ClassiCube/src/Protocol.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void OnChatMessage(void* obj, const cc_string* msg, int msgType)
{
	// if message starts with this, run as a mario64 client command
	cc_string magicCmd = String_FromConst("&0!mario64");

	if (msgType == 0 && String_CaselessStarts(msg, &magicCmd))
	{
		// i probably should not be using the unsafe functions?...
		cc_string args[64];

		int argsCount = String_UNSAFE_Split(msg, ' ', args, 64);
		for (int i=0; i<argsCount-1; i++)
			args[i] = args[i+1];

		OnMarioClientCmd(args, argsCount-1);
	}
}

void OnHacksChanged(void* obj)
{
	if (!marioInstances[ENTITIES_SELF_ID]) return;

	struct LocalPlayer* p = Entities.CurPlayer;
	struct HacksComp* hax = &p->Hacks;

	if (!String_ContainsConst(&hax->HacksFlags, "+mario64") && !hax->CanFly)
	{
		cc_string humanModelName = String_FromReadonly(Models.Human->name);
		Entity_SetModel(&p->Base, &humanModelName);
	}
}

void OnKeyDown(void* obj, int key, cc_bool repeating)
{
	if (repeating || isGuiOpen()) return;

	if (marioInstances[ENTITIES_SELF_ID])
	{
		if (strcmp(pluginOptions[PLUGINOPTION_KEY_CROUCH].value.str.current, keyNames[key]) == 0)
			marioInstances[ENTITIES_SELF_ID]->input.buttonZ = true;
		else if (strcmp(pluginOptions[PLUGINOPTION_KEY_ATTACK].value.str.current, keyNames[key]) == 0)
			marioInstances[ENTITIES_SELF_ID]->input.buttonB = true;
	}
}

void OnKeyUp(void* obj, int key)
{
	if (isGuiOpen()) return;

	if (marioInstances[ENTITIES_SELF_ID])
	{
		if (strcmp(pluginOptions[PLUGINOPTION_KEY_CROUCH].value.str.current, keyNames[key]) == 0)
			marioInstances[ENTITIES_SELF_ID]->input.buttonZ = false;
		else if (strcmp(pluginOptions[PLUGINOPTION_KEY_ATTACK].value.str.current, keyNames[key]) == 0)
			marioInstances[ENTITIES_SELF_ID]->input.buttonB = false;
	}
}

void OnContextLost(void* obj)
{
	Gfx_DeleteTexture(&marioTextureID);
	for (int i=0; i<256; i++)
	{
		if (marioInstances[i])
		{
			struct MarioInstance* obj = marioInstances[i];
			Gfx_DeleteDynamicVb(&obj->vertexID);
			Gfx_DeleteDynamicVb(&obj->texturedVertexID);
#ifdef CLASSIC64_DEBUG
			Gfx_DeleteDynamicVb(&obj->debuggerVertexID);
#endif
		}
	}
}

void OnContextRecreated(void* obj)
{
	marioTextureID = Gfx_CreateTexture(&marioBitmap, 0, false);
	for (int i=0; i<256; i++)
	{
		if (marioInstances[i])
		{
			struct MarioInstance* obj = marioInstances[i];
			obj->vertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, 4 * SM64_GEO_MAX_TRIANGLES);
			obj->texturedVertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, 4 * SM64_GEO_MAX_TRIANGLES);
#ifdef CLASSIC64_DEBUG
			obj->debuggerVertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, DEBUGGER_MAX_VERTICES);
#endif
		}
	}
}
