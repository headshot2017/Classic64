#ifndef CLASSIC64_H
#define CLASSIC64_H

#define MARIO_SCALE 128.f
#define IMARIO_SCALE 128
#define DEBUGGER_MAX_VERTICES 4096

#include <inttypes.h>

#include "Classic64_settings.h"
#include "libsm64.h"

#include "ClassiCube/Bitmap.h"
#include "ClassiCube/Entity.h"
#include "ClassiCube/Graphics.h"
#include "ClassiCube/Vectors.h"

enum
{
	OPCODE_MARIO_HAS_PLUGIN=1,
	OPCODE_MARIO_TICK,
	OPCODE_MARIO_SET_COLORS,
	OPCODE_MARIO_REQUEST_COLORS,
	OPCODE_MARIO_SET_CAP,
	OPCODE_MARIO_FORCE
};

struct MarioColorUpdate
{
	bool on;
	struct RGBCol newColors[6];
};
extern struct MarioColorUpdate *marioColorUpdates[256];

extern bool serverHasPlugin;
extern GfxResourceID marioTextureID;
extern struct Bitmap marioBitmap;

struct MarioInstance // represents a Mario object in the plugin
{
	int32_t ID;
	uint32_t surfaces[128];
	const struct EntityVTABLE* OriginalVTABLE;
	struct EntityVTABLE marioVTABLE;
	bool customColors;
	struct RGBCol colors[6];

	Vec3 lastPos;
	Vec3 currPos;
	Vec3 lastGeom[3 * SM64_GEO_MAX_TRIANGLES], lastTexturedGeom[3 * SM64_GEO_MAX_TRIANGLES];
	Vec3 currGeom[3 * SM64_GEO_MAX_TRIANGLES], currTexturedGeom[3 * SM64_GEO_MAX_TRIANGLES];

	Vec3 lastPole;
	int lastPoleHeight;

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
};
extern struct MarioInstance *marioInstances[256];

bool isGuiOpen();
void updateMarioColors(int i, bool on, struct RGBCol colors[6]);
void sendMarioColors();

void SendChat(const char* format, const void* arg1, const void* arg2, const void* arg3, const void* arg4);
void OnMarioClientCmd(const cc_string* args, int argsCount);

#endif
