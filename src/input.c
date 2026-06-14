#include "input.h"

static const UBYTE levelWave[64] = {
    32, 35, 38, 41, 44, 47, 50, 53,
    55, 58, 60, 61, 63, 64, 64, 64,
    64, 64, 63, 61, 60, 58, 55, 53,
    50, 47, 44, 41, 38, 35, 32, 29,
    26, 23, 20, 17, 14, 11,  9,  6,
     4,  3,  1,  0,  0,  0,  0,  0,
     1,  3,  4,  6,  9, 11, 14, 17,
    20, 23, 26, 29, 32, 32, 32, 32
};

static UWORD clamp100(UWORD value)
{
    return value > 100 ? 100 : value;
}

static UWORD prng(struct InputSource *input)
{
    input->seed = (UWORD)((input->seed * 25173U) + 13849U);
    return input->seed;
}

static UWORD waveAt(ULONG frame, UWORD step, UWORD phase)
{
    return levelWave[((frame * step) + phase) & 63U];
}

void Input_Init(struct InputSource *input)
{
    input->frame = 0;
    input->seed = 0x4d4c;
    input->lastBass = 0;
}

void Input_UpdateDemo(struct InputSource *input, struct AudioFeatures *features)
{
    UWORD noise;
    UWORD bass;
    UWORD mid;
    UWORD treble;

    input->frame++;
    noise = (UWORD)(prng(input) & 15U);

    bass = clamp100((UWORD)(20 + waveAt(input->frame, 1, 0) + (noise >> 1)));
    mid = clamp100((UWORD)(14 + waveAt(input->frame, 3, 11) + (noise >> 2)));
    treble = clamp100((UWORD)(10 + waveAt(input->frame, 7, 29) + noise));

    features->bass = bass;
    features->mid = mid;
    features->treble = treble;
    features->left = clamp100((UWORD)((bass + mid + waveAt(input->frame, 2, 7)) / 2));
    features->right = clamp100((UWORD)((bass + treble + waveAt(input->frame, 5, 19)) / 2));
    features->onset = (bass > input->lastBass + 18U) ? 100U : 0U;
    features->pitch = (UWORD)(80 + (waveAt(input->frame, 2, 3) * 5));
    features->frame = input->frame;

    input->lastBass = bass;
}

