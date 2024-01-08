#include "play_sound.h"

#include "decomp/audio/external.h"
#include "debug_print.h"
#include "load_audio_data.h"

SM64PlaySoundFunctionPtr g_play_sound_func = NULL;

extern void play_sound( uint32_t soundBits, f32 *pos ) {
    if (pos[0] == -1 && pos[1] == -1 && pos[2] == -1)
        return; // Classic64: dirty hack for non-local players

    if ( g_is_audio_initialized ) {
        DEBUG_PRINT("$ play_sound(%d) request %d; pos %f %f %f\n", soundBits,sSoundRequestCount,pos[0],pos[1],pos[2]);
        sSoundRequests[sSoundRequestCount].soundBits = soundBits;
        sSoundRequests[sSoundRequestCount].position = pos;
        sSoundRequestCount++;
    }

    if ( g_play_sound_func ) {
        g_play_sound_func(soundBits, pos);
    }
}