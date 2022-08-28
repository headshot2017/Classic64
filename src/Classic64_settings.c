#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>

#include "Classic64_settings.h"
#include "ClassiCube/Core.h"

const cc_string usageStrings[] = {
	String_FromConst("<number>"),
	String_FromConst("<on/off>"),
	String_FromConst("<red 0/255> <green 0/255> <blue 0/255>"),
};

const struct RGBCol defaultColors[] = {
	{0  , 0  , 255}, // Overalls
	{255, 0  , 0  }, // Shirt/Hat
	{254, 193, 121}, // Skin
	{114, 28 , 14 }, // Hair	
	{255, 255, 255}, // Gloves
	{115, 6  , 0  }, // Shoes
};

struct PluginOption pluginOptions[] = {
	{
		String_FromConst("first_use"),
		{String_FromConst("")},
		1, PLUGINOPTION_VALUE_BOOL, {.on=false}, true
	},
	{
		String_FromConst("hurt"),
		{String_FromConst("&eSet whether Mario can get hurt.")},
		1, PLUGINOPTION_VALUE_BOOL, {.on=false}, false
	},
	{
		String_FromConst("camera"),
		{String_FromConst("&eRotate the camera automatically behind Mario.")},
		1, PLUGINOPTION_VALUE_BOOL, {.on=false}, false
	},
	{
		String_FromConst("bgr"),
		{
			String_FromConst("&eSwap Mario's RGB colors with BGR."),
			String_FromConst("&eThis is off by default."),
			String_FromConst("&eIf Mario's colors appear inverted, change this setting."),
		},
		3, PLUGINOPTION_VALUE_BOOL, {.on=false}, false
	},
	{
		String_FromConst("custom_colors"),
		{String_FromConst("&eWhether to use custom Mario colors.")},
		1, PLUGINOPTION_VALUE_BOOL, {.on=true}, false
	},

	// colors
	{
		String_FromConst("color_overalls"),
		{String_FromConst("&eSet colors for Mario's overalls.")},
		1, PLUGINOPTION_VALUE_RGB, {.col={0,0,255}}, false
	},
	{
		String_FromConst("color_shirthat"),
		{String_FromConst("&eSet colors for Mario's shirt/hat.")},
		1, PLUGINOPTION_VALUE_RGB, {.col={255,0,0}}, false
	},
	{
		String_FromConst("color_skin"),
		{String_FromConst("&eSet colors for Mario's skin.")},
		1, PLUGINOPTION_VALUE_RGB, {.col={254,193,121}}, false
	},
	{
		String_FromConst("color_hair"),
		{String_FromConst("&eSet colors for Mario's hair.")},
		1, PLUGINOPTION_VALUE_RGB, {.col={114,28,14}}, false
	},
	{
		String_FromConst("color_gloves"),
		{String_FromConst("&eSet colors for Mario's gloves.")},
		1, PLUGINOPTION_VALUE_RGB, {.col={255,255,255}}, false
	},
	{
		String_FromConst("color_shoes"),
		{String_FromConst("&eSet colors for Mario's shoes.")},
		1, PLUGINOPTION_VALUE_RGB, {.col={115,6,0}}, false
	},

#ifdef CLASSIC64_DEBUG
	{
		String_FromConst("surface_debugger"),
		{String_FromConst("&eToggle the SM64 surface debugger.")},
		1, PLUGINOPTION_VALUE_BOOL, {.on=false}, false
	}
#endif
};

void saveSettings()
{
	FILE *f = fopen("Classic64.cfg", "w");
	for (int i=0; i<PLUGINOPTIONS_MAX; i++)
	{
		fprintf(f, "%s: ", pluginOptions[i].name.buffer);
		switch(pluginOptions[i].valueType)
		{
			case PLUGINOPTION_VALUE_BOOL:
				fprintf(f, "%s\n", (pluginOptions[i].value.on) ? "on" : "off");
				break;
			case PLUGINOPTION_VALUE_NUMBER:
				fprintf(f, "%d\n", pluginOptions[i].value.num);
				break;
			case PLUGINOPTION_VALUE_RGB:
				fprintf(f, "%d,%d,%d\n", pluginOptions[i].value.col.r, pluginOptions[i].value.col.g, pluginOptions[i].value.col.b);
				break;
		}
	}
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
				switch(pluginOptions[i].valueType)
				{
					case PLUGINOPTION_VALUE_BOOL:
						pluginOptions[i].value.on = (strcmp(value, "on") == 0);
						break;
					case PLUGINOPTION_VALUE_NUMBER:
						pluginOptions[i].value.num = atoi(value);
						break;
					case PLUGINOPTION_VALUE_RGB:
						{
							char *r = strtok(value, ",");
							char *g = strtok(NULL, ",");
							char *b = strtok(NULL, ",");
							char *ptr;
							pluginOptions[i].value.col = (struct RGBCol){strtoul(r, &ptr, 10), strtoul(g, &ptr, 10), strtoul(b, &ptr, 10)};
						}
						break;
				}
				continue;
			}
		}
	}
}
