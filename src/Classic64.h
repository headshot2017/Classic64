#ifndef CLASSIC64_H
#define CLASSIC64_H

#define MARIO_SCALE 128.f
#define IMARIO_SCALE 128
#define DEBUGGER_MAX_VERTICES 4096

#include <inttypes.h>

#include "libsm64.h"

#include "ClassiCube/Bitmap.h"
#include "ClassiCube/Graphics.h"
#include "ClassiCube/Vectors.h"

extern GfxResourceID marioTextureID;
static struct Bitmap marioBitmap = {0, 1024, SM64_TEXTURE_HEIGHT};

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
};
extern struct MarioInstance *marioInstances[256];

#endif
