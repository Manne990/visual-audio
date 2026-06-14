#ifndef VISUAL_AUDIO_MUSIC_H
#define VISUAL_AUDIO_MUSIC_H

#include <exec/types.h>

struct MusicModule {
    UBYTE *bytes;
    LONG size;
    UBYTE songLength;
    UBYTE maxPattern;
    LONG patternDataEnd;
};

struct AudioFeatures;

void Music_Init(void);
void Music_Shutdown(void);
struct MusicModule *Music_LoadModule(const char *path);
void Music_FreeModule(struct MusicModule *module);
BOOL Music_PlayModule(struct MusicModule *module);
void Music_Stop(void);
BOOL Music_IsPlaying(void);
void Music_UpdateFeatures(struct AudioFeatures *features);

#endif
