#ifdef _WIN32
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// classicube symbols
static void LoadSymbolsFromGame(void);
static struct _ServerConnectionData* Server_;
static struct _BlockLists* Blocks_;
static struct _ModelsData* Models_;
static struct _GameData* Game_;
static struct _WorldData* World_;
static struct _EntitiesData* Entities_;
static struct _CameraData* Camera_;
static struct _GuiData* Gui_;
static struct _Lighting* Lighting_;
static struct _ChatEventsList* ChatEvents_;

// plugin settings
enum
{
	PLUGINOPTION_HURT,
	PLUGINOPTION_CAMERA,
	PLUGINOPTION_BGR,
	PLUGINOPTIONS_MAX
};

struct PluginOption {
	cc_string name;
	cc_string usage;
	cc_string desc[3];
	int descLines;
	int value;
} pluginOptions[] = {
	{
		String_FromConst("hurt"),
		String_FromConst("<on/off>"),
		{String_FromConst("&eSet whether Mario can get hurt.")},
		1, 0
	},
	{
		String_FromConst("camera"),
		String_FromConst("<on/off>"),
		{String_FromConst("&eRotate the camera automatically behind Mario.")},
		1, 0
	},
	{
		String_FromConst("bgr"),
		String_FromConst("<on/off>"),
		{
			String_FromConst("&eSwap Mario's RGB colors with BGR."),
			String_FromConst("&eThis is off by default."),
			String_FromConst("&eIf Mario's colors appear inverted, change this setting."),
		},
		3, 0
	},
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
/*
struct MarioInstance
{
	int32_t ID;
	uint32_t objs[9*3];
	Vec3 lastPos;

	struct SM64MarioInputs inputs;
	struct SM64MarioState state;
	struct SM64MarioGeometryBuffers geometry;
} *marioInstances[256];
*/
uint32_t marioObjs[256][9*3]; // sm64_surface_object_create()
int32_t marioIds[256];
Vec3 lastPos[256];
int ticksBeforeSpawn;
bool inited;
bool allowTick; // false when loading world
struct SM64MarioInputs marioInputs;
struct SM64MarioState marioState;
struct SM64MarioGeometryBuffers marioGeometry;

// mario's model (the hard part)
static struct Bitmap marioBitmap = {0, 1024, SM64_TEXTURE_HEIGHT};
struct VertexTextured marioVertices[4 * SM64_GEO_MAX_TRIANGLES];
struct VertexTextured marioTexturedVertices[4 * SM64_GEO_MAX_TRIANGLES];
GfxResourceID marioTextureID;
GfxResourceID marioVertexID;
GfxResourceID marioTexturedVertexID;
uint16_t numTexturedTriangles;

static void marioModel_Draw(struct Entity* p)
{
	Gfx_BindTexture(marioTextureID);

	// draw colored triangles first (can't use VERTEX_FORMAT_COLOURED with VertexColoured structs because that crashes randomly)
	Gfx_BindVb(marioVertexID);
	Gfx_DrawVb_IndexedTris(4 * marioGeometry.numTrianglesUsed);

	// draw textured triangles next (eyes, mustache, etc)
	Gfx_BindVb(marioTexturedVertexID);
	Gfx_DrawVb_IndexedTris(4 * numTexturedTriangles);
}

static void marioModel_GetTransform(struct Entity* entity, Vec3 pos, struct Matrix* m)
{
	struct Matrix tmp;
	Matrix_Scale(m, 1,1,1);
	Matrix_Translate(&tmp, pos.X, pos.Y, pos.Z);
	Matrix_MulBy(m, &tmp);
}

static void marioModel_DrawArm(struct Entity* entity) {}
static void marioModel_MakeParts(void) {}
static float marioModel_GetNameY(struct Entity* e) { return 28/16.0f; }
static float marioModel_GetEyeY(struct Entity* e)  { return 1; }
static void marioModel_GetSize(struct Entity* e) {e->Size = (Vec3) { 1, 1, 1 };}
static void marioModel_GetBounds(struct Entity* e) {e->ModelAABB = (struct AABB) { 0, 0, 0, 0, 0, 0 };}
static struct ModelVertex mario_unused_vertices[1]; // without this, the game crashes in first person view with nothing held in hand
static struct Model mario_model = { "mario64", mario_unused_vertices, NULL,
	marioModel_MakeParts, marioModel_Draw,
	marioModel_GetNameY,  marioModel_GetEyeY,
	marioModel_GetSize,   marioModel_GetBounds
};

static struct Model* marioModel_GetInstance(void) {
	Model_Init(&mario_model);
	mario_model.GetTransform = marioModel_GetTransform;
	mario_model.DrawArm = marioModel_DrawArm;
	mario_model.shadowScale = 0.85f;
	mario_model.usesHumanSkin = true; // without this, the game crashes in first person view with nothing held in hand
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

		if (marioIds[ENTITIES_SELF_ID] == -1)
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
				sm64_set_mario_state(marioIds[ENTITIES_SELF_ID], 0);
				sm64_mario_interact_cap(marioIds[ENTITIES_SELF_ID], caps[i].capFlag, 65535, 0);
				return;
			}
		}
		SendChat("&cUnknown cap \"%s\"", &args[1], NULL, NULL);
		return;
	}
	else if (String_Compare(&args[0], &options[2]) == 0) // kill mario
	{
		if (marioIds[ENTITIES_SELF_ID] == -1)
		{
			SendChat("&cSwitch to Mario first", NULL, NULL, NULL);
			return;
		}
		sm64_mario_kill(marioIds[ENTITIES_SELF_ID]);
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
				strcat(msgBuffer, pluginOptions[i].name.buffer);
				if (i != PLUGINOPTIONS_MAX-1) strcat(msgBuffer, ", ");
			}
			SendChat(msgBuffer, NULL, NULL, NULL);
			return;
		}

		for (int i=0; i<PLUGINOPTIONS_MAX; i++)
		{
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
void loadNewBlocks(int i, int x, int y, int z)
{
	int j;
	for (j=0; j<9*3; j++)
	{
		if (marioObjs[i][j] == -1) continue;
		sm64_surface_object_delete(marioObjs[i][j]);
		marioObjs[i][j] = -1;
	}

	j = 0;
	for (int zadd=-1; zadd<=1; zadd++)
	{
		for (int xadd=-1; xadd<=1; xadd++)
		{
			if (x+xadd < 0 || x+xadd >= World_->Width || z+zadd < 0 || z+zadd >= World_->Length) continue;

			// get block at mario's head if it exists
			BlockID block = World_GetBlock(x+xadd, y+1, z+zadd);
			if (block != 0 && (Blocks_->Collide[block] == COLLIDE_SOLID || Blocks_->Collide[block] == COLLIDE_ICE || Blocks_->Collide[block] == COLLIDE_SLIPPERY_ICE))
			{
				struct SM64SurfaceObject obj;
				memset(&obj.transform, 0, sizeof(struct SM64ObjectTransform));
				obj.transform.position[0] = (x+xadd) * IMARIO_SCALE;
				obj.transform.position[1] = (y+1) * IMARIO_SCALE - IMARIO_SCALE;
				obj.transform.position[2] = (z+zadd) * IMARIO_SCALE;
				obj.surfaceCount = 6*2;
				obj.surfaces = (struct SM64Surface*)malloc(sizeof(struct SM64Surface) * obj.surfaceCount);

				for (int ind=0; ind<obj.surfaceCount; ind++)
				{
					obj.surfaces[ind].type = (Blocks_->Collide[block] == COLLIDE_ICE || Blocks_->Collide[block] == COLLIDE_SLIPPERY_ICE) ? SURFACE_ICE : SURFACE_DEFAULT;
					obj.surfaces[ind].force = 0;
					switch(Blocks_->StepSounds[block])
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
				obj.surfaces[0].vertices[0][0] = IMARIO_SCALE; obj.surfaces[0].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[0].vertices[0][2] = IMARIO_SCALE;
				obj.surfaces[0].vertices[1][0] = 0; obj.surfaces[0].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[0].vertices[1][2] = 0;
				obj.surfaces[0].vertices[2][0] = 0; obj.surfaces[0].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[0].vertices[2][2] = IMARIO_SCALE;

				obj.surfaces[1].vertices[0][0] = 0; obj.surfaces[1].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[1].vertices[0][2] = 0;
				obj.surfaces[1].vertices[1][0] = IMARIO_SCALE; obj.surfaces[1].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[1].vertices[1][2] = IMARIO_SCALE;
				obj.surfaces[1].vertices[2][0] = IMARIO_SCALE; obj.surfaces[1].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[1].vertices[2][2] = 0;

				// left (Z+)
				obj.surfaces[2].vertices[0][0] = 0; obj.surfaces[2].vertices[0][1] = 0; obj.surfaces[2].vertices[0][2] = IMARIO_SCALE;
				obj.surfaces[2].vertices[1][0] = IMARIO_SCALE; obj.surfaces[2].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[2].vertices[1][2] = IMARIO_SCALE;
				obj.surfaces[2].vertices[2][0] = 0; obj.surfaces[2].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[2].vertices[2][2] = IMARIO_SCALE;

				obj.surfaces[3].vertices[0][0] = IMARIO_SCALE; obj.surfaces[3].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[3].vertices[0][2] = IMARIO_SCALE;
				obj.surfaces[3].vertices[1][0] = 0; obj.surfaces[3].vertices[1][1] = 0; obj.surfaces[3].vertices[1][2] = IMARIO_SCALE;
				obj.surfaces[3].vertices[2][0] = IMARIO_SCALE; obj.surfaces[3].vertices[2][1] = 0; obj.surfaces[3].vertices[2][2] = IMARIO_SCALE;

				// right (Z-)
				obj.surfaces[4].vertices[0][0] = IMARIO_SCALE; obj.surfaces[4].vertices[0][1] = 0; obj.surfaces[4].vertices[0][2] = 0;
				obj.surfaces[4].vertices[1][0] = 0; obj.surfaces[4].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[4].vertices[1][2] = 0;
				obj.surfaces[4].vertices[2][0] = IMARIO_SCALE; obj.surfaces[4].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[4].vertices[2][2] = 0;

				obj.surfaces[5].vertices[0][0] = 0; obj.surfaces[5].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[5].vertices[0][2] = 0;
				obj.surfaces[5].vertices[1][0] = IMARIO_SCALE; obj.surfaces[5].vertices[1][1] = 0; obj.surfaces[5].vertices[1][2] = 0;
				obj.surfaces[5].vertices[2][0] = 0; obj.surfaces[5].vertices[2][1] = 0; obj.surfaces[5].vertices[2][2] = 0;

				// back (X+)
				obj.surfaces[6].vertices[0][0] = IMARIO_SCALE; obj.surfaces[6].vertices[0][1] = 0; obj.surfaces[6].vertices[0][2] = 0;
				obj.surfaces[6].vertices[1][0] = IMARIO_SCALE; obj.surfaces[6].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[6].vertices[1][2] = 0;
				obj.surfaces[6].vertices[2][0] = IMARIO_SCALE; obj.surfaces[6].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[6].vertices[2][2] = IMARIO_SCALE;

				obj.surfaces[7].vertices[0][0] = IMARIO_SCALE; obj.surfaces[7].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[7].vertices[0][2] = IMARIO_SCALE;
				obj.surfaces[7].vertices[1][0] = IMARIO_SCALE; obj.surfaces[7].vertices[1][1] = 0; obj.surfaces[7].vertices[1][2] = IMARIO_SCALE;
				obj.surfaces[7].vertices[2][0] = IMARIO_SCALE; obj.surfaces[7].vertices[2][1] = 0; obj.surfaces[7].vertices[2][2] = 0;

				// front (X-)
				obj.surfaces[8].vertices[0][0] = 0; obj.surfaces[8].vertices[0][1] = 0; obj.surfaces[8].vertices[0][2] = IMARIO_SCALE;
				obj.surfaces[8].vertices[1][0] = 0; obj.surfaces[8].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[8].vertices[1][2] = IMARIO_SCALE;
				obj.surfaces[8].vertices[2][0] = 0; obj.surfaces[8].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[8].vertices[2][2] = 0;

				obj.surfaces[9].vertices[0][0] = 0; obj.surfaces[9].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[9].vertices[0][2] = 0;
				obj.surfaces[9].vertices[1][0] = 0; obj.surfaces[9].vertices[1][1] = 0; obj.surfaces[9].vertices[1][2] = 0;
				obj.surfaces[9].vertices[2][0] = 0; obj.surfaces[9].vertices[2][1] = 0; obj.surfaces[9].vertices[2][2] = IMARIO_SCALE;

				// block bottom face
				obj.surfaces[10].vertices[0][0] = 0; obj.surfaces[10].vertices[0][1] = 0; obj.surfaces[10].vertices[0][2] = IMARIO_SCALE;
				obj.surfaces[10].vertices[1][0] = 0; obj.surfaces[10].vertices[1][1] = 0; obj.surfaces[10].vertices[1][2] = 0;
				obj.surfaces[10].vertices[2][0] = IMARIO_SCALE; obj.surfaces[10].vertices[2][1] = 0; obj.surfaces[10].vertices[2][2] = IMARIO_SCALE;

				obj.surfaces[11].vertices[0][0] = IMARIO_SCALE; obj.surfaces[11].vertices[0][1] = 0; obj.surfaces[11].vertices[0][2] = 0;
				obj.surfaces[11].vertices[1][0] = IMARIO_SCALE; obj.surfaces[11].vertices[1][1] = 0; obj.surfaces[11].vertices[1][2] = IMARIO_SCALE;
				obj.surfaces[11].vertices[2][0] = 0; obj.surfaces[11].vertices[2][1] = 0; obj.surfaces[11].vertices[2][2] = 0;

				marioObjs[i][j++] = sm64_surface_object_create(&obj);
				free(obj.surfaces);
			}

			// get block at floor
			int yadd = 1;
			do
			{
				yadd--;
				if (y+yadd < 0 || y+yadd >= World_->Height) break;
				block = World_GetBlock(x+xadd, y+yadd, z+zadd);
			} while (block == 0 || (Blocks_->Collide[block] != COLLIDE_SOLID && Blocks_->Collide[block] != COLLIDE_ICE && Blocks_->Collide[block] != COLLIDE_SLIPPERY_ICE));

			struct SM64SurfaceObject obj;
			memset(&obj.transform, 0, sizeof(struct SM64ObjectTransform));
			obj.transform.position[0] = (x+xadd) * IMARIO_SCALE;
			obj.transform.position[1] = (y+yadd) * IMARIO_SCALE - IMARIO_SCALE;
			obj.transform.position[2] = (z+zadd) * IMARIO_SCALE;
			obj.surfaceCount = 6*2;
			obj.surfaces = (struct SM64Surface*)malloc(sizeof(struct SM64Surface) * obj.surfaceCount);

			for (int ind=0; ind<obj.surfaceCount; ind++)
			{
				obj.surfaces[ind].type = (Blocks_->Collide[block] == COLLIDE_ICE || Blocks_->Collide[block] == COLLIDE_SLIPPERY_ICE) ? SURFACE_ICE : SURFACE_DEFAULT;
				obj.surfaces[ind].force = 0;
				switch(Blocks_->StepSounds[block])
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
			obj.surfaces[0].vertices[0][0] = IMARIO_SCALE; obj.surfaces[0].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[0].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[0].vertices[1][0] = 0; obj.surfaces[0].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[0].vertices[1][2] = 0;
			obj.surfaces[0].vertices[2][0] = 0; obj.surfaces[0].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[0].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[1].vertices[0][0] = 0; obj.surfaces[1].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[1].vertices[0][2] = 0;
			obj.surfaces[1].vertices[1][0] = IMARIO_SCALE; obj.surfaces[1].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[1].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[1].vertices[2][0] = IMARIO_SCALE; obj.surfaces[1].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[1].vertices[2][2] = 0;

			// left (Z+)
			obj.surfaces[2].vertices[0][0] = 0; obj.surfaces[2].vertices[0][1] = 0; obj.surfaces[2].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[2].vertices[1][0] = IMARIO_SCALE; obj.surfaces[2].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[2].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[2].vertices[2][0] = 0; obj.surfaces[2].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[2].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[3].vertices[0][0] = IMARIO_SCALE; obj.surfaces[3].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[3].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[3].vertices[1][0] = 0; obj.surfaces[3].vertices[1][1] = 0; obj.surfaces[3].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[3].vertices[2][0] = IMARIO_SCALE; obj.surfaces[3].vertices[2][1] = 0; obj.surfaces[3].vertices[2][2] = IMARIO_SCALE;

			// right (Z-)
			obj.surfaces[4].vertices[0][0] = IMARIO_SCALE; obj.surfaces[4].vertices[0][1] = 0; obj.surfaces[4].vertices[0][2] = 0;
			obj.surfaces[4].vertices[1][0] = 0; obj.surfaces[4].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[4].vertices[1][2] = 0;
			obj.surfaces[4].vertices[2][0] = IMARIO_SCALE; obj.surfaces[4].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[4].vertices[2][2] = 0;

			obj.surfaces[5].vertices[0][0] = 0; obj.surfaces[5].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[5].vertices[0][2] = 0;
			obj.surfaces[5].vertices[1][0] = IMARIO_SCALE; obj.surfaces[5].vertices[1][1] = 0; obj.surfaces[5].vertices[1][2] = 0;
			obj.surfaces[5].vertices[2][0] = 0; obj.surfaces[5].vertices[2][1] = 0; obj.surfaces[5].vertices[2][2] = 0;

			// back (X+)
			obj.surfaces[6].vertices[0][0] = IMARIO_SCALE; obj.surfaces[6].vertices[0][1] = 0; obj.surfaces[6].vertices[0][2] = 0;
			obj.surfaces[6].vertices[1][0] = IMARIO_SCALE; obj.surfaces[6].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[6].vertices[1][2] = 0;
			obj.surfaces[6].vertices[2][0] = IMARIO_SCALE; obj.surfaces[6].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[6].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[7].vertices[0][0] = IMARIO_SCALE; obj.surfaces[7].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[7].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[7].vertices[1][0] = IMARIO_SCALE; obj.surfaces[7].vertices[1][1] = 0; obj.surfaces[7].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[7].vertices[2][0] = IMARIO_SCALE; obj.surfaces[7].vertices[2][1] = 0; obj.surfaces[7].vertices[2][2] = 0;

			// front (X-)
			obj.surfaces[8].vertices[0][0] = 0; obj.surfaces[8].vertices[0][1] = 0; obj.surfaces[8].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[8].vertices[1][0] = 0; obj.surfaces[8].vertices[1][1] = IMARIO_SCALE*2; obj.surfaces[8].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[8].vertices[2][0] = 0; obj.surfaces[8].vertices[2][1] = IMARIO_SCALE*2; obj.surfaces[8].vertices[2][2] = 0;

			obj.surfaces[9].vertices[0][0] = 0; obj.surfaces[9].vertices[0][1] = IMARIO_SCALE*2; obj.surfaces[9].vertices[0][2] = 0;
			obj.surfaces[9].vertices[1][0] = 0; obj.surfaces[9].vertices[1][1] = 0; obj.surfaces[9].vertices[1][2] = 0;
			obj.surfaces[9].vertices[2][0] = 0; obj.surfaces[9].vertices[2][1] = 0; obj.surfaces[9].vertices[2][2] = IMARIO_SCALE;

			// block bottom face
			obj.surfaces[10].vertices[0][0] = 0; obj.surfaces[10].vertices[0][1] = 0; obj.surfaces[10].vertices[0][2] = IMARIO_SCALE;
			obj.surfaces[10].vertices[1][0] = 0; obj.surfaces[10].vertices[1][1] = 0; obj.surfaces[10].vertices[1][2] = 0;
			obj.surfaces[10].vertices[2][0] = IMARIO_SCALE; obj.surfaces[10].vertices[2][1] = 0; obj.surfaces[10].vertices[2][2] = IMARIO_SCALE;

			obj.surfaces[11].vertices[0][0] = IMARIO_SCALE; obj.surfaces[11].vertices[0][1] = 0; obj.surfaces[11].vertices[0][2] = 0;
			obj.surfaces[11].vertices[1][0] = IMARIO_SCALE; obj.surfaces[11].vertices[1][1] = 0; obj.surfaces[11].vertices[1][2] = IMARIO_SCALE;
			obj.surfaces[11].vertices[2][0] = 0; obj.surfaces[11].vertices[2][1] = 0; obj.surfaces[11].vertices[2][2] = 0;

			marioObjs[i][j++] = sm64_surface_object_create(&obj);
			free(obj.surfaces);
		}
	}
}

void marioTick(struct ScheduledTask* task)
{
	if (!allowTick) return;

	for (int i=0; i<256; i++)
	{
		if (!Entities_->List[i]) continue;
		if (marioIds[i] == -1 && ticksBeforeSpawn <= 0 && strcmp(Entities_->List[i]->Model->name, "mario64") == 0)
		{
			// spawn mario
			loadNewBlocks(i, Entities_->List[i]->Position.X, Entities_->List[i]->Position.Y, Entities_->List[i]->Position.Z);
			marioIds[i] = sm64_mario_create(Entities_->List[i]->Position.X*IMARIO_SCALE, Entities_->List[i]->Position.Y*IMARIO_SCALE, Entities_->List[i]->Position.Z*IMARIO_SCALE, 0,0,0,0);
			if (marioIds[i] == -1)
			{
				SendChat("&cFailed to spawn Mario", NULL, NULL, NULL);
			}
			else
			{
				lastPos[i].X = Entities_->List[i]->Position.X*IMARIO_SCALE;
				lastPos[i].Y = Entities_->List[i]->Position.Y*IMARIO_SCALE;
				lastPos[i].Z = Entities_->List[i]->Position.Z*IMARIO_SCALE;
			}
		}
		else if (marioIds[i] != -1)
		{
			if (strcmp(Entities_->List[i]->Model->name, "mario64"))
			{
				sm64_mario_delete(marioIds[i]);
				marioIds[i] = -1;
				continue;
			}

			if (i != ENTITIES_SELF_ID) sm64_set_mario_position(marioIds[i], Entities_->List[ENTITIES_SELF_ID]->Position.X, Entities_->List[ENTITIES_SELF_ID]->Position.Y, Entities_->List[ENTITIES_SELF_ID]->Position.Z);
			else if (!pluginOptions[PLUGINOPTION_HURT].value && sm64_mario_get_health(marioIds[i]) != 0xff) sm64_mario_set_health(marioIds[i], 0x880);

			if (marioState.position[0] != 0 && marioState.position[1] != 0 && marioState.position[2] != 0)
			{
				lastPos[i].X = marioState.position[0]; lastPos[i].Y = marioState.position[1]; lastPos[i].Z = marioState.position[2];
			}

			// water
			int yadd = 0;
			int waterY = -1024;
			while (marioState.position[1]/MARIO_SCALE+yadd > waterY && Blocks_->IsLiquid[World_GetBlock(marioState.position[0]/MARIO_SCALE, marioState.position[1]/MARIO_SCALE+yadd, marioState.position[2]/MARIO_SCALE)])
			{
				waterY = marioState.position[1]/MARIO_SCALE+yadd;
				yadd++;
			}
			sm64_set_mario_water_level(marioIds[i], waterY*IMARIO_SCALE+IMARIO_SCALE);

			sm64_mario_tick(marioIds[i], &marioInputs, &marioState, &marioGeometry);

			numTexturedTriangles = 0;
			bool bgr = (pluginOptions[PLUGINOPTION_BGR].value);
			for (int i=0; i<marioGeometry.numTrianglesUsed; i++)
			{
				bool hasTexture = (marioGeometry.uv[i*6+0] != 1 && marioGeometry.uv[i*6+1] != 1 && marioGeometry.uv[i*6+2] != 1 && marioGeometry.uv[i*6+3] != 1 && marioGeometry.uv[i*6+4] != 1 && marioGeometry.uv[i*6+5] != 1);
				bool wingCap = (marioState.flags & MARIO_WING_CAP && i >= marioGeometry.numTrianglesUsed-8 && i <= marioGeometry.numTrianglesUsed-1); // do not draw white rectangles around wingcap

				marioVertices[i*4+0] = (struct VertexTextured) {
					(marioGeometry.position[i*9+0] - marioState.position[0]) / MARIO_SCALE,
					(marioGeometry.position[i*9+1] - marioState.position[1]) / MARIO_SCALE,
					(marioGeometry.position[i*9+2] - marioState.position[2]) / MARIO_SCALE,
					PackedCol_Tint(PackedCol_Make(marioGeometry.color[i*9 + (bgr?2:0)]*255, marioGeometry.color[i*9+1]*255, marioGeometry.color[i*9 + (bgr?0:2)]*255, 255), Lighting_->Color(marioState.position[0]/MARIO_SCALE, marioState.position[1]/MARIO_SCALE, marioState.position[2]/MARIO_SCALE)),
					(wingCap) ? 0 : 0.95, (wingCap) ? 0.95 : 0
				};

				marioVertices[i*4+1] = (struct VertexTextured) {
					(marioGeometry.position[i*9+3] - marioState.position[0]) / MARIO_SCALE,
					(marioGeometry.position[i*9+4] - marioState.position[1]) / MARIO_SCALE,
					(marioGeometry.position[i*9+5] - marioState.position[2]) / MARIO_SCALE,
					PackedCol_Tint(PackedCol_Make(marioGeometry.color[i*9 + (bgr?5:3)]*255, marioGeometry.color[i*9+4]*255, marioGeometry.color[i*9 + (bgr?3:5)]*255, 255), Lighting_->Color(marioState.position[0]/MARIO_SCALE, marioState.position[1]/MARIO_SCALE, marioState.position[2]/MARIO_SCALE)),
					(wingCap) ? 0 : 0.96, (wingCap) ? 0.96 : 0
				};

				marioVertices[i*4+2] = (struct VertexTextured) {
					(marioGeometry.position[i*9+6] - marioState.position[0]) / MARIO_SCALE,
					(marioGeometry.position[i*9+7] - marioState.position[1]) / MARIO_SCALE,
					(marioGeometry.position[i*9+8] - marioState.position[2]) / MARIO_SCALE,
					PackedCol_Tint(PackedCol_Make(marioGeometry.color[i*9 + (bgr?8:6)]*255, marioGeometry.color[i*9+7]*255, marioGeometry.color[i*9 + (bgr?6:8)]*255, 255), Lighting_->Color(marioState.position[0]/MARIO_SCALE, marioState.position[1]/MARIO_SCALE, marioState.position[2]/MARIO_SCALE)),
					(wingCap) ? 0.01 : 0.96, (wingCap) ? 0.96 : 1
				};

				marioVertices[i*4+3] = marioVertices[i*4+2]; // WHY IS IT A QUAD?!?!?!?!? why does classicube use quads!??!?!??

				if (hasTexture)
				{
					for (int j=0; j<4; j++)
					{
						marioTexturedVertices[numTexturedTriangles*4+j] = (struct VertexTextured) {
							marioVertices[i*4+j].X, marioVertices[i*4+j].Y, marioVertices[i*4+j].Z,
							PackedCol_Tint(PACKEDCOL_WHITE, Lighting_->Color(marioState.position[0]/MARIO_SCALE, marioState.position[1]/MARIO_SCALE, marioState.position[2]/MARIO_SCALE)),
							marioGeometry.uv[i*6+(j*2+0)], marioGeometry.uv[i*6+(j*2+1)]
						};
					}
					numTexturedTriangles++;
				}
			}
			Gfx_SetDynamicVbData(marioVertexID, &marioVertices, 4 * SM64_GEO_MAX_TRIANGLES);
			Gfx_SetDynamicVbData(marioTexturedVertexID, &marioTexturedVertices, 4 * SM64_GEO_MAX_TRIANGLES);

			if ((int)(lastPos[i].X) != (int)(marioState.position[0]) || (int)(lastPos[i].Y) != (int)(marioState.position[1]) || (int)(lastPos[i].Z) != (int)(marioState.position[2]))
			{
				loadNewBlocks(i, marioState.position[0]/MARIO_SCALE, marioState.position[1]/MARIO_SCALE, marioState.position[2]/MARIO_SCALE);
			}
		}
	}

	if (ticksBeforeSpawn > 0) ticksBeforeSpawn--;
}

void selfMarioTick(struct ScheduledTask* task)
{
	if (marioIds[ENTITIES_SELF_ID] == -1) return;

	struct LocationUpdate update = {0};
	update.Flags = LOCATIONUPDATE_POS;
	update.Pos.X = marioState.position[0] / MARIO_SCALE;
	update.Pos.Y = marioState.position[1] / MARIO_SCALE;
	update.Pos.Z = marioState.position[2] / MARIO_SCALE;
	update.RelativePos = false;
	if (pluginOptions[PLUGINOPTION_CAMERA].value)
	{
		static float currYaw = 0;
		currYaw = Entities_->List[ENTITIES_SELF_ID]->Yaw;
		float diffYaw = ((-marioState.faceAngle + MATH_PI) * MATH_RAD2DEG) - Entities_->List[ENTITIES_SELF_ID]->Yaw;
		if (abs(diffYaw) >= 180)
		{
			currYaw = diffYaw;
			diffYaw = ((-marioState.faceAngle + MATH_PI) * MATH_RAD2DEG) - currYaw;
		}
		currYaw += diffYaw*0.05f;
		update.Flags |= LOCATIONUPDATE_YAW;
		update.Yaw = ((-marioState.faceAngle + MATH_PI) * MATH_RAD2DEG); // use currYaw for smooth camera (must fix)
		//printf("%.2f %.2f %.2f\n", update.Yaw, Entities_->List[ENTITIES_SELF_ID]->Yaw, diffYaw);
	}
	Entities_->List[ENTITIES_SELF_ID]->VTABLE->SetLocation(Entities_->List[ENTITIES_SELF_ID], &update, false);
	Entities_->List[ENTITIES_SELF_ID]->Velocity.X = Entities_->List[ENTITIES_SELF_ID]->Velocity.Y = Entities_->List[ENTITIES_SELF_ID]->Velocity.Z = 0;

	if (Gui_->InputGrab) // menu is open
	{
		memset(&marioInputs, 0, sizeof(struct SM64MarioInputs));
		return;
	}

	float dir;
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

	marioInputs.stickX = Math_Cos(dir) * spd;
	marioInputs.stickY = Math_Sin(dir) * spd;
	marioInputs.camLookX = (update.Pos.X - Camera_->CurrentPos.X);
	marioInputs.camLookZ = (update.Pos.Z - Camera_->CurrentPos.Z);
	marioInputs.buttonA = KeyBind_IsPressed(KEYBIND_JUMP);
	marioInputs.buttonB = KeyBind_IsPressed(KEYBIND_DELETE_BLOCK);
	marioInputs.buttonZ = KeyBind_IsPressed(KEYBIND_SPEED);
	/*
	Entities_->List[ENTITIES_SELF_ID]->Position.X = marioState.position[0];
	Entities_->List[ENTITIES_SELF_ID]->Position.Y = marioState.position[1];
	Entities_->List[ENTITIES_SELF_ID]->Position.Z = marioState.position[2];
	*/
}

static void Classic64_Init()
{
	LoadSymbolsFromGame();
	memset(&marioIds, -1, sizeof(marioIds));
	memset(&marioObjs, -1, sizeof(marioObjs));
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
	marioTextureUint8 = (uint8_t*)malloc(4 * 1024 * SM64_TEXTURE_HEIGHT);
	marioBitmap.scan0 = (BitmapCol*)malloc(sizeof(BitmapCol) * 1024 * SM64_TEXTURE_HEIGHT); // texture sizes must be power of two

	marioGeometry.position = malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
	marioGeometry.color    = malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
	marioGeometry.normal   = malloc( sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES );
	marioGeometry.uv       = malloc( sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES );

	sm64_global_terminate();
	sm64_global_init(romBuffer, marioTextureUint8, NULL);
	f = fopen("texture.raw", "wb");
	for (int i=0; i<1024 * SM64_TEXTURE_HEIGHT; i++) // copy texture to classicube bitmap
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
	marioVertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, 4 * SM64_GEO_MAX_TRIANGLES);
	marioTexturedVertexID = Gfx_CreateDynamicVb(VERTEX_FORMAT_TEXTURED, 4 * SM64_GEO_MAX_TRIANGLES);

	SendChat("&aSuper Mario 64 US ROM loaded!", NULL, NULL, NULL);

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
	{
		for (int j=0; j<9*3; j++)
		{
			if (marioObjs[i][j] == -1) continue;
			sm64_surface_object_delete(marioObjs[i][j]);
			marioObjs[i][j] = -1;
		}
		if (marioIds[i] == -1) continue;
		sm64_mario_delete(marioIds[i]);
		marioIds[i] = -1;
	}
}

static void Classic64_Reset()
{
	if (!inited) return;
	allowTick = false;
	for (int i=0; i<256; i++)
	{
		for (int j=0; j<9*3; j++)
		{
			if (marioObjs[i][j] == -1) continue;
			sm64_surface_object_delete(marioObjs[i][j]);
			marioObjs[i][j] = -1;
		}
		if (marioIds[i] == -1) continue;
		sm64_mario_delete(marioIds[i]);
		marioIds[i] = -1;
	}
}

static void Classic64_OnNewMap()
{
	if (!inited) return;
	allowTick = false;
	for (int i=0; i<256; i++)
	{
		for (int j=0; j<9*3; j++)
		{
			if (marioObjs[i][j] == -1) continue;
			sm64_surface_object_delete(marioObjs[i][j]);
			marioObjs[i][j] = -1;
		}
		if (marioIds[i] == -1) continue;
		sm64_mario_delete(marioIds[i]);
		marioIds[i] = -1;
	}
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
	LoadSymbol(Entities);
	LoadSymbol(Camera);
	LoadSymbol(Gui);
	LoadSymbol(Lighting);
	LoadSymbol(ChatEvents);
}