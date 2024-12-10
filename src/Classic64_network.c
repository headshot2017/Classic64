#include "Classic64_network.h"
#include "Classic64_events.h"
#include "Classic64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ClassiCube/src/Protocol.h"

// from Stream.c
cc_uint16 Stream_GetU16_BE(const cc_uint8* data) {
	return (cc_uint16)((data[0] << 8) | data[1]);
}

cc_uint32 Stream_GetU32_BE(const cc_uint8* data) {
	return (cc_uint32)(
		((cc_uint32)data[0] << 24) | ((cc_uint32)data[1] << 16) |
		((cc_uint32)data[2] << 8)  |  (cc_uint32)data[3]);
}

void Stream_SetU16_BE(cc_uint8* data, cc_uint16 value) {
	data[0] = (cc_uint8)(value >> 8 ); data[1] = (cc_uint8)(value      );
}

void Stream_SetU32_BE(cc_uint8* data, cc_uint32 value) {
	data[0] = (cc_uint8)(value >> 24); data[1] = (cc_uint8)(value >> 16);
	data[2] = (cc_uint8)(value >> 8 ); data[3] = (cc_uint8)(value);
}


void sendMarioColors()
{
	if (!serverHasPlugin) return;

	cc_uint8 data[64] = {0};
	data[0] = OPCODE_MARIO_SET_COLORS;
	data[1] = pluginOptions[PLUGINOPTION_CUSTOM_COLORS].value.on ? 1 : 0;
	if (pluginOptions[PLUGINOPTION_CUSTOM_COLORS].value.on) // append custom colors
	{
		for (int i=0; i<6; i++)
		{
			data[2 + (i*3) + 0] = pluginOptions[PLUGINOPTION_COLOR_OVERALLS + i].value.col.r;
			data[2 + (i*3) + 1] = pluginOptions[PLUGINOPTION_COLOR_OVERALLS + i].value.col.g;
			data[2 + (i*3) + 2] = pluginOptions[PLUGINOPTION_COLOR_OVERALLS + i].value.col.b;
		}
	}
	CPE_SendPluginMessage(64, data);
}

void sendMarioTick()
{
	if (!serverHasPlugin || !marioInstances[ENTITIES_SELF_ID]) return;
	struct MarioInstance *obj = marioInstances[ENTITIES_SELF_ID];

	cc_uint8 data[64] = {0};
	data[0] = OPCODE_MARIO_TICK;
	Stream_SetU16_BE(&data[1], obj->state.health);
	Stream_SetU32_BE(&data[3], obj->state.action);
	Stream_SetU32_BE(&data[7], obj->state.actionArg);
	Stream_SetU16_BE(&data[11], obj->state.rawFaceAngle);
	memcpy(&data[13], &obj->input, sizeof(struct SM64MarioInputs));

	CPE_SendPluginMessage(64, data);
}

void OnPluginMessage(void* obj, cc_uint8 channel, cc_uint8* data)
{
	if (channel != 64) return;

	switch(data[0])
	{
		case OPCODE_MARIO_HAS_PLUGIN:
			SendChat("&aServer is running Classic64-MCGalaxy plugin!", NULL, NULL, NULL, NULL);
			if (!serverHasPlugin)
			{
				serverHasPlugin = true;
				sendMarioColors();
			}
			break;

		case OPCODE_MARIO_TICK:
			{
				cc_uint8 who = data[1];
				if (!marioInstances[who]) return;

				cc_int16 health = Stream_GetU16_BE(&data[2]);
				cc_uint32 action = Stream_GetU32_BE(&data[4]);
				cc_uint32 actionArg = Stream_GetU32_BE(&data[8]);
				cc_int16 faceAngle = Stream_GetU16_BE(&data[12]);

				sm64_mario_set_health(marioInstances[who]->ID, health);
				if (marioInstances[who]->state.action != action) sm64_set_mario_action_arg(marioInstances[who]->ID, action, actionArg);
				sm64_set_mario_faceangle(marioInstances[who]->ID, faceAngle);
				memcpy(&marioInstances[who]->input, &data[14], sizeof(struct SM64MarioInputs));
			}
			break;

		case OPCODE_MARIO_SET_COLORS:
			{
				cc_uint8 who = data[1];
				cc_uint8 on = data[2];
				if (!marioUpdates[who])
				{
					marioUpdates[who] = (struct MarioUpdate*)malloc(sizeof(struct MarioUpdate));
					marioUpdates[who]->flags = MARIOUPDATE_FLAG_COLORS;
				}
				else if (!(marioUpdates[who]->flags & MARIOUPDATE_FLAG_COLORS))
					marioUpdates[who]->flags |= MARIOUPDATE_FLAG_COLORS;

				marioUpdates[who]->customColors = on;
				if (on)
				{
					struct RGBCol newColors[6];
					for (int i=0; i<6; i++)
					{
						newColors[i].r = data[3 + (i*3) + 0];
						newColors[i].g = data[3 + (i*3) + 1];
						newColors[i].b = data[3 + (i*3) + 2];
					}
					memcpy(&marioUpdates[who]->newColors, newColors, sizeof(struct RGBCol) * 6);
				}
			}
			break;

		case OPCODE_MARIO_SET_CAP:
			{
				cc_uint8 who = data[1];
				cc_uint32 capFlag = Stream_GetU32_BE(&data[2]);
				if (!marioUpdates[who])
				{
					marioUpdates[who] = (struct MarioUpdate*)malloc(sizeof(struct MarioUpdate));
					marioUpdates[who]->flags = MARIOUPDATE_FLAG_CAP;
				}
				else if (!(marioUpdates[who]->flags & MARIOUPDATE_FLAG_CAP))
					marioUpdates[who]->flags |= MARIOUPDATE_FLAG_CAP;

				marioUpdates[who]->cap = capFlag;
			}
			break;
	}
}
