#ifndef VISUAL_AUDIO_INPUT_H
#define VISUAL_AUDIO_INPUT_H

#include <exec/types.h>

struct AudioFeatures {
    UWORD bass;
    UWORD mid;
    UWORD treble;
    UWORD left;
    UWORD right;
    UWORD onset;
    UWORD pitch;
    ULONG frame;
};

struct InputSource {
    ULONG frame;
    UWORD seed;
    UWORD lastBass;
};

void Input_Init(struct InputSource *input);
void Input_UpdateDemo(struct InputSource *input, struct AudioFeatures *features);

#endif

