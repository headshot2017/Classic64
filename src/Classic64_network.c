#include "Classic64_network.h"
#include "Classic64_events.h"
#include "Classic64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// from Stream.c
cc_uint32 Stream_GetU32_BE(const cc_uint8* data) {
	return (cc_uint32)(
		((cc_uint32)data[0] << 24) | ((cc_uint32)data[1] << 16) |
		((cc_uint32)data[2] << 8)  |  (cc_uint32)data[3]);
}

void Stream_SetU32_BE(cc_uint8* data, cc_uint32 value) {
	data[0] = (cc_uint8)(value >> 24); data[1] = (cc_uint8)(value >> 16);
	data[2] = (cc_uint8)(value >> 8 ); data[3] = (cc_uint8)(value);
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
