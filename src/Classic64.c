#ifdef _WIN32
	#define _WIN32_WINNT 0x0600
	#include <windows.h>

	#define CC_API __declspec(dllimport)
	#define CC_VAR __declspec(dllimport)
	#define EXPORT __declspec(dllexport)
#else
	#define CC_API
	#define CC_VAR
	#define EXPORT __attribute__((visibility("default")))
#endif

#define MARIO_SCALE 128.f
#define IMARIO_SCALE 128
#define DEBUGGER_MAX_VERTICES 1024

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsm64.h"
#include "decomp/include/sm64.h"
#include "decomp/include/surface_terrains.h"
#include "decomp/include/seq_ids.h"

#include "ClassiCube/Bitmap.h"
#include "ClassiCube/Block.h"
#include "ClassiCube/Camera.h"
#include "ClassiCube/Chat.h"
#include "ClassiCube/Entity.h"
#include "ClassiCube/Event.h"
#include "ClassiCube/ExtMath.h"
#include "ClassiCube/Game.h"
#include "ClassiCube/Graphics.h"
#include "ClassiCube/Gui.h"
#include "ClassiCube/Input.h"
#include "ClassiCube/Lighting.h"
#include "ClassiCube/Model.h"
#include "ClassiCube/Protocol.h"
#include "ClassiCube/Server.h"
#include "ClassiCube/String.h"
#include "ClassiCube/Window.h"
#include "ClassiCube/World.h"

#define sign(x) ((x>0)?1:(x<0)?-1:0)

// shortcut to log messages to chat
static void SendChat(const char* format, const void* arg1, const void* arg2, const void* arg3) {
	cc_string msg; char msgBuffer[256];
	String_InitArray(msg, msgBuffer);

	String_Format3(&msg, format, arg1, arg2, arg3);
	Chat_Add(&msg);
}

BlockID World_SafeGetBlock(int x, int y, int z) {
	return World_Contains(x, y, z) ? World_GetBlock(x, y, z) : BLOCK_AIR;
}

// classicube symbols
static void LoadSymbolsFromGame(void);
static struct _ServerConnectionData* Server_;
static struct _BlockLists* Blocks_;
static struct _ModelsData* Models_;
static struct _GameData* Game_;
static struct _WorldData* World_;
static struct _EnvData* Env_;
static struct _EntitiesData* Entities_;
static struct _CameraData* Camera_;
static struct _GuiData* Gui_;
static struct _Lighting* Lighting_;
static struct _ChatEventsList* ChatEvents_;

// plugin settings
enum
{
	PLUGINOPTION_FIRST_USE,
	PLUGINOPTION_HURT,
	PLUGINOPTION_CAMERA,
	PLUGINOPTION_BGR,
#ifdef CLASSIC64_DEBUG
	PLUGINOPTION_SURFACE_DEBUGGER,
#endif
	PLUGINOPTIONS_MAX
};

struct PluginOption {
	cc_string name;
	cc_string usage;
	cc_string desc[3];
	int descLines;
	int value;
	bool hidden;
} pluginOptions[] = {
	{
		String_FromConst("first_use"),
		String_FromConst(""),
		{String_FromConst("")},
		1, 0, true
	},
	{
		String_FromConst("hurt"),
		String_FromConst("<on/off>"),
		{String_FromConst("&eSet whether Mario can get hurt.")},
		1, 0, false
	},
	{
		String_FromConst("camera"),
		String_FromConst("<on/off>"),
		{String_FromConst("&eRotate the camera automatically behind Mario.")},
		1, 0, false
	},
	{
		String_FromConst("bgr"),
		String_FromConst("<on/off>"),
		{
			String_FromConst("&eSwap Mario's RGB colors with BGR."),
			String_FromConst("&eThis is off by default."),
			String_FromConst("&eIf Mario's colors appear inverted, change this setting."),
		},
		3, 0, false
	},
#ifdef CLASSIC64_DEBUG
	{
		String_FromConst("surface_debugger"),
		String_FromConst("<on/off>"),
		{String_FromConst("Toggle the SM64 surface debugger")},
		1, 0, false
	}
#endif
};

void saveSettings()
{
	FILE *f = fopen("Classic64.cfg", "w");
	for (int i=0; i<PLUGINOPTIONS_MAX; i++)
		fprintf(f, "%s: %d\n", pluginOptions[i].name.buffer, pluginOptions[i].value);
	fclose(f);
}

void loadSettings()
{
	FILE *f = fopen("Classic64.cfg", "r");
	if (!f) return;
	char buf[256];
	while (fgets(buf, sizeof(buf), f))
	{
		char *name = strtok(buf, ": ");
		char *value = strtok(NULL, ": ");
		if (value) value[strcspn(value, "\n")] = 0;

		for (int i=0; i<PLUGINOPTIONS_MAX; i++)
		{
			if (strcmp(name, pluginOptions[i].name.buffer) == 0)
			{
				pluginOptions[i].value = atoi(value);
				continue;
			}
		}
	}
}

// mario variables
uint8_t *marioTextureUint8;

struct MarioInstance // represents a Mario object in the plugin
{
	int32_t ID;
	uint32_t surfaces[9*3];
	Vec3 lastPos;
	Vec3 currPos;

	struct SM64MarioInputs input;
	struct SM64MarioState state;
	struct SM64MarioGeometryBuffers geometry;

	struct VertexTextured vertices[4 * SM64_GEO_MAX_TRIANGLES];
	struct VertexTextured texturedVertices[4 * SM64_GEO_MAX_TRIANGLES];
	GfxResourceID vertexID;
	GfxResourceID texturedVertexID;
	uint16_t numTexturedTriangles;
#ifdef CLASSIC64_DEBUG
	struct VertexTextured debuggerVertices[DEBUGGER_MAX_VERTICES];
	uint16_t numDebuggerTriangles;
	GfxResourceID debuggerVertexID;
#endif
} *marioInstances[256];

int ticksBeforeSpawn;
bool inited;
bool allowTick; // false when loading world
float marioInterpTicks;

// mario's model (the hard part)
static struct Bitmap marioBitmap = {0, 1024, SM64_TEXTURE_HEIGHT};
GfxResourceID marioTextureID;

static void marioModel_Draw(struct Entity* p)
{
	for (int i=0; i<256; i++)
	{
		if (Entities_->List[i] && Entities_->List[i] == p && marioInstances[i])
		{
			struct MarioInstance *obj = marioInstances[i];
			Gfx_BindTexture(marioTextureID);

			// draw colored triangles first (can't use VERTEX_FORMAT_COLOURED with VertexColoured structs because that crashes randomly)
			Gfx_BindVb(obj->vertexID);
			Gfx_DrawVb_IndexedTris(4 * obj->geometry.numTrianglesUsed);

			// draw textured triangles next (eyes, mustache, etc)
			Gfx_BindVb(obj->texturedVertexID);
			Gfx_DrawVb_IndexedTris(4 * obj->numTexturedTriangles);

#ifdef CLASSIC64_DEBUG
			if (pluginOptions[PLUGINOPTION_SURFACE_DEBUGGER].value)
			{
				Gfx_BindVb(obj->debuggerVertexID);
				Gfx_DrawVb_IndexedTris(obj->numDebuggerTriangles);
			}
#endif
		}
	}
}

static void marioModel_GetTransform(struct Entity* entity, Vec3 pos, struct Matrix* m)
{
	struct Matrix tmp;
	Matrix_Scale(m, 1,1,1);
	Matrix_Translate(&tmp, pos.X, pos.Y, pos.Z);
	Matrix_MulBy(m, &tmp);
}

static struct ModelTex unused_tex = { "char.png" }; // without this, the game crashes in first person view with nothing held in hand
static void marioModel_DrawArm(struct Entity* entity) {}
static void marioModel_MakeParts(void) {}
static float marioModel_GetNameY(struct Entity* e) { return 1+0.25f; }
static float marioModel_GetEyeY(struct Entity* e)  { return 1; }
static void marioModel_GetSize(struct Entity* e) {e->Size = (Vec3) { 0.5f, 1, 0.5f };}
static void marioModel_GetBounds(struct Entity* e) {AABB_Make(&e->ModelAABB, &e->Position, &e->Size);}
static struct Model mario_model = { "mario64", NULL, &unused_tex,
	marioModel_MakeParts, marioModel_Draw,
	marioModel_GetNameY,  marioModel_GetEyeY,
	marioModel_GetSize,   marioModel_GetBounds
};

static struct Model* marioModel_GetInstance(void) {
	Model_Init(&mario_model);
	mario_model.GetTransform = marioModel_GetTransform;
	mario_model.DrawArm = marioModel_DrawArm;
	mario_model.shadowScale = 0.85f;
	mario_model.usesHumanSkin = false;
	mario_model.bobbing = false;
	return &mario_model;
}

void OnChatMessage(void* obj, const cc_string* msg, int msgType)
{
	
}

// chat command
static void OnMarioClientCmd(const cc_string* args, int argsCount)
{
	cc_string empty = String_FromConst("");
	cc_string on = String_FromConst("on");
	cc_string off = String_FromConst("off");
	if (!argsCount || String_Compare(&args[0], &empty) == 0)
	{
		SendChat("&cSee /client help mario64 for available options", NULL, NULL, NULL);
		return;
	}

	cc_string options[] = {
		String_FromConst("music"),
		String_FromConst("cap"),
		String_FromConst("kill"),
		String_FromConst("force"),
		String_FromConst("settings"),
	};

	if (String_Compare(&args[0], &options[0]) == 0) // music
	{
		if (argsCount < 2)
		{
			SendChat("&a/client mario64 music <id 0-33>", NULL, NULL, NULL);
			SendChat("&ePlay music from Super Mario 64.", NULL, NULL, NULL);
			SendChat("&eTo stop music, use music ID 0.", NULL, NULL, NULL);
			return;
		}

		cc_uint8 music;
		if (!Convert_ParseUInt8(&args[1], &music) || music >= 0x22)
		{
			SendChat("&cInvalid music ID", NULL, NULL, NULL);
			return;
		}

		for (int i=0; i<0x22; i++) sm64_stop_background_music(i);
		if (music != 0) sm64_play_music(0, ((0 << 8) | music), 0);
		return;
	}
	else if (String_Compare(&args[0], &options[1]) == 0) // set mario cap setting
	{
		if (argsCount < 2)
		{
			SendChat("&a/client mario64 cap <option>", NULL, NULL, NULL);
			SendChat("&eChange Mario's cap.", NULL, NULL, NULL);
			SendChat("&eOptions: off, on, wing, metal", NULL, NULL, NULL);
			return;
		}

		if (!marioInstances[ENTITIES_SELF_ID])
		{
			SendChat("&cSwitch to Mario first", NULL, NULL, NULL);
			return;
		}

		struct {
			cc_string name;
			uint32_t capFlag;
		} caps[] = {
			{String_FromConst("off"), 0},
			{String_FromConst("on"), MARIO_NORMAL_CAP},
			{String_FromConst("wing"), MARIO_WING_CAP},
			{String_FromConst("metal"), MARIO_METAL_CAP}
		};

		for (int i=0; i<4; i++)
		{
			if (String_Compare(&args[1], &caps[i].name) == 0)
			{
				sm64_set_mario_state(marioInstances[ENTITIES_SELF_ID]->ID, 0);
				sm64_mario_interact_cap(marioInstances[ENTITIES_SELF_ID]->ID, caps[i].capFlag, 65535, 0);
				return;
			}
		}
		SendChat("&cUnknown cap \"%s\"", &args[1], NULL, NULL);
		return;
	}
	else if (String_Compare(&args[0], &options[2]) == 0) // kill mario
	{
		if (!marioInstances[ENTITIES_SELF_ID])
		{
			SendChat("&cSwitch to Mario first", NULL, NULL, NULL);
			return;
		}
		sm64_mario_kill(marioInstances[ENTITIES_SELF_ID]->ID);
		return;
	}
	else if (String_Compare(&args[0], &options[3]) == 0) // force-switch to mario
	{
		if (String_ContainsConst(&Server_->MOTD, "-hax") || String_ContainsConst(&Server_->MOTD, "-fly"))
		{
			SendChat("&cHacks are disabled, cannot switch to Mario", NULL, NULL, NULL);
			return;
		}
		cc_string model = String_FromConst( (strcmp(Entities_->List[ENTITIES_SELF_ID]->Model->name, "mario64") == 0) ? "human" : "mario64" );
		Entity_SetModel(Entities_->List[ENTITIES_SELF_ID], &model);
		return;
	}
	else if (String_Compare(&args[0], &options[4]) == 0) // change plugin settings
	{
		if (argsCount < 2)
		{
			SendChat("&a/client mario64 settings <option>", NULL, NULL, NULL);
			SendChat("&eChange plugin settings.", NULL, NULL, NULL);

			char msgBuffer[256];
			sprintf(msgBuffer, "&eAvailable settings: ");
			for (int i=0; i<PLUGINOPTIONS_MAX; i++)
			{
				if (pluginOptions[i].hidden) continue;
				strcat(msgBuffer, pluginOptions[i].name.buffer);
				if (i != PLUGINOPTIONS_MAX-1) strcat(msgBuffer, ", ");
			}
			SendChat(msgBuffer, NULL, NULL, NULL);
			return;
		}

		for (int i=0; i<PLUGINOPTIONS_MAX; i++)
		{
			if (pluginOptions[i].hidden) continue;
			if (String_Compare(&args[1], &pluginOptions[i].name) == 0)
			{
				if (argsCount < 3)
				{
					SendChat("&a/client mario64 settings %s %s", &pluginOptions[i].name, &pluginOptions[i].usage, NULL);
					for (int j=0; j<pluginOptions[i].descLines; j++) SendChat("%s", &pluginOptions[i].desc[j], NULL, NULL);

					cc_string value; char valueBuf[32];
					String_InitArray(value, valueBuf);
					String_AppendInt(&value, pluginOptions[i].value);

					SendChat("Current value: %s", &value, NULL, NULL);
					return;
				}

				int value;
				if (String_Compare(&args[2], &on) == 0) value = 1;
				else if (String_Compare(&args[2], &off) == 0) value = 0;
				else if (!Convert_ParseInt(&args[2], &value))
				{
					SendChat("&cUnknown value \"%s\". Please enter 'on', 'off' or a number.", &args[2], NULL, NULL);
					return;
				}
				pluginOptions[i].value = value;
				SendChat("&a%s: %s", &pluginOptions[i].name, &args[2], NULL);
				saveSettings();
				return;
			}
		}

		SendChat("&cUnknown setting \"%s\"", &args[1], NULL, NULL);
		return;
	}

	SendChat("&cUnknown option \"%s\"", &args[0], NULL, NULL);
}

static struct ChatCommand MarioClientCmd = {
	"Mario64", OnMarioClientCmd, false,
	{
		"&a/client mario64 <option>",
		"&eAvailable options:",
		"&emusic, cap, kill, force, settings",
	}
};

// mario functions
void deleteBlocks(uint32_t *arrayTarget) // specify arrayTarget because the blocks need to be created before mario can be spawned
{
	for (int j=0; j<9*3; j++)
	{
		if (arrayTarget[j] == -1) continue;
		sm64_surface_object_delete(arrayTarget[j]);
		arrayTarget[j] = -1;
	}
}

void deleteMario(int i)
{
	if (!marioInstances[i]) return;
	struct MarioInstance *obj = marioInstances[i];
	printf("delete mario %d\n", i);

	deleteBlocks(obj->surfaces);
	sm64_mario_delete(obj->ID);

	free(obj->geometry.position);
	free(obj->geometry.color);
	free(obj->geometry.normal);
	free(obj->geometry.uv);
	//Gfx_DeleteDynamicVb(obj->vertexID);
	//Gfx_DeleteDynamicVb(obj->texturedVertexID);

	free(marioInstances[i]);
	marioInstances[i] = 0;
}

void loadNewBlocks(int i, int x, int y, int z, uint32_t *arrayTarget) // specify arrayTarget because the blocks need to be created before mario can be spawned
{
	deleteBlocks(arrayTarget);

	int j = 0;
	for (int zadd=-1; zadd<=1; zadd++)
	{
		for (int xadd=-1; xadd<=1; xadd++)
		{
			if (x+xadd < 0 || x+xadd >= World_->Width || z+zadd < 0 || z+zadd >= World_->Length) continue;
			BlockID block = 0;

			// get block at mario's head if it exists
			if (y+2 < World_->Height)
			{
				block = World_SafeGetBlock(x+xadd, y+2, z+zadd);
				if (block != 0 && (Blocks_->Collide[block] == COLLIDE_SOLID || Blocks_->Collide[block] == COLLIDE_ICE || Blocks_->Collide[block] == COLLIDE_SLIPPERY_ICE))
				{
					struct SM64SurfaceObject obj;
					memset(&obj.transform, 0, sizeof(struct SM64ObjectTransform));
					obj.transform.position[0] = (x+xadd) * IMARIO_SCALE;
					obj.transform.position[1] = (y+2) * IMARIO_SCALE;
					obj.transform.position[2] = (z+zadd) * IMARIO_SCALE;
					obj.surfaceCount = 2;
					obj.surfaces = (struct SM64Surface*)malloc(sizeof(struct SM64Surface) * obj.surfaceCount);

					for (int ind=0; ind<obj.surfaceCount; ind++)
					{
						obj.surfaces[ind].type = (Blocks_->Collide[block] == COLLIDE_ICE || Blocks_->Collide[block] == COLLIDE_SLIPPERY_ICE) ? SURFACE_ICE : SURFACE_DEFAULT;
						obj.surfaces[ind].force = 0;
						if (obj.surfaces[ind].type == SURFACE_ICE) obj.surfaces[ind].terrain = TERRAIN_SLIDE;
						else switch(Blocks_->StepSounds[block])
						{
							case SOUND_SNOW:
								obj.surfaces[ind].terrain = TERRAIN_SNOW;
								break;
							case SOUND_GRASS:
							case SOUND_GRAVEL:
							case SOUND_CLOTH:
							case SOUND_METAL:
								obj.surfaces[ind].terrain = TERRAIN_GRASS;
								break;
							case SOUND_SAND:
								obj.surfaces[ind].terrain = TERRAIN_SAND;
								break;
							case SOUND_WOOD:
								obj.surfaces[ind].terrain = TERRAIN_SPOOKY;
								break;
							default:
								obj.surfaces[ind].terrain = TERRAIN_STONE;
								break;
						}
					}

					// block ground face
					/*
					obj.surfaces[0].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[0].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[0].vertices[0][2] = IMARIO_SCALE;
					obj.surfaces[0].vertices[1][0] = 0;				obj.surfaces[0].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[0].vertices[1][2] = 0;
					obj.surfaces[0].vertices[2][0] = 0;				obj.surfaces[0].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[0].vertices[2][2] = IMARIO_SCALE;

					obj.surfaces[1].vertices[0][0] = 0; 			obj.surfaces[1].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[1].vertices[0][2] = 0;
					obj.surfaces[1].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[1].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[1].vertices[1][2] = IMARIO_SCALE;
					obj.surfaces[1].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[1].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[1].vertices[2][2] = 0;

					// left (Z+)
					obj.surfaces[2].vertices[0][0] = 0;				obj.surfaces[2].vertices[0][1] = 0;				obj.surfaces[2].vertices[0][2] = IMARIO_SCALE;
					obj.surfaces[2].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[2].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[2].vertices[1][2] = IMARIO_SCALE;
					obj.surfaces[2].vertices[2][0] = 0;				obj.surfaces[2].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[2].vertices[2][2] = IMARIO_SCALE;

					obj.surfaces[3].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[3].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[3].vertices[0][2] = IMARIO_SCALE;
					obj.surfaces[3].vertices[1][0] = 0;				obj.surfaces[3].vertices[1][1] = 0;				obj.surfaces[3].vertices[1][2] = IMARIO_SCALE;
					obj.surfaces[3].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[3].vertices[2][1] = 0;				obj.surfaces[3].vertices[2][2] = IMARIO_SCALE;

					// right (Z-)
					obj.surfaces[4].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[4].vertices[0][1] = 0;				obj.surfaces[4].vertices[0][2] = 0;
					obj.surfaces[4].vertices[1][0] = 0;				obj.surfaces[4].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[4].vertices[1][2] = 0;
					obj.surfaces[4].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[4].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[4].vertices[2][2] = 0;

					obj.surfaces[5].vertices[0][0] = 0;				obj.surfaces[5].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[5].vertices[0][2] = 0;
					obj.surfaces[5].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[5].vertices[1][1] = 0;				obj.surfaces[5].vertices[1][2] = 0;
					obj.surfaces[5].vertices[2][0] = 0;				obj.surfaces[5].vertices[2][1] = 0;				obj.surfaces[5].vertices[2][2] = 0;

					// back (X+)
					obj.surfaces[6].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[6].vertices[0][1] = 0;				obj.surfaces[6].vertices[0][2] = 0;
					obj.surfaces[6].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[6].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[6].vertices[1][2] = 0;
					obj.surfaces[6].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[6].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[6].vertices[2][2] = IMARIO_SCALE;

					obj.surfaces[7].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[7].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[7].vertices[0][2] = IMARIO_SCALE;
					obj.surfaces[7].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[7].vertices[1][1] = 0;				obj.surfaces[7].vertices[1][2] = IMARIO_SCALE;
					obj.surfaces[7].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[7].vertices[2][1] = 0;				obj.surfaces[7].vertices[2][2] = 0;

					// front (X-)
					obj.surfaces[8].vertices[0][0] = 0;				obj.surfaces[8].vertices[0][1] = 0;				obj.surfaces[8].vertices[0][2] = IMARIO_SCALE;
					obj.surfaces[8].vertices[1][0] = 0;				obj.surfaces[8].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[8].vertices[1][2] = IMARIO_SCALE;
					obj.surfaces[8].vertices[2][0] = 0;				obj.surfaces[8].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[8].vertices[2][2] = 0;

					obj.surfaces[9].vertices[0][0] = 0;				obj.surfaces[9].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[9].vertices[0][2] = 0;
					obj.surfaces[9].vertices[1][0] = 0;				obj.surfaces[9].vertices[1][1] = 0;				obj.surfaces[9].vertices[1][2] = 0;
					obj.surfaces[9].vertices[2][0] = 0;				obj.surfaces[9].vertices[2][1] = 0;				obj.surfaces[9].vertices[2][2] = IMARIO_SCALE;*/

					// block bottom face
					obj.surfaces[0].vertices[0][0] = 0;				obj.surfaces[0].vertices[0][1] = 0;				obj.surfaces[0].vertices[0][2] = IMARIO_SCALE;
					obj.surfaces[0].vertices[1][0] = 0;				obj.surfaces[0].vertices[1][1] = 0;				obj.surfaces[0].vertices[1][2] = 0;
					obj.surfaces[0].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[0].vertices[2][1] = 0;				obj.surfaces[0].vertices[2][2] = IMARIO_SCALE;

					obj.surfaces[1].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[1].vertices[0][1] = 0;				obj.surfaces[1].vertices[0][2] = 0;
					obj.surfaces[1].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[1].vertices[1][1] = 0;				obj.surfaces[1].vertices[1][2] = IMARIO_SCALE;
					obj.surfaces[1].vertices[2][0] = 0;				obj.surfaces[1].vertices[2][1] = 0;				obj.surfaces[1].vertices[2][2] = 0;

					arrayTarget[j++] = sm64_surface_object_create(&obj);
					free(obj.surfaces);
				}
			}

			// get block at floor
			int yadd = 1;
			do
			{
				yadd--;
				if (y+yadd < 0) break;
				block = World_SafeGetBlock(x+xadd, y+yadd, z+zadd);
			} while (block == 0 || (Blocks_->Collide[block] != COLLIDE_SOLID && Blocks_->Collide[block] != COLLIDE_ICE && Blocks_->Collide[block] != COLLIDE_SLIPPERY_ICE));

			struct SM64SurfaceObject obj;
			memset(&obj.transform, 0, sizeof(struct SM64ObjectTransform));
			obj.transform.position[0] = (x+xadd) * IMARIO_SCALE;
			obj.transform.position[1] = (y+yadd) * IMARIO_SCALE;
			obj.transform.position[2] = (z+zadd) * IMARIO_SCALE;
			obj.surfaceCount = 6*2;
			obj.surfaces = (struct SM64Surface*)malloc(sizeof(struct SM64Surface) * obj.surfaceCount);

			for (int ind=0; ind<obj.surfaceCount; ind++)
			{
				obj.surfaces[ind].type = (Blocks_->Collide[block] == COLLIDE_ICE || Blocks_->Collide[block] == COLLIDE_SLIPPERY_ICE) ? SURFACE_ICE : SURFACE_DEFAULT;
				obj.surfaces[ind].force = 0;
				if (obj.surfaces[ind].type == SURFACE_ICE) obj.surfaces[ind].terrain = TERRAIN_SLIDE;
				else switch(Blocks_->StepSounds[block])
				{
					case SOUND_SNOW:
						obj.surfaces[ind].terrain = TERRAIN_SNOW;
						break;
					case SOUND_GRASS:
					case SOUND_GRAVEL:
					case SOUND_CLOTH:
					case SOUND_METAL:
						obj.surfaces[ind].terrain = TERRAIN_GRASS;
						break;
					case SOUND_SAND:
						obj.surfaces[ind].terrain = TERRAIN_SAND;
						break;
					case SOUND_WOOD:
						obj.surfaces[ind].terrain = TERRAIN_SPOOKY;
						break;
					default:
						obj.surfaces[ind].terrain = TERRAIN_STONE;
						break;
				}
			}

			// block ground face
			obj.surfaces[0].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[0].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[0].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[0].vertices[1][0] = 0;				obj.surfaces[0].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[0].vertices[1][2] = 0;
			obj.surfaces[0].vertices[2][0] = 0;				obj.surfaces[0].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[0].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[1].vertices[0][0] = 0; 			obj.surfaces[1].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[1].vertices[0][2] = 0;
			obj.surfaces[1].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[1].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[1].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[1].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[1].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[1].vertices[2][2] = 0;

			// left (Z+)
			obj.surfaces[2].vertices[0][0] = 0;				obj.surfaces[2].vertices[0][1] = 0;				obj.surfaces[2].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[2].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[2].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[2].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[2].vertices[2][0] = 0;				obj.surfaces[2].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[2].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[3].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[3].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[3].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[3].vertices[1][0] = 0;				obj.surfaces[3].vertices[1][1] = 0;				obj.surfaces[3].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[3].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[3].vertices[2][1] = 0;				obj.surfaces[3].vertices[2][2] = IMARIO_SCALE;

			// right (Z-)
			obj.surfaces[4].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[4].vertices[0][1] = 0;				obj.surfaces[4].vertices[0][2] = 0;
			obj.surfaces[4].vertices[1][0] = 0;				obj.surfaces[4].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[4].vertices[1][2] = 0;
			obj.surfaces[4].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[4].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[4].vertices[2][2] = 0;

			obj.surfaces[5].vertices[0][0] = 0;				obj.surfaces[5].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[5].vertices[0][2] = 0;
			obj.surfaces[5].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[5].vertices[1][1] = 0;				obj.surfaces[5].vertices[1][2] = 0;
			obj.surfaces[5].vertices[2][0] = 0;				obj.surfaces[5].vertices[2][1] = 0;				obj.surfaces[5].vertices[2][2] = 0;

			// back (X+)
			obj.surfaces[6].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[6].vertices[0][1] = 0;				obj.surfaces[6].vertices[0][2] = 0;
			obj.surfaces[6].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[6].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[6].vertices[1][2] = 0;
			obj.surfaces[6].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[6].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[6].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[7].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[7].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[7].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[7].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[7].vertices[1][1] = 0;				obj.surfaces[7].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[7].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[7].vertices[2][1] = 0;				obj.surfaces[7].vertices[2][2] = 0;

			// front (X-)
			obj.surfaces[8].vertices[0][0] = 0;				obj.surfaces[8].vertices[0][1] = 0;				obj.surfaces[8].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[8].vertices[1][0] = 0;				obj.surfaces[8].vertices[1][1] = IMARIO_SCALE;	obj.surfaces[8].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[8].vertices[2][0] = 0;				obj.surfaces[8].vertices[2][1] = IMARIO_SCALE;	obj.surfaces[8].vertices[2][2] = 0;

			obj.surfaces[9].vertices[0][0] = 0;				obj.surfaces[9].vertices[0][1] = IMARIO_SCALE;	obj.surfaces[9].vertices[0][2] = 0;
			obj.surfaces[9].vertices[1][0] = 0;				obj.surfaces[9].vertices[1][1] = 0;				obj.surfaces[9].vertices[1][2] = 0;
			obj.surfaces[9].vertices[2][0] = 0;				obj.surfaces[9].vertices[2][1] = 0;				obj.surfaces[9].vertices[2][2] = IMARIO_SCALE;

			// block bottom face
			obj.surfaces[10].vertices[0][0] = 0;			obj.surfaces[10].vertices[0][1] = 0;			obj.surfaces[10].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[10].vertices[1][0] = 0;			obj.surfaces[10].vertices[1][1] = 0;			obj.surfaces[10].vertices[1][2] = 0;
			obj.surfaces[10].vertices[2][0] = IMARIO_SCALE;	obj.surfaces[10].vertices[2][1] = 0;			obj.surfaces[10].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[11].vertices[0][0] = IMARIO_SCALE;	obj.surfaces[11].vertices[0][1] = 0;			obj.surfaces[11].vertices[0][2] = 0;
			obj.surfaces[11].vertices[1][0] = IMARIO_SCALE;	obj.surfaces[11].vertices[1][1] = 0;			obj.surfaces[11].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[11].vertices[2][0] = 0;			obj.surfaces[11].vertices[2][1] = 0;			obj.surfaces[11].vertices[2][2] = 0;

			arrayTarget[j++] = sm64_surface_object_create(&obj);
			free(obj.surfaces);
		}
	}

#ifdef CLASSIC64_DEBUG
	if (marioInstances[i])
	{
		struct MarioInstance* mario = marioInstances[i];
		uint32_t objCount = 0, vCount = 0;
		struct LoadedSurfaceObject* objs = sm64_get_all_surface_objects(&objCount);
		for (uint32_t i=0; i<objCount; i++)
		{
			for (j=0; j<objs[i].surfaceCount; j++)
			{
				float u[3] = {0.95, 0.96, 0.96};
				float v[3] = {0, 0, 1};
				for (int k=0; k<3; k++)
				{
					mario->debuggerVertices[vCount+k] = (struct VertexTextured) {
						(objs[i].transform->aPosX - (x*MARIO_SCALE) + objs[i].libSurfaces[j].vertices[k][0]) / MARIO_SCALE - 0.5,
						(objs[i].transform->aPosY - (y*MARIO_SCALE) + objs[i].libSurfaces[j].vertices[k][1]) / MARIO_SCALE,
						(objs[i].transform->aPosZ - (z*MARIO_SCALE) + objs[i].libSurfaces[j].vertices[k][2]) / MARIO_SCALE - 0.5,
						PackedCol_Make(0, 255, 0, 255), u[k], v[k]
					};
				}
				mario->debuggerVertices[vCount+3] = mario->debuggerVertices[vCount+2];
				vCount += 4;
			}
		}
		mario->numDebuggerTriangles = vCount;
		printf("%d %d\n", mario->numDebuggerTriangles, DEBUGGER_MAX_VERTICES);
		Gfx_SetDynamicVbData(mario->debuggerVertexID, &mario->debuggerVertices, DEBUGGER_MAX_VERTICES);
	}
#endif
}

void marioTick(struct ScheduledTask* task)
{
	if (!allowTick) return;

	for (int i=0; i<256; i++)
	{
		if (!Entities_->List[i]) continue;
		if (!marioInstances[i] && ticksBeforeSpawn <= 0 && strcmp(Entities_->List[i]->Model->name, "mario64") == 0)
		{
			// spawn mario. have some temporary variables for the surface IDs and mario ID
			uint32_t surfaces[9*3];
			memset(surfaces, -1, sizeof(surfaces));
			loadNewBlocks(i, Entities_->List[i]->Position.X, Entities_->List[i]->Position.Y, Entities_->List[i]->Position.Z, surfaces);
			int32_t ID = sm64_mario_create(Entities_->List[i]->Position.X*IMARIO_SCALE, Entities_->List[i]->Position.Y*IMARIO_SCALE, Entities_->List[i]->Position.Z*IMARIO_SCALE, 0,0,0,0);
			if (ID == -1)
			{
				SendChat("&cFailed to spawn Mario", NULL, NULL, NULL);
			}
			else
			{
				// mario was spawned, intialize the instance struct and put everything in there
				sm64_set_mario_faceangle(ID, (int16_t)((-Entities_->List[i]->Yaw + 180) / 180 * 32768.0f));
				marioInstances[i] = (struct MarioInstance*)malloc(sizeof(struct MarioInstance));
				marioInstances[i]->ID = ID;
				memcpy(&marioInstances[i]->surfaces, surfaces, sizeof(surfaces));
				marioInstances[i]->lastPos.X = marioInstances[i]->currPos.X = Entities_->List[i]->Position.X*IMARIO_SCALE;
				marioInstances[i]->lastPos.Y = marioInstances[i]->currPos.Y = Entities_->List[i]->Position.Y*IMARIO_SCALE;
				marioInstances[i]->lastPos.Z = marioInstances[i]->currPos.Z = Entities_->List[i]->Position.Z*IMARIO_SCALE;
				memset(&marioInstances[i]->input, 0, sizeof(struct SM64MarioInputs));
				marioInstances[i]->geometry.position = malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
				marioInstances[i]->geometry.color    = malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
				marioInstances[i]->geometry.normal   = malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
				marioInstances[i]->geometry.uv       = malloc( sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES );
				marioInstances[i]->vertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, 4 * SM64_GEO_MAX_TRIANGLES);
				marioInstances[i]->texturedVertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, 4 * SM64_GEO_MAX_TRIANGLES);
				marioInstances[i]->numTexturedTriangles = 0;
#ifdef CLASSIC64_DEBUG
				marioInstances[i]->debuggerVertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, DEBUGGER_MAX_VERTICES);
#endif
			}
		}
		else if (marioInstances[i])
		{
			struct MarioInstance *obj = marioInstances[i];
			if (strcmp(Entities_->List[i]->Model->name, "mario64"))
			{
				// no longer mario, delete from memory
				deleteMario(i);
				continue;
			}

			if (i != ENTITIES_SELF_ID) // not you
			{
				Vec3 newPos = {Entities_->List[i]->Position.X*IMARIO_SCALE, Entities_->List[i]->Position.Y*IMARIO_SCALE, Entities_->List[i]->Position.Z*IMARIO_SCALE};
				sm64_set_mario_position(obj->ID, newPos.X, newPos.Y, newPos.Z);
				obj->input.buttonA = (newPos.Y - obj->lastPos.Y > 0);

				bool moved = (newPos.Z - obj->lastPos.Z) && (newPos.X - obj->lastPos.X);
				if (moved)
				{
					float dir = atan2(newPos.Z - obj->lastPos.Z, newPos.X - obj->lastPos.X);
					obj->input.stickX = -Math_Cos(dir);
					obj->input.stickY = -Math_Sin(dir);
				}
				else
					obj->input.stickX = obj->input.stickY = 0;

				obj->lastPos = newPos;
			}
			else
			{
				marioInterpTicks = 0;
				obj->lastPos.X = obj->state.position[0]; obj->lastPos.Y = obj->state.position[1]; obj->lastPos.Z = obj->state.position[2];
			}

			if (!pluginOptions[PLUGINOPTION_HURT].value && sm64_mario_get_health(obj->ID) != 0xff) sm64_mario_set_health(obj->ID, 0x880);

			// water
			int yadd = 0;
			int waterY = -1024;
			while (obj->state.position[1]/MARIO_SCALE+yadd > waterY && Blocks_->IsLiquid[World_SafeGetBlock(obj->state.position[0]/MARIO_SCALE, obj->state.position[1]/MARIO_SCALE+yadd, obj->state.position[2]/MARIO_SCALE)])
			{
				waterY = obj->state.position[1]/MARIO_SCALE+yadd;
				yadd++;
			}
			sm64_set_mario_water_level(obj->ID, waterY*IMARIO_SCALE+IMARIO_SCALE);

			sm64_mario_tick(obj->ID, &obj->input, &obj->state, &obj->geometry);
			obj->currPos.X = obj->state.position[0];
			obj->currPos.Y = obj->state.position[1];
			obj->currPos.Z = obj->state.position[2];

			obj->numTexturedTriangles = 0;
			bool bgr = (pluginOptions[PLUGINOPTION_BGR].value);

			// fix for 1.3.2 until a future release comes out with the lighting system refactor
			PackedCol lightCol = (Lighting_) ? Lighting_->Color(obj->state.position[0]/MARIO_SCALE, obj->state.position[1]/MARIO_SCALE, obj->state.position[2]/MARIO_SCALE) : Env_->SunCol;

			for (int i=0; i<obj->geometry.numTrianglesUsed; i++)
			{
				bool hasTexture = (obj->geometry.uv[i*6+0] != 1 && obj->geometry.uv[i*6+1] != 1 && obj->geometry.uv[i*6+2] != 1 && obj->geometry.uv[i*6+3] != 1 && obj->geometry.uv[i*6+4] != 1 && obj->geometry.uv[i*6+5] != 1);
				bool wingCap = (obj->state.flags & MARIO_WING_CAP && i >= obj->geometry.numTrianglesUsed-8 && i <= obj->geometry.numTrianglesUsed-1); // do not draw white rectangles around wingcap

				obj->vertices[i*4+0] = (struct VertexTextured) {
					(obj->geometry.position[i*9+0] - obj->state.position[0]) / MARIO_SCALE,
					(obj->geometry.position[i*9+1] - obj->state.position[1]) / MARIO_SCALE,
					(obj->geometry.position[i*9+2] - obj->state.position[2]) / MARIO_SCALE,
					PackedCol_Tint(PackedCol_Make(obj->geometry.color[i*9 + (bgr?2:0)]*255, obj->geometry.color[i*9+1]*255, obj->geometry.color[i*9 + (bgr?0:2)]*255, 255), lightCol),
					(wingCap) ? 0 : 0.95, (wingCap) ? 0.95 : 0
				};

				obj->vertices[i*4+1] = (struct VertexTextured) {
					(obj->geometry.position[i*9+3] - obj->state.position[0]) / MARIO_SCALE,
					(obj->geometry.position[i*9+4] - obj->state.position[1]) / MARIO_SCALE,
					(obj->geometry.position[i*9+5] - obj->state.position[2]) / MARIO_SCALE,
					PackedCol_Tint(PackedCol_Make(obj->geometry.color[i*9 + (bgr?5:3)]*255, obj->geometry.color[i*9+4]*255, obj->geometry.color[i*9 + (bgr?3:5)]*255, 255), lightCol),
					(wingCap) ? 0 : 0.96, (wingCap) ? 0.96 : 0
				};

				obj->vertices[i*4+2] = (struct VertexTextured) {
					(obj->geometry.position[i*9+6] - obj->state.position[0]) / MARIO_SCALE,
					(obj->geometry.position[i*9+7] - obj->state.position[1]) / MARIO_SCALE,
					(obj->geometry.position[i*9+8] - obj->state.position[2]) / MARIO_SCALE,
					PackedCol_Tint(PackedCol_Make(obj->geometry.color[i*9 + (bgr?8:6)]*255, obj->geometry.color[i*9+7]*255, obj->geometry.color[i*9 + (bgr?6:8)]*255, 255), lightCol),
					(wingCap) ? 0.01 : 0.96, (wingCap) ? 0.96 : 1
				};

				obj->vertices[i*4+3] = obj->vertices[i*4+2]; // WHY IS IT A QUAD?!?!?!?!? why does classicube use quads!??!?!??

				if (hasTexture)
				{
					for (int j=0; j<4; j++)
					{
						obj->texturedVertices[obj->numTexturedTriangles*4+j] = (struct VertexTextured) {
							obj->vertices[i*4+j].X, obj->vertices[i*4+j].Y, obj->vertices[i*4+j].Z,
							PackedCol_Tint(PACKEDCOL_WHITE, lightCol),
							obj->geometry.uv[i*6+(j*2+0)], obj->geometry.uv[i*6+(j*2+1)]
						};
					}
					obj->numTexturedTriangles++;
				}
			}
			Gfx_SetDynamicVbData(obj->vertexID, &obj->vertices, 4 * SM64_GEO_MAX_TRIANGLES);
			Gfx_SetDynamicVbData(obj->texturedVertexID, &obj->texturedVertices, 4 * SM64_GEO_MAX_TRIANGLES);

			if ((int)(obj->lastPos.X) != (int)(obj->currPos.X) || (int)(obj->lastPos.Y) != (int)(obj->currPos.Y) || (int)(obj->lastPos.Z) != (int)(obj->currPos.Z))
			{
				loadNewBlocks(i, obj->currPos.X/MARIO_SCALE, obj->currPos.Y/MARIO_SCALE, obj->currPos.Z/MARIO_SCALE, obj->surfaces);
			}
		}
	}

	if (ticksBeforeSpawn > 0) ticksBeforeSpawn--;
}

void selfMarioTick(struct ScheduledTask* task)
{
	if (!marioInstances[ENTITIES_SELF_ID]) return;
	struct MarioInstance *obj = marioInstances[ENTITIES_SELF_ID];

	obj->state.position[0] = obj->lastPos.X + ((obj->currPos.X - obj->lastPos.X) * (marioInterpTicks / (1.f/30)));
	obj->state.position[1] = obj->lastPos.Y + ((obj->currPos.Y - obj->lastPos.Y) * (marioInterpTicks / (1.f/30)));
	obj->state.position[2] = obj->lastPos.Z + ((obj->currPos.Z - obj->lastPos.Z) * (marioInterpTicks / (1.f/30)));
	if (marioInterpTicks < 1.f/30) marioInterpTicks += 1.f/300;

	struct LocationUpdate update = {0};
	update.Flags = LOCATIONUPDATE_POS;
	update.Pos.X = obj->state.position[0] / MARIO_SCALE;
	update.Pos.Y = obj->state.position[1] / MARIO_SCALE;
	update.Pos.Z = obj->state.position[2] / MARIO_SCALE;
	update.RelativePos = false;
	if (pluginOptions[PLUGINOPTION_CAMERA].value)
	{
		static float currYaw = 0;
		currYaw = Entities_->List[ENTITIES_SELF_ID]->Yaw;
		float diffYaw = ((-obj->state.faceAngle + MATH_PI) * MATH_RAD2DEG) - Entities_->List[ENTITIES_SELF_ID]->Yaw;
		if (abs(diffYaw) >= 180)
		{
			currYaw = diffYaw;
			diffYaw = ((-obj->state.faceAngle + MATH_PI) * MATH_RAD2DEG) - currYaw;
		}
		currYaw += diffYaw*0.05f;
		update.Flags |= LOCATIONUPDATE_YAW;
		update.Yaw = ((-obj->state.faceAngle + MATH_PI) * MATH_RAD2DEG); // use currYaw for smooth camera (must fix)
		//printf("%.2f %.2f %.2f\n", update.Yaw, Entities_->List[ENTITIES_SELF_ID]->Yaw, diffYaw);
	}
	Entities_->List[ENTITIES_SELF_ID]->VTABLE->SetLocation(Entities_->List[ENTITIES_SELF_ID], &update, false);
	Entities_->List[ENTITIES_SELF_ID]->Velocity.X = Entities_->List[ENTITIES_SELF_ID]->Velocity.Y = Entities_->List[ENTITIES_SELF_ID]->Velocity.Z = 0;

	if (Gui_->InputGrab) // menu is open
	{
		memset(&obj->input, 0, sizeof(struct SM64MarioInputs));
		return;
	}

	float dir = 0;
	float spd = 0;
	if (KeyBind_IsPressed(KEYBIND_FORWARD) && KeyBind_IsPressed(KEYBIND_RIGHT))
	{
		dir = -MATH_PI * 0.25f;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_FORWARD) && KeyBind_IsPressed(KEYBIND_LEFT))
	{
		dir = -MATH_PI * 0.75f;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_BACK) && KeyBind_IsPressed(KEYBIND_RIGHT))
	{
		dir = MATH_PI * 0.25f;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_BACK) && KeyBind_IsPressed(KEYBIND_LEFT))
	{
		dir = MATH_PI * 0.75f;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_FORWARD))
	{
		dir = -MATH_PI * 0.5f;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_BACK))
	{
		dir = MATH_PI * 0.5f;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_LEFT))
	{
		dir = MATH_PI;
		spd = 1;
	}
	else if (KeyBind_IsPressed(KEYBIND_RIGHT))
	{
		dir = 0;
		spd = 1;
	}

	obj->input.stickX = Math_Cos(dir) * spd;
	obj->input.stickY = Math_Sin(dir) * spd;
	if (Camera_->Active->isThirdPerson)
	{
		obj->input.camLookX = (update.Pos.X - Camera_->CurrentPos.X);
		obj->input.camLookZ = (update.Pos.Z - Camera_->CurrentPos.Z);
	}
	else
	{
		obj->input.camLookX = (update.Pos.X - Camera_->CurrentPos.X) + (Math_Sin((-Entities_->List[ENTITIES_SELF_ID]->Yaw + 180) * MATH_DEG2RAD));
		obj->input.camLookZ = (update.Pos.Z - Camera_->CurrentPos.Z) + (Math_Cos((-Entities_->List[ENTITIES_SELF_ID]->Yaw + 180) * MATH_DEG2RAD));
	}
	obj->input.buttonA = KeyBind_IsPressed(KEYBIND_JUMP);
	obj->input.buttonB = KeyBind_IsPressed(KEYBIND_DELETE_BLOCK);
	obj->input.buttonZ = KeyBind_IsPressed(KEYBIND_SPEED);
	/*
	Entities_->List[ENTITIES_SELF_ID]->Position.X = marioState.position[0];
	Entities_->List[ENTITIES_SELF_ID]->Position.Y = marioState.position[1];
	Entities_->List[ENTITIES_SELF_ID]->Position.Z = marioState.position[2];
	*/
}

static void Classic64_Init()
{
	LoadSymbolsFromGame();
	for (int i=0; i<256; i++) marioInstances[i] = 0;
	loadSettings();

	FILE *f = fopen("plugins/sm64.us.z64", "rb");

	if (!f)
	{
		inited = false;
		SendChat("&cSuper Mario 64 US ROM not found!", NULL, NULL, NULL);
		SendChat("&cClassic64 will not function without it.", NULL, NULL, NULL);
		SendChat("&cPlease supply a ROM with filename 'sm64.us.z64'", NULL, NULL, NULL);
		SendChat("&cand place it inside the plugins folder.", NULL, NULL, NULL);
		return;
	}

	inited = true;
	allowTick = false;
	ticksBeforeSpawn = 1;
	marioInterpTicks = 0;

	// load libsm64
	uint8_t *romBuffer;
	size_t romFileLength;

	fseek(f, 0, SEEK_END);
	romFileLength = (size_t)ftell(f);
	rewind(f);
	romBuffer = (uint8_t*)malloc(romFileLength + 1);
	fread(romBuffer, 1, romFileLength, f);
	romBuffer[romFileLength] = 0;
	fclose(f);

	// Mario texture is 704x64 RGBA (changed to 1024x64 in this classicube plugin)
	marioTextureUint8 = (uint8_t*)malloc(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);
	marioBitmap.scan0 = (BitmapCol*)malloc(sizeof(BitmapCol) * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT); // texture sizes must be power of two

	sm64_global_terminate();
	sm64_global_init(romBuffer, marioTextureUint8, NULL);
	f = fopen("texture.raw", "wb");
	for (int i=0; i<SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT; i++) // copy texture to classicube bitmap
	{
		int x = i % 1024;
		if (x < 704)
		{
			cc_uint8 a = marioTextureUint8[i*4+3];
			cc_uint8 r = marioTextureUint8[i*4+0];
			cc_uint8 g = marioTextureUint8[i*4+1];
			cc_uint8 b = marioTextureUint8[i*4+2];
			/*
			if (x >= 128 && x <= 128+32 && a == 0) // hat "M" logo
			{
				a = 255;
				r = 255;
				g = 0;
				b = 0;
			}
			*/
			marioBitmap.scan0[i] = b + (g << 8) + (r << 16) + (a << 24);
		}
		else
		{
			// fill X pos above 704 with white, for use with untextured (colored) triangles
			marioBitmap.scan0[i] = 255 + (255 << 8) + (255 << 16) + (255 << 24);
		}
		fwrite(&marioBitmap.scan0[i], sizeof(BitmapCol), 1, f);
	}
	fclose(f);
	free(marioTextureUint8);
	free(romBuffer);

	marioTextureID = Gfx_CreateTexture(&marioBitmap, 0, false);

	SendChat("&aSuper Mario 64 US ROM loaded!", NULL, NULL, NULL);
	if (!pluginOptions[PLUGINOPTION_FIRST_USE].value)
	{
		// give a little tutorial
		pluginOptions[PLUGINOPTION_FIRST_USE].value = 1;
		saveSettings();
		SendChat("&bTo get started, switch to Mario by using &m/model mario64", NULL, NULL, NULL);
		SendChat("&bYou can find some extra commands by typing &m/client mario64", NULL, NULL, NULL);
		SendChat("&bHave fun playing as &mMario!", NULL, NULL, NULL);
	}

	String_AppendConst(&Server_->AppName, " + Classic64 Mario 64 WIP");

	Model_Register(marioModel_GetInstance());
	Event_Register_(&ChatEvents_->ChatReceived, NULL, OnChatMessage);

	ScheduledTask_Add(1./30, marioTick);
	ScheduledTask_Add(1./300, selfMarioTick);
	Commands_Register(&MarioClientCmd);
}

static void Classic64_Free()
{
	if (!inited) return;
	allowTick = false;
	for (int i=0; i<256; i++)
		deleteMario(i);
}

static void Classic64_Reset()
{
	if (!inited) return;
	allowTick = false;
	for (int i=0; i<256; i++)
		deleteMario(i);
}

static void Classic64_OnNewMap()
{
	if (!inited) return;
	allowTick = false;
	for (int i=0; i<256; i++)
		deleteMario(i);
}

static void Classic64_OnNewMapLoaded()
{
	if (!inited) return;
	allowTick = true;
	ticksBeforeSpawn = 1;
}

EXPORT int Plugin_ApiVersion = 1;
EXPORT struct IGameComponent Plugin_Component = {
	Classic64_Init,
	Classic64_Free,
	Classic64_Reset,
	Classic64_OnNewMap,
	Classic64_OnNewMapLoaded
};

#define QUOTE(x) #x

#ifdef CC_BUILD_WIN
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOMCX
#define NOIME
#include <windows.h>
#define LoadSymbol(name) name ## _ = GetProcAddress(app, QUOTE(name))
#else
#define _GNU_SOURCE
#include <dlfcn.h>
#define LoadSymbol(name) name ## _ = dlsym(RTLD_DEFAULT, QUOTE(name))
#endif

static void LoadSymbolsFromGame(void) {
#ifdef CC_BUILD_WIN
	HMODULE app = GetModuleHandle(NULL);
#endif
	LoadSymbol(Server); 
	LoadSymbol(Blocks);
	LoadSymbol(Models); 
	LoadSymbol(Game);
	LoadSymbol(World);
	LoadSymbol(Env);
	LoadSymbol(Entities);
	LoadSymbol(Camera);
	LoadSymbol(Gui);
	LoadSymbol(Lighting);
	LoadSymbol(ChatEvents);
}
