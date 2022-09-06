#include "level.h"
#include "../src/decomp/include/surface_terrains.h"
const struct SM64Surface surfaces[] = {
// bottom of block
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{0, 0, 256}, {0, 0, 0}, {256, 0, 256}}}, // bottom left, top right, top left
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{256, 0, 0}, {256, 0, 256}, {0, 0, 0}}}, // top right, bottom left, bottom right

// left (Z+)
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{0, 0, 256}, {256, 256, 256}, {0, 256, 256}}}, // bottom left, top right, top left
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{256, 256, 256}, {0, 0, 256}, {256, 0, 256}}}, // top right, bottom left, bottom right

// right (Z-)
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{256, 0, 0}, {0, 256, 0}, {256, 256, 0}}}, // bottom left, top right, top left
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{0, 256, 0}, {256, 0, 0}, {0, 0, 0}}}, // top right, bottom left, bottom right

// back (X+)
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{256, 0, 0}, {256, 256, 0}, {256, 256, 256}}}, // bottom left, top right, top left
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{256, 256, 256}, {256, 0, 256}, {256, 0, 0}}}, // top right, bottom left, bottom right

// front (X-)
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{0, 0, 256}, {0, 256, 256}, {0, 256, 0}}}, // bottom left, top right, top left
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{0, 256, 0}, {0, 0, 0}, {0, 0, 256}}}, // top right, bottom left, bottom right

// top of block
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{256, 256, 256}, {0, 256, 0}, {0, 256, 256}}}, // bottom left, top right, top left
{SURFACE_DEFAULT,0,TERRAIN_STONE,{{0, 256, 0}, {256, 256, 256}, {256, 256, 0}}}, // top right, bottom left, bottom right
};
const size_t surfaces_count = sizeof( surfaces ) / sizeof( surfaces[0] );
const int32_t spawn[] = {0, 256, 0};
