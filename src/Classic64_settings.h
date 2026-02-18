#ifndef CLASSIC64_SETTINGS_H
#define CLASSIC64_SETTINGS_H

#include <stdbool.h>

#include "../ClassiCube/src/String_.h"

enum
{
	PLUGINOPTION_FIRST_USE,
	PLUGINOPTION_HURT,
	PLUGINOPTION_CAMERA,
	PLUGINOPTION_BGR,
	PLUGINOPTION_INTERP,
	PLUGINOPTION_KEY_CROUCH,
	PLUGINOPTION_KEY_ATTACK,
	PLUGINOPTION_CUSTOM_COLORS,
	PLUGINOPTION_COLOR_OVERALLS,
	PLUGINOPTION_COLOR_SHIRT_HAT,
	PLUGINOPTION_COLOR_SKIN,
	PLUGINOPTION_COLOR_HAIR,
	PLUGINOPTION_COLOR_GLOVES,
	PLUGINOPTION_COLOR_SHOES,
	PLUGINOPTION_VOLUME,
#ifdef CLASSIC64_DEBUG
	PLUGINOPTION_SURFACE_DEBUGGER,
#endif
	PLUGINOPTIONS_MAX,

	PLUGINOPTION_VALUE_NUMBER=0,
	PLUGINOPTION_VALUE_STRING,
	PLUGINOPTION_VALUE_BOOL,
	PLUGINOPTION_VALUE_RGB
};

struct RGBCol
{
	uint8_t r, g, b;
};

extern const char* keyNames[];
extern const cc_string usageStrings[];
extern const struct RGBCol defaultColors[];

struct PluginOption {
	cc_string name;
	cc_string desc[3];
	int descLines;
	int valueType;
	union ValueUnion
	{
		struct numValue {
			int current, min, max;
		} num;
		struct strValue {
			char current[64];
			const char **validList;
			int validMax;
		} str;
		bool on;
		struct RGBCol col;
	} value;
	bool hidden;
	void (*onChange)(int option);
};

extern struct PluginOption pluginOptions[];

void saveSettings();
void loadSettings();

#endif
