#ifndef VISUAL_AUDIO_VISUAL_H
#define VISUAL_AUDIO_VISUAL_H

#include <exec/types.h>
#include <intuition/intuition.h>

#include "input.h"

#define VISUAL_PARTICLE_COUNT 48
#define VISUAL_ROW_COUNT      24
#define VISUAL_COLUMN_COUNT   36

enum VisualMode {
    VISUAL_MODE_RADIAL_BRUSH = 0,
    VISUAL_MODE_DENSE_RASTER,
    VISUAL_MODE_WIREFRAME,
    VISUAL_MODE_PARTICLES,
    VISUAL_MODE_WATERFALL,
    VISUAL_MODE_DARK_DROP,
    VISUAL_MODE_COUNT
};

struct VisualParticle {
    WORD x;
    WORD y;
    WORD dx;
    WORD dy;
    UBYTE life;
    UBYTE pen;
    UBYTE shape;
};

struct VisualColumn {
    UWORD seed;
    WORD offset;
    UBYTE phase;
    UBYTE pen;
};

struct VisualState {
    UWORD hue;
    UWORD orbit;
    UWORD drift;
    UWORD scene;
    UWORD mode;
    UWORD modeAge;
    UWORD beatCount;
    UWORD burst;
    UWORD lastOnset;
    UWORD blanker;
    UWORD brushPhase;
    UWORD prng;
    UWORD rowSeed[VISUAL_ROW_COUNT];
    struct VisualParticle particles[VISUAL_PARTICLE_COUNT];
    struct VisualColumn columns[VISUAL_COLUMN_COUNT];
    UWORD frozen;
};

void Visual_Init(struct VisualState *visual);
void Visual_Render(struct Window *window,
                   struct VisualState *visual,
                   const struct AudioFeatures *features);
void Visual_ToggleFreeze(struct VisualState *visual);
void Visual_NextScene(struct VisualState *visual);

#endif
