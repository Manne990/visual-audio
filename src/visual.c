#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "visual.h"

#define VISUAL_RAY_COUNT 16

static const WORD rayX[VISUAL_RAY_COUNT] = {
      0,  49,  90, 118, 128, 118,  90,  49,
      0, -49, -90,-118,-128,-118, -90, -49
};

static const WORD rayY[VISUAL_RAY_COUNT] = {
   -128,-118, -90, -49,   0,  49,  90, 118,
    128, 118,  90,  49,   0, -49, -90,-118
};

static WORD innerLeft(const struct Window *window)
{
    return window->BorderLeft;
}

static WORD innerTop(const struct Window *window)
{
    return window->BorderTop;
}

static WORD innerWidth(const struct Window *window)
{
    return (WORD)(window->Width - window->BorderLeft - window->BorderRight);
}

static WORD innerHeight(const struct Window *window)
{
    return (WORD)(window->Height - window->BorderTop - window->BorderBottom);
}

static WORD scaleTo(WORD value, WORD maxValue)
{
    return (WORD)((LONG)value * maxValue / 100L);
}

static UWORD average3(UWORD a, UWORD b, UWORD c)
{
    return (UWORD)(((ULONG)a + (ULONG)b + (ULONG)c) / 3UL);
}

static WORD absWord(WORD value)
{
    return value < 0 ? (WORD)-value : value;
}

static WORD wrapNorm(WORD value)
{
    return (WORD)(((UWORD)value) & 1023U);
}

static WORD centerWave(UWORD phase, WORD amplitude)
{
    UWORD p;
    WORD value;

    p = (UWORD)(phase & 255U);
    if (p > 127U) {
        p = (UWORD)(255U - p);
    }

    value = (WORD)p - 64;
    return (WORD)(((LONG)value * amplitude) / 64L);
}

static UWORD nextRandom(struct VisualState *visual)
{
    visual->prng = (UWORD)((visual->prng * 25173U) + 13849U);
    return visual->prng;
}

static UBYTE brightPen(UWORD phase)
{
    return (UBYTE)(2U + ((phase >> 4) & 1U));
}

static UBYTE dimPen(UWORD phase)
{
    return (UBYTE)(1U + ((phase >> 5) & 1U));
}

static UBYTE colorDepth(const struct RastPort *rp)
{
    if (rp == NULL || rp->BitMap == NULL) {
        return 2;
    }
    return rp->BitMap->Depth;
}

static UBYTE penLimit(const struct RastPort *rp)
{
    UBYTE depth;

    depth = colorDepth(rp);
    if (depth >= 5) {
        return 31;
    }
    if (depth >= 4) {
        return 15;
    }
    if (depth >= 3) {
        return 7;
    }
    return 3;
}

static UBYTE colorPen(const struct RastPort *rp, UWORD phase)
{
    UBYTE limit;

    limit = penLimit(rp);
    if (limit <= 3) {
        return brightPen(phase);
    }

    return (UBYTE)(2U + (phase % (UWORD)(limit - 1U)));
}

static UBYTE accentPen(const struct RastPort *rp, UWORD phase)
{
    UBYTE limit;

    limit = penLimit(rp);
    if (limit <= 3) {
        return dimPen(phase);
    }

    return (UBYTE)(4U + ((phase * 3U) % (UWORD)(limit - 3U)));
}

static void clearArea(struct RastPort *rp, WORD x0, WORD y0, WORD w, WORD h)
{
    SetAPen(rp, 0);
    RectFill(rp, x0, y0, (WORD)(x0 + w - 1), (WORD)(y0 + h - 1));
}

static void drawRectOutline(struct RastPort *rp,
                            WORD x1,
                            WORD y1,
                            WORD x2,
                            WORD y2,
                            UBYTE pen)
{
    SetAPen(rp, pen);
    Move(rp, x1, y1);
    Draw(rp, x2, y1);
    Draw(rp, x2, y2);
    Draw(rp, x1, y2);
    Draw(rp, x1, y1);
}

static void drawDiamond(struct RastPort *rp,
                        WORD cx,
                        WORD cy,
                        WORD radius,
                        UBYTE pen)
{
    SetAPen(rp, pen);
    Move(rp, cx, (WORD)(cy - radius));
    Draw(rp, (WORD)(cx + radius), cy);
    Draw(rp, cx, (WORD)(cy + radius));
    Draw(rp, (WORD)(cx - radius), cy);
    Draw(rp, cx, (WORD)(cy - radius));
}

static void fillDiamond(struct RastPort *rp,
                        WORD cx,
                        WORD cy,
                        WORD radius,
                        UBYTE pen)
{
    WORD y;
    WORD span;

    if (radius < 1) {
        return;
    }

    SetAPen(rp, pen);
    for (y = (WORD)-radius; y <= radius; y++) {
        span = (WORD)(radius - absWord(y));
        RectFill(rp,
                 (WORD)(cx - span),
                 (WORD)(cy + y),
                 (WORD)(cx + span),
                 (WORD)(cy + y));
    }
}

static void drawBrushStamp(struct RastPort *rp,
                           WORD cx,
                           WORD cy,
                           WORD size,
                           UBYTE pen,
                           UWORD phase)
{
    WORD small;

    if (size < 2) {
        size = 2;
    } else if (size > 13) {
        size = 13;
    }
    small = (WORD)(size / 2);

    SetAPen(rp, pen);
    RectFill(rp, (WORD)(cx - size), (WORD)(cy - 1),
             (WORD)(cx + size), (WORD)(cy + 1));
    RectFill(rp, (WORD)(cx - 1), (WORD)(cy - size),
             (WORD)(cx + 1), (WORD)(cy + size));

    if ((phase & 1U) == 0U) {
        drawDiamond(rp, cx, cy, small,
                    colorPen(rp, (UWORD)(phase + 37U)));
    } else {
        RectFill(rp, (WORD)(cx - small), (WORD)(cy - small),
                 (WORD)(cx + small), (WORD)(cy + small));
    }
}

static void eraseTrailBands(struct RastPort *rp,
                            WORD x0,
                            WORD y0,
                            WORD w,
                            WORD h,
                            const struct VisualState *visual,
                            UWORD strength)
{
    WORD i;
    WORD y;
    WORD x;
    WORD bandCount;

    bandCount = (WORD)(2 + strength);
    SetAPen(rp, 0);

    for (i = 0; i < bandCount; i++) {
        y = (WORD)(y0 + (((LONG)(visual->drift + i * 53) * h) / 255L));
        RectFill(rp, x0, y, (WORD)(x0 + w - 1), (WORD)(y + 1));
    }

    if ((visual->modeAge & 3U) == 0U) {
        for (i = 0; i < 3; i++) {
            x = (WORD)(x0 + (((LONG)(visual->orbit + i * 79) * w) / 255L));
            RectFill(rp, x, y0, x, (WORD)(y0 + h - 1));
        }
    }
}

static void setMode(struct VisualState *visual, UWORD mode)
{
    visual->mode = (UWORD)(mode % VISUAL_MODE_COUNT);
    visual->scene = visual->mode;
    visual->modeAge = 0;
    visual->blanker = 5;
    visual->burst = 100;
}

static void resetParticle(struct VisualState *visual,
                          struct VisualParticle *particle,
                          WORD centerX,
                          WORD centerY,
                          UWORD energy)
{
    UWORD r;

    r = nextRandom(visual);
    if (energy > 45U) {
        particle->x = wrapNorm((WORD)(centerX + (WORD)(r & 255U) - 128));
        particle->y = wrapNorm((WORD)(centerY + (WORD)((r >> 8) & 255U) - 128));
    } else {
        particle->x = (WORD)(r & 1023U);
        particle->y = (WORD)((nextRandom(visual)) & 1023U);
    }

    r = nextRandom(visual);
    particle->dx = (WORD)(((WORD)(r & 31U)) - 15);
    particle->dy = (WORD)(((WORD)((r >> 5) & 31U)) - 15);
    if (particle->dx == 0) {
        particle->dx = 3;
    }
    if (particle->dy == 0) {
        particle->dy = -2;
    }
    particle->life = (UBYTE)(24U + (r & 31U));
    particle->pen = (UBYTE)(r & 31U);
    particle->shape = (UBYTE)((r >> 9) & 3U);
}

static void reseedRasterRows(struct VisualState *visual)
{
    WORD i;

    for (i = 0; i < VISUAL_ROW_COUNT; i += 3) {
        visual->rowSeed[(i + visual->beatCount) % VISUAL_ROW_COUNT] =
            nextRandom(visual);
    }
}

static void updateTiming(struct VisualState *visual,
                         const struct AudioFeatures *features)
{
    UWORD energy;
    BOOL onset;

    energy = average3(features->bass, features->mid, features->treble);
    onset = (features->onset > 68U && visual->lastOnset <= 68U);

    if (onset) {
        visual->beatCount = (UWORD)(visual->beatCount + 1U);
        if ((visual->beatCount & 7U) == 0U ||
                (visual->modeAge > 420U && features->onset > 80U)) {
            setMode(visual, (UWORD)(visual->mode + 1U));
        } else if (visual->mode == VISUAL_MODE_DENSE_RASTER) {
            reseedRasterRows(visual);
        }
    }
    visual->lastOnset = features->onset;

    if (features->onset > visual->burst) {
        visual->burst = features->onset;
    } else {
        visual->burst = (UWORD)(((ULONG)visual->burst * 72UL) / 100UL);
    }

    visual->hue = (UWORD)((visual->hue + 1U + (features->treble >> 5)) & 255U);
    visual->orbit = (UWORD)((visual->orbit + 2U + (features->bass >> 4)) & 255U);
    visual->drift = (UWORD)((visual->drift + 1U + (energy >> 5)) & 255U);
    visual->brushPhase =
        (UWORD)((visual->brushPhase + 2U + (features->treble >> 4)) & 255U);
    visual->modeAge = (UWORD)(visual->modeAge + 1U);
}

static void drawDropBlanker(struct RastPort *rp,
                            WORD x0,
                            WORD y0,
                            WORD w,
                            WORD h,
                            struct VisualState *visual)
{
    WORD y;

    clearArea(rp, x0, y0, w, h);
    if (visual->blanker <= 2U) {
        y = (WORD)(y0 + (((LONG)(visual->orbit & 255U) * h) / 255L));
        SetAPen(rp, colorPen(rp, visual->hue));
        RectFill(rp, x0, y, (WORD)(x0 + w - 1), (WORD)(y + 2));
    }
    visual->blanker = (UWORD)(visual->blanker - 1U);
}

static void renderRadialBrush(struct RastPort *rp,
                              WORD x0,
                              WORD y0,
                              WORD w,
                              WORD h,
                              struct VisualState *visual,
                              const struct AudioFeatures *features)
{
    WORD centerX;
    WORD centerY;
    WORD maxRadius;
    WORD radius;
    WORD stamps;
    WORD i;
    WORD index;
    WORD pathRadius;
    WORD x;
    WORD y;
    WORD size;
    WORD stereo;
    UBYTE pen;

    if (visual->modeAge < 2U) {
        clearArea(rp, x0, y0, w, h);
    } else {
        eraseTrailBands(rp, x0, y0, w, h, visual, 2);
    }

    stereo = (WORD)(((WORD)features->right - (WORD)features->left) / 4);
    centerX = (WORD)(x0 + (w / 2) + stereo);
    centerY = (WORD)(y0 + (h / 2) -
                     scaleTo((WORD)features->treble, (WORD)(h / 10)));
    maxRadius = (w < h) ? (WORD)(w / 2) : (WORD)(h / 2);
    radius = (WORD)(maxRadius / 3 + scaleTo((WORD)features->bass,
                                            (WORD)(maxRadius / 3)));
    stamps = (WORD)(14 + (features->mid / 10U));
    if (stamps > 24) {
        stamps = 24;
    }

    for (i = 0; i < stamps; i++) {
        index = (WORD)((i + (visual->brushPhase >> 4)) & 15);
        pathRadius = (WORD)(radius +
                     centerWave((UWORD)(visual->brushPhase + i * 17),
                                (WORD)(maxRadius / 4)));
        x = (WORD)(centerX + (((LONG)rayX[index] * pathRadius) / 128L) +
                  centerWave((UWORD)(visual->drift + i * 11), (WORD)(w / 7)));
        y = (WORD)(centerY + (((LONG)rayY[index] * pathRadius) / 128L) +
                  centerWave((UWORD)(visual->orbit + i * 19), (WORD)(h / 9)));
        size = (WORD)(3 + (features->bass / 18U) + ((i + visual->burst) & 3U));
        pen = colorPen(rp, (UWORD)(visual->hue + i * 9));
        drawBrushStamp(rp, x, y, size, pen, (UWORD)(i + visual->hue));

        if ((i & 3) == 0) {
            drawBrushStamp(rp,
                           (WORD)(x0 + w - (x - x0)),
                           y,
                           (WORD)(size - 1),
                           accentPen(rp, (UWORD)(visual->hue + i * 5)),
                           (UWORD)(i + 1));
        }
    }

    drawDiamond(rp, centerX, centerY,
                (WORD)(8 + (features->mid / 5U)), 1);
}

static void renderDenseRaster(struct RastPort *rp,
                              WORD x0,
                              WORD y0,
                              WORD w,
                              WORD h,
                              struct VisualState *visual,
                              const struct AudioFeatures *features)
{
    WORD row;
    WORD dash;
    WORD segments;
    WORD y;
    WORD x;
    WORD width;
    WORD height;
    WORD maskRadius;
    UWORD seed;
    UWORD energy;
    UBYTE pen;

    energy = average3(features->bass, features->mid, features->treble);
    if (visual->modeAge < 2U || (features->onset > 85U && visual->burst > 85U)) {
        clearArea(rp, x0, y0, w, h);
    } else {
        eraseTrailBands(rp, x0, y0, w, h, visual, 1);
    }

    segments = (WORD)(6 + (energy / 7U));
    if (segments > 22) {
        segments = 22;
    }
    height = (WORD)(1 + (features->mid / 45U));

    for (row = 0; row < VISUAL_ROW_COUNT; row++) {
        seed = (UWORD)(visual->rowSeed[row] + visual->modeAge * 37U);
        y = (WORD)(y0 + (((LONG)row * h) / VISUAL_ROW_COUNT) +
                  centerWave((UWORD)(visual->drift + row * 13), 3));
        for (dash = 0; dash < segments; dash++) {
            seed = (UWORD)((seed * 25173U) + 13849U);
            x = (WORD)(x0 + ((LONG)(seed & 1023U) * w) / 1024L);
            width = (WORD)(2 + ((seed >> 6) & 7U) + (features->treble / 28U));
            pen = colorPen(rp, (UWORD)(seed + visual->hue + row * 5));
            SetAPen(rp, pen);
            RectFill(rp, x, y, (WORD)(x + width), (WORD)(y + height));

            if ((seed & 7U) == 0U) {
                SetAPen(rp, accentPen(rp, seed));
                RectFill(rp, x, y, (WORD)(x + 1),
                         (WORD)(y + 2 + (features->treble / 20U)));
            }
            SetAPen(rp, pen);
        }
        visual->rowSeed[row] = seed;
    }

    maskRadius = (WORD)(12 + (features->bass / 3U));
    fillDiamond(rp,
                (WORD)(x0 + (w / 2) +
                       centerWave(visual->orbit, (WORD)(w / 5))),
                (WORD)(y0 + (h / 2) +
                       centerWave(visual->drift, (WORD)(h / 6))),
                maskRadius,
                0);
}

static void renderWireframe(struct RastPort *rp,
                            WORD x0,
                            WORD y0,
                            WORD w,
                            WORD h,
                            struct VisualState *visual,
                            const struct AudioFeatures *features)
{
    WORD centerX;
    WORD centerY;
    WORD i;
    WORD radius;
    WORD index;
    WORD x;
    WORD y;
    WORD x2;
    WORD y2;
    UBYTE pen;

    if (visual->modeAge < 2U) {
        clearArea(rp, x0, y0, w, h);
    } else {
        eraseTrailBands(rp, x0, y0, w, h, visual, 3);
    }

    centerX = (WORD)(x0 + (w / 2) +
                     (((WORD)features->right - (WORD)features->left) / 3));
    centerY = (WORD)(y0 + (h / 2));
    radius = (WORD)(20 + scaleTo((WORD)features->bass, (WORD)(w / 3)));

    for (i = 0; i < VISUAL_RAY_COUNT; i++) {
        index = (WORD)((i + (visual->orbit >> 5)) & 15);
        x = (WORD)(centerX + (((LONG)rayX[index] *
             (radius + centerWave((UWORD)(visual->drift + i * 17), 22))) /
             128L));
        y = (WORD)(centerY + (((LONG)rayY[index] *
             (radius + centerWave((UWORD)(visual->hue + i * 11), 18))) /
             128L));
        x2 = (WORD)(centerX + (((LONG)rayX[(index + 3) & 15] *
              (radius / 2 + features->treble)) / 128L));
        y2 = (WORD)(centerY + (((LONG)rayY[(index + 3) & 15] *
              (radius / 2 + features->mid)) / 128L));
        pen = colorPen(rp, (UWORD)(visual->hue + i * 13));
        SetAPen(rp, pen);
        Move(rp, x, y);
        Draw(rp, x2, y2);
        if ((i & 3) == 0) {
            drawDiamond(rp, x, y, (WORD)(5 + (features->mid / 14U)), pen);
        }
    }

    for (i = 0; i < 4; i++) {
        radius = (WORD)(12 + i * 18 + (features->bass / 4U));
        drawDiamond(rp,
                    (WORD)(centerX + centerWave((UWORD)(visual->orbit + i * 29),
                                                (WORD)(w / 7))),
                    (WORD)(centerY + centerWave((UWORD)(visual->drift + i * 31),
                                                (WORD)(h / 8))),
                    radius,
                    accentPen(rp, (UWORD)(visual->hue + i * 19)));
    }

    drawRectOutline(rp,
                    (WORD)(x0 + 8 + centerWave(visual->drift, 18)),
                    (WORD)(y0 + 8 + centerWave(visual->orbit, 10)),
                    (WORD)(x0 + w - 10 + centerWave(visual->hue, 14)),
                    (WORD)(y0 + h - 10 + centerWave(visual->drift, 9)),
                    accentPen(rp, visual->hue));

    if (visual->burst > 30U) {
        fillDiamond(rp,
                    (WORD)(centerX + centerWave(visual->hue, (WORD)(w / 6))),
                    (WORD)(centerY + centerWave(visual->drift, (WORD)(h / 6))),
                    (WORD)(8 + (visual->burst / 6U)),
                    0);
    }
}

static void updateParticles(struct VisualState *visual,
                            const struct AudioFeatures *features)
{
    WORD i;
    WORD centerX;
    WORD centerY;
    WORD stereo;
    UWORD energy;

    energy = average3(features->bass, features->mid, features->treble);
    stereo = (WORD)(((WORD)features->right - (WORD)features->left) / 5);
    centerX = (WORD)(512 + stereo * 3);
    centerY = (WORD)(512 - ((WORD)features->treble * 2));

    for (i = 0; i < VISUAL_PARTICLE_COUNT; i++) {
        if (visual->particles[i].life == 0U ||
                (features->onset > 75U && i < 8)) {
            resetParticle(visual, &visual->particles[i],
                          centerX, centerY, energy);
        } else {
            visual->particles[i].x =
                wrapNorm((WORD)(visual->particles[i].x +
                         visual->particles[i].dx + stereo));
            visual->particles[i].y =
                wrapNorm((WORD)(visual->particles[i].y +
                         visual->particles[i].dy + 2));
            visual->particles[i].life--;
        }
    }
}

static void renderParticles(struct RastPort *rp,
                            WORD x0,
                            WORD y0,
                            WORD w,
                            WORD h,
                            struct VisualState *visual,
                            const struct AudioFeatures *features)
{
    WORD i;
    WORD x;
    WORD y;
    WORD size;
    struct VisualParticle *particle;

    if (visual->modeAge < 2U) {
        clearArea(rp, x0, y0, w, h);
    } else {
        eraseTrailBands(rp, x0, y0, w, h, visual, 2);
    }

    updateParticles(visual, features);

    for (i = 0; i < VISUAL_PARTICLE_COUNT; i++) {
        particle = &visual->particles[i];
        x = (WORD)(x0 + (((LONG)particle->x * w) / 1024L));
        y = (WORD)(y0 + (((LONG)particle->y * h) / 1024L));
        size = (WORD)(1 + (features->treble / 45U) + (particle->shape & 1U));
        SetAPen(rp, colorPen(rp,
                             (UWORD)(visual->hue + particle->pen * 7U)));
        if ((particle->shape & 2U) != 0U) {
            RectFill(rp, (WORD)(x - size), y, (WORD)(x + size), y);
        } else {
            RectFill(rp, x, y, (WORD)(x + size), (WORD)(y + size));
        }
    }

    if (features->onset > 40U) {
        for (i = 0; i < VISUAL_RAY_COUNT; i += 2) {
            SetAPen(rp, colorPen(rp, (UWORD)(visual->hue + i * 11)));
            Move(rp, (WORD)(x0 + w / 2), (WORD)(y0 + h / 2));
            Draw(rp,
                 (WORD)(x0 + w / 2 +
                        (((LONG)rayX[i] * (features->onset + 20U)) / 128L)),
                 (WORD)(y0 + h / 2 +
                        (((LONG)rayY[i] * (features->onset + 20U)) / 128L)));
        }
    }
}

static void renderWaterfall(struct RastPort *rp,
                            WORD x0,
                            WORD y0,
                            WORD w,
                            WORD h,
                            struct VisualState *visual,
                            const struct AudioFeatures *features)
{
    WORD i;
    WORD x;
    WORD y;
    WORD endY;
    WORD dash;
    WORD gap;
    WORD thickness;
    WORD columnHeight;
    WORD lowerEdge;
    WORD phaseSpan;
    struct VisualColumn *column;

    if (visual->modeAge < 2U) {
        clearArea(rp, x0, y0, w, h);
    } else {
        eraseTrailBands(rp, x0, y0, w, h, visual, 1);
    }

    lowerEdge = (WORD)(y0 + h - 4 -
                scaleTo((WORD)features->treble, (WORD)(h / 5)) +
                centerWave(visual->drift, (WORD)(h / 10)));
    phaseSpan = (WORD)(h / 3 + 1);
    if (phaseSpan < 1) {
        phaseSpan = 1;
    }

    for (i = 0; i < VISUAL_COLUMN_COUNT; i++) {
        column = &visual->columns[i];
        column->phase = (UBYTE)(column->phase + 1U + (features->bass >> 5));
        x = (WORD)(x0 + (((LONG)i * w) / VISUAL_COLUMN_COUNT) +
                  column->offset +
                  centerWave((UWORD)(visual->orbit + column->seed), 5));
        columnHeight = (WORD)(h / 4 + (column->seed & 31U) +
                       scaleTo((WORD)features->bass, (WORD)(h / 2)));
        y = (WORD)(y0 + ((column->phase + (column->seed & 63U)) %
                         phaseSpan));
        endY = (WORD)(y + columnHeight);
        if (endY > lowerEdge) {
            endY = lowerEdge;
        }

        dash = (WORD)(2 + (features->treble / 35U) + (column->seed & 1U));
        gap = (WORD)(3 + ((column->seed >> 3) & 3U));
        thickness = (WORD)(1 + (features->mid / 55U));
        SetAPen(rp, colorPen(rp,
                             (UWORD)(visual->hue + column->pen * 5U)));
        while (y < endY) {
            RectFill(rp, x, y, (WORD)(x + thickness), (WORD)(y + dash));
            y = (WORD)(y + dash + gap);
        }

        if (features->onset > 80U && (i & 7) == (visual->beatCount & 7U)) {
            column->seed = nextRandom(visual);
            column->pen = (UBYTE)(column->seed & 31U);
        }
    }
}

static void renderDarkDrop(struct RastPort *rp,
                           WORD x0,
                           WORD y0,
                           WORD w,
                           WORD h,
                           struct VisualState *visual,
                           const struct AudioFeatures *features)
{
    WORD i;
    WORD y;
    WORD x;

    clearArea(rp, x0, y0, w, h);

    if ((visual->modeAge & 7U) == 0U || features->onset > 60U) {
        for (i = 0; i < 5; i++) {
            y = (WORD)(y0 + (((LONG)(visual->drift + i * 47) * h) / 255L));
            x = (WORD)(x0 + (((LONG)(visual->orbit + i * 61) * w) / 255L));
            SetAPen(rp, accentPen(rp, (UWORD)(visual->hue + i * 17)));
            RectFill(rp, x, y, (WORD)(x + 24 + features->treble / 3U), y);
        }
    }

    if (visual->modeAge > 75U && features->onset > 55U) {
        setMode(visual, VISUAL_MODE_RADIAL_BRUSH);
    }
}

void Visual_Init(struct VisualState *visual)
{
    WORD i;

    visual->hue = 0;
    visual->orbit = 0;
    visual->drift = 0;
    visual->scene = 0;
    visual->mode = VISUAL_MODE_RADIAL_BRUSH;
    visual->modeAge = 0;
    visual->beatCount = 0;
    visual->burst = 0;
    visual->lastOnset = 0;
    visual->blanker = 0;
    visual->brushPhase = 0;
    visual->prng = 0x4d1a;
    visual->frozen = 0;

    for (i = 0; i < VISUAL_ROW_COUNT; i++) {
        visual->rowSeed[i] = nextRandom(visual);
    }

    for (i = 0; i < VISUAL_PARTICLE_COUNT; i++) {
        resetParticle(visual, &visual->particles[i], 512, 512, 0);
    }

    for (i = 0; i < VISUAL_COLUMN_COUNT; i++) {
        visual->columns[i].seed = nextRandom(visual);
        visual->columns[i].offset =
            (WORD)(((WORD)(visual->columns[i].seed & 15U)) - 7);
        visual->columns[i].phase = (UBYTE)(visual->columns[i].seed & 63U);
        visual->columns[i].pen = (UBYTE)(visual->columns[i].seed & 31U);
    }
}

void Visual_ToggleFreeze(struct VisualState *visual)
{
    visual->frozen = visual->frozen ? 0U : 1U;
}

void Visual_NextScene(struct VisualState *visual)
{
    setMode(visual, (UWORD)(visual->mode + 1U));
}

void Visual_Render(struct Window *window,
                   struct VisualState *visual,
                   const struct AudioFeatures *features)
{
    struct RastPort *rp;
    WORD x0;
    WORD y0;
    WORD w;
    WORD h;
    WORD visualH;

    if (visual->frozen) {
        return;
    }

    rp = window->RPort;
    x0 = innerLeft(window);
    y0 = innerTop(window);
    w = innerWidth(window);
    h = innerHeight(window);
    visualH = (h > 24) ? (WORD)(h - 18) : h;

    if (w < 80 || visualH < 60) {
        return;
    }

    updateTiming(visual, features);

    if (visual->blanker > 0U) {
        drawDropBlanker(rp, x0, y0, w, visualH, visual);
        return;
    }

    switch (visual->mode) {
        case VISUAL_MODE_RADIAL_BRUSH:
            renderRadialBrush(rp, x0, y0, w, visualH, visual, features);
            break;

        case VISUAL_MODE_DENSE_RASTER:
            renderDenseRaster(rp, x0, y0, w, visualH, visual, features);
            break;

        case VISUAL_MODE_WIREFRAME:
            renderWireframe(rp, x0, y0, w, visualH, visual, features);
            break;

        case VISUAL_MODE_PARTICLES:
            renderParticles(rp, x0, y0, w, visualH, visual, features);
            break;

        case VISUAL_MODE_WATERFALL:
            renderWaterfall(rp, x0, y0, w, visualH, visual, features);
            break;

        case VISUAL_MODE_DARK_DROP:
        default:
            renderDarkDrop(rp, x0, y0, w, visualH, visual, features);
            break;
    }
}
