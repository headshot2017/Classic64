#ifndef SURFACE_TYPES_H
#define SURFACE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

struct SM64Surface
{
    int16_t type;
    int16_t force;
    uint16_t terrain;
    int32_t vertices[3][3];
};

struct SM64ObjectTransform
{
    float position[3];
    float eulerRotation[3];
};

struct SM64SurfaceObject
{
    struct SM64ObjectTransform transform;
    uint32_t surfaceCount;
    struct SM64Surface *surfaces;
};

#endif