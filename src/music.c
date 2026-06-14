#include "music.h"
#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/memory.h>
#include <proto/exec.h>

#define MUSIC_MIN_MOD_BYTES 1084L
#define MUSIC_MAX_MOD_BYTES 1048576L
#define MUSIC_ALL_CHANNELS  0x0f
#define MUSIC_MAX_VOLUME    64
#define MUSIC_CHANNEL_COUNT 4
#define MUSIC_ORDER_COUNT   128
#define MUSIC_ROW_COUNT     64
#define MUSIC_PATTERN_BYTES 1024L
#define MUSIC_PATTERN_START 1084L
#define MUSIC_ROW_BYTES     16L
#define MUSIC_DEFAULT_SPEED 6
#define MUSIC_DEFAULT_TEMPO 125
#define MUSIC_TICK_BASE     125
#define MUSIC_MIN_PERIOD    113
#define MUSIC_MAX_PERIOD    856

int ana_pt_install(void);
void ana_pt_remove(void);
void ana_pt_init(void *module);
void ana_pt_end(void);
void ana_pt_enable(int enable);
void ana_pt_mastervol(int volume);
void ana_pt_musicmask(int mask);
void ana_pt_channelmask(int mask);

static struct MusicModule *currentModule;
static BOOL ptInstalled;
static BOOL playing;

struct ModEvent {
    UBYTE sample;
    UWORD period;
    UBYTE effect;
    UBYTE param;
};

struct MusicAnalysisState {
    ULONG frame;
    UBYTE order;
    UBYTE row;
    UBYTE speed;
    UWORD tempo;
    UWORD tick;
    UWORD tickAccumulator;
    UWORD channelLevel[MUSIC_CHANNEL_COUNT];
    UWORD channelHit[MUSIC_CHANNEL_COUNT];
    UWORD channelVolume[MUSIC_CHANNEL_COUNT];
    UWORD channelPeriod[MUSIC_CHANNEL_COUNT];
    UWORD onsetLevel;
    BOOL rowPending;
    BOOL jumpPending;
    UBYTE jumpOrder;
    UBYTE breakRow;
};

static struct MusicAnalysisState analysis;

static int readU16BE(const UBYTE *bytes)
{
    return ((int)bytes[0] << 8) | (int)bytes[1];
}

static UWORD clamp100Long(LONG value)
{
    if (value < 0L) {
        return 0;
    }
    if (value > 100L) {
        return 100;
    }
    return (UWORD)value;
}

static UBYTE clampVolume(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > MUSIC_MAX_VOLUME) {
        return MUSIC_MAX_VOLUME;
    }
    return (UBYTE)value;
}

static int signatureChannels(const UBYTE *signature)
{
    if (signature == NULL) {
        return 0;
    }

    if (memcmp(signature, "M.K.", 4u) == 0 ||
            memcmp(signature, "M!K!", 4u) == 0 ||
            memcmp(signature, "FLT4", 4u) == 0 ||
            memcmp(signature, "4CHN", 4u) == 0) {
        return 4;
    }

    return 0;
}

static int moduleSongLength(const UBYTE *bytes)
{
    int length;

    if (bytes == NULL) {
        return 0;
    }

    length = (int)bytes[950];
    if (length < 1 || length > MUSIC_ORDER_COUNT) {
        return 0;
    }

    return length;
}

static int moduleMaxPattern(const UBYTE *bytes, int songLength)
{
    int i;
    int maxPattern;
    int pattern;

    if (bytes == NULL || songLength < 1) {
        return -1;
    }

    maxPattern = 0;
    for (i = 0; i < songLength; i++) {
        pattern = (int)bytes[952 + i];
        if (pattern > maxPattern) {
            maxPattern = pattern;
        }
    }

    return maxPattern;
}

static LONG modulePatternDataEnd(int maxPattern)
{
    if (maxPattern < 0) {
        return 0L;
    }

    return MUSIC_PATTERN_START + (((LONG)maxPattern + 1L) * MUSIC_PATTERN_BYTES);
}

static BOOL isValidMod(const UBYTE *bytes, LONG size)
{
    int songLength;
    int maxPattern;
    LONG patternEnd;

    if (bytes == NULL ||
            size < MUSIC_MIN_MOD_BYTES ||
            size > MUSIC_MAX_MOD_BYTES) {
        return FALSE;
    }

    if (signatureChannels(bytes + 1080) != 4) {
        return FALSE;
    }

    songLength = moduleSongLength(bytes);
    maxPattern = moduleMaxPattern(bytes, songLength);
    patternEnd = modulePatternDataEnd(maxPattern);

    return patternEnd >= MUSIC_PATTERN_START && patternEnd <= size;
}

static void prepareModSamples(UBYTE *bytes, LONG size)
{
    int sample;
    int maxPattern;
    int songLength;
    LONG sampleOffset;
    LONG sampleLength;
    LONG sampleHeader;

    if (!isValidMod(bytes, size)) {
        return;
    }

    songLength = moduleSongLength(bytes);
    maxPattern = moduleMaxPattern(bytes, songLength);
    sampleOffset = modulePatternDataEnd(maxPattern);
    if (sampleOffset >= size) {
        return;
    }

    for (sample = 0; sample < 31; sample++) {
        sampleHeader = 20L + (LONG)sample * 30L;
        sampleLength = (LONG)readU16BE(bytes + sampleHeader) * 2L;
        if (sampleLength <= 0L) {
            continue;
        }

        if (sampleOffset + sampleLength > size) {
            return;
        }

        if (sampleLength >= 2L) {
            bytes[sampleOffset] = 0;
            bytes[sampleOffset + 1L] = 0;
        }
        sampleOffset += sampleLength;
    }
}

static void resetAnalysis(void)
{
    memset(&analysis, 0, sizeof(analysis));
    analysis.speed = MUSIC_DEFAULT_SPEED;
    analysis.tempo = MUSIC_DEFAULT_TEMPO;
    analysis.rowPending = TRUE;
}

static BOOL ensurePtPlayer(void)
{
    if (!ptInstalled) {
        ptInstalled = ana_pt_install() != 0;
    }

    return ptInstalled;
}

void Music_Init(void)
{
    currentModule = NULL;
    ptInstalled = FALSE;
    playing = FALSE;
    resetAnalysis();
}

void Music_Shutdown(void)
{
    Music_Stop();
    if (ptInstalled) {
        ana_pt_end();
        ana_pt_remove();
        ptInstalled = FALSE;
    }
}

struct MusicModule *Music_LoadModule(const char *path)
{
    FILE *file;
    struct MusicModule *module;
    size_t bytesRead;
    LONG size;

    if (path == NULL) {
        return NULL;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    size = ftell(file);
    if (size < MUSIC_MIN_MOD_BYTES ||
            size > MUSIC_MAX_MOD_BYTES ||
            fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    module = (struct MusicModule *)malloc(sizeof(struct MusicModule));
    if (module == NULL) {
        fclose(file);
        return NULL;
    }
    module->bytes = NULL;
    module->size = 0L;
    module->songLength = 0;
    module->maxPattern = 0;
    module->patternDataEnd = 0L;

    module->bytes = (UBYTE *)AllocMem((ULONG)size, MEMF_CHIP | MEMF_CLEAR);
    if (module->bytes == NULL) {
        free(module);
        fclose(file);
        return NULL;
    }

    bytesRead = fread(module->bytes, 1u, (size_t)size, file);
    fclose(file);

    if (bytesRead != (size_t)size || !isValidMod(module->bytes, size)) {
        FreeMem(module->bytes, (ULONG)size);
        free(module);
        return NULL;
    }

    prepareModSamples(module->bytes, size);
    module->size = size;
    module->songLength = (UBYTE)moduleSongLength(module->bytes);
    module->maxPattern = (UBYTE)moduleMaxPattern(module->bytes,
                                                 (int)module->songLength);
    module->patternDataEnd = modulePatternDataEnd((int)module->maxPattern);

    return module;
}

void Music_FreeModule(struct MusicModule *module)
{
    if (module == NULL) {
        return;
    }

    if (currentModule == module) {
        Music_Stop();
        currentModule = NULL;
    }

    if (module->bytes != NULL) {
        FreeMem(module->bytes, (ULONG)module->size);
    }
    free(module);
}

BOOL Music_PlayModule(struct MusicModule *module)
{
    if (module == NULL || module->bytes == NULL || !ensurePtPlayer()) {
        return FALSE;
    }

    Music_Stop();

    currentModule = module;
    resetAnalysis();
    ana_pt_init(module->bytes);
    ana_pt_musicmask(MUSIC_ALL_CHANNELS);
    ana_pt_channelmask(MUSIC_ALL_CHANNELS);
    ana_pt_mastervol(MUSIC_MAX_VOLUME);
    ana_pt_enable(1);
    playing = TRUE;

    return TRUE;
}

void Music_Stop(void)
{
    if (!ptInstalled) {
        playing = FALSE;
        currentModule = NULL;
        return;
    }

    ana_pt_enable(0);
    ana_pt_mastervol(0);
    ana_pt_channelmask(0);
    ana_pt_end();
    playing = FALSE;
    currentModule = NULL;
    resetAnalysis();
}

BOOL Music_IsPlaying(void)
{
    return playing;
}

static UBYTE sampleVolume(const struct MusicModule *module, UBYTE sample)
{
    LONG offset;

    if (module == NULL || module->bytes == NULL || sample < 1 || sample > 31) {
        return 0;
    }

    offset = 20L + ((LONG)sample - 1L) * 30L + 25L;
    if (offset >= module->size) {
        return 0;
    }

    return clampVolume((int)module->bytes[offset]);
}

static BOOL readEvent(const struct MusicModule *module,
                      UBYTE order,
                      UBYTE row,
                      UBYTE channel,
                      struct ModEvent *event)
{
    const UBYTE *bytes;
    UBYTE pattern;
    LONG offset;

    if (module == NULL || module->bytes == NULL || event == NULL ||
            order >= module->songLength ||
            row >= MUSIC_ROW_COUNT ||
            channel >= MUSIC_CHANNEL_COUNT) {
        return FALSE;
    }

    bytes = module->bytes;
    pattern = bytes[952 + order];
    if (pattern > module->maxPattern) {
        return FALSE;
    }

    offset = MUSIC_PATTERN_START +
             ((LONG)pattern * MUSIC_PATTERN_BYTES) +
             ((LONG)row * MUSIC_ROW_BYTES) +
             ((LONG)channel * 4L);
    if (offset + 3L >= module->patternDataEnd || offset + 3L >= module->size) {
        return FALSE;
    }

    event->sample = (UBYTE)((bytes[offset] & 0xf0U) |
                            ((bytes[offset + 2L] >> 4) & 0x0fU));
    event->period = (UWORD)(((UWORD)(bytes[offset] & 0x0fU) << 8) |
                            (UWORD)bytes[offset + 1L]);
    event->effect = (UBYTE)(bytes[offset + 2L] & 0x0fU);
    event->param = bytes[offset + 3L];

    return TRUE;
}

static UBYTE patternBreakRow(UBYTE param)
{
    UBYTE row;

    row = (UBYTE)(((param >> 4) * 10) + (param & 0x0fU));
    if (row >= MUSIC_ROW_COUNT) {
        row = 0;
    }

    return row;
}

static void queueJump(UBYTE order, UBYTE row)
{
    analysis.jumpPending = TRUE;
    analysis.jumpOrder = order;
    analysis.breakRow = row;
}

static void applyControlEffect(const struct MusicModule *module,
                               const struct ModEvent *event)
{
    UBYTE nextOrder;

    if (module == NULL || event == NULL) {
        return;
    }

    switch (event->effect) {
        case 0x0b:
            queueJump(event->param, 0);
            break;

        case 0x0d:
            if (analysis.jumpPending) {
                nextOrder = analysis.jumpOrder;
            } else {
                nextOrder = (UBYTE)(analysis.order + 1U);
            }
            queueJump(nextOrder, patternBreakRow(event->param));
            break;

        case 0x0f:
            if (event->param > 0 && event->param <= 31) {
                analysis.speed = event->param;
            } else if (event->param >= 32) {
                analysis.tempo = event->param;
            }
            break;

        default:
            break;
    }
}

static UWORD periodPitchScore(UWORD period)
{
    if (period < MUSIC_MIN_PERIOD) {
        period = MUSIC_MIN_PERIOD;
    } else if (period > MUSIC_MAX_PERIOD) {
        period = MUSIC_MAX_PERIOD;
    }

    return (UWORD)(((LONG)(MUSIC_MAX_PERIOD - period) * 100L) /
                   (LONG)(MUSIC_MAX_PERIOD - MUSIC_MIN_PERIOD));
}

static UWORD bassWeight(UWORD period)
{
    if (period <= 300) {
        return 0;
    }
    if (period >= MUSIC_MAX_PERIOD) {
        return 100;
    }

    return (UWORD)(((LONG)(period - 300) * 100L) /
                   (LONG)(MUSIC_MAX_PERIOD - 300));
}

static UWORD trebleWeight(UWORD period)
{
    if (period <= MUSIC_MIN_PERIOD) {
        return 100;
    }
    if (period >= 520) {
        return 0;
    }

    return (UWORD)(((LONG)(520 - period) * 100L) /
                   (LONG)(520 - MUSIC_MIN_PERIOD));
}

static UWORD midWeight(UWORD period)
{
    LONG distance;
    LONG weight;

    distance = labs((LONG)period - 428L);
    weight = 100L - ((distance * 100L) / 330L);

    return clamp100Long(weight);
}

static void processCurrentRow(void)
{
    struct ModEvent event;
    UBYTE channel;
    UWORD volume;
    UWORD level;
    UWORD onsetBoost;
    BOOL noteTriggered;

    if (currentModule == NULL || currentModule->bytes == NULL) {
        return;
    }

    onsetBoost = 0;
    for (channel = 0; channel < MUSIC_CHANNEL_COUNT; channel++) {
        if (!readEvent(currentModule,
                       analysis.order,
                       analysis.row,
                       channel,
                       &event)) {
            continue;
        }

        if (event.sample >= 1 && event.sample <= 31) {
            analysis.channelVolume[channel] =
                sampleVolume(currentModule, event.sample);
        }

        if (event.effect == 0x0c) {
            analysis.channelVolume[channel] = clampVolume((int)event.param);
        }

        noteTriggered = event.period != 0;
        if (noteTriggered) {
            analysis.channelPeriod[channel] = event.period;
            volume = analysis.channelVolume[channel];
            if (volume == 0) {
                volume = MUSIC_MAX_VOLUME;
            }

            level = (UWORD)(30U + ((volume * 70U) / MUSIC_MAX_VOLUME));
            if (level > analysis.channelLevel[channel]) {
                analysis.channelLevel[channel] = level;
            }
            analysis.channelHit[channel] = 100;
            onsetBoost = (UWORD)(onsetBoost + 25U + (volume >> 1));
        }

        applyControlEffect(currentModule, &event);
    }

    if (onsetBoost > analysis.onsetLevel) {
        analysis.onsetLevel = clamp100Long((LONG)onsetBoost);
    }
}

static void advanceRow(void)
{
    UBYTE songLength;

    if (currentModule == NULL || currentModule->songLength == 0) {
        analysis.row = 0;
        analysis.order = 0;
        analysis.jumpPending = FALSE;
        return;
    }

    songLength = currentModule->songLength;
    if (analysis.jumpPending) {
        analysis.order = analysis.jumpOrder;
        if (analysis.order >= songLength) {
            analysis.order = 0;
        }
        analysis.row = analysis.breakRow;
        if (analysis.row >= MUSIC_ROW_COUNT) {
            analysis.row = 0;
        }
        analysis.jumpPending = FALSE;
        return;
    }

    analysis.row = (UBYTE)(analysis.row + 1U);
    if (analysis.row >= MUSIC_ROW_COUNT) {
        analysis.row = 0;
        analysis.order = (UBYTE)(analysis.order + 1U);
        if (analysis.order >= songLength) {
            analysis.order = 0;
        }
    }
}

static void advanceTicks(void)
{
    int guard;

    if (analysis.rowPending) {
        processCurrentRow();
        analysis.rowPending = FALSE;
    }

    analysis.tickAccumulator =
        (UWORD)(analysis.tickAccumulator + analysis.tempo);

    guard = 0;
    while (analysis.tickAccumulator >= MUSIC_TICK_BASE && guard < 8) {
        analysis.tickAccumulator =
            (UWORD)(analysis.tickAccumulator - MUSIC_TICK_BASE);
        analysis.tick = (UWORD)(analysis.tick + 1U);
        if (analysis.tick >= analysis.speed) {
            analysis.tick = 0;
            advanceRow();
            processCurrentRow();
        }
        guard++;
    }
}

static void writeFeatures(struct AudioFeatures *features)
{
    UBYTE channel;
    UWORD level;
    UWORD period;
    LONG bass;
    LONG mid;
    LONG treble;
    LONG left;
    LONG right;
    LONG pitchWeighted;
    LONG totalLevel;

    bass = 0L;
    mid = 0L;
    treble = 0L;
    left = 0L;
    right = 0L;
    pitchWeighted = 0L;
    totalLevel = 0L;

    for (channel = 0; channel < MUSIC_CHANNEL_COUNT; channel++) {
        level = clamp100Long((LONG)analysis.channelLevel[channel] +
                             ((LONG)analysis.channelHit[channel] / 3L));
        period = analysis.channelPeriod[channel];

        if (period != 0) {
            bass += ((LONG)level * (LONG)bassWeight(period)) / 100L;
            mid += ((LONG)level * (LONG)midWeight(period)) / 100L;
            treble += ((LONG)level * (LONG)trebleWeight(period)) / 100L;
            pitchWeighted +=
                ((LONG)level * (LONG)periodPitchScore(period));
            totalLevel += level;
        }

        if (channel == 0 || channel == 3) {
            left += level;
        } else {
            right += level;
        }
    }

    features->bass = clamp100Long(bass + ((LONG)analysis.onsetLevel / 4L));
    features->mid = clamp100Long(mid + ((LONG)analysis.onsetLevel / 6L));
    features->treble = clamp100Long(treble + ((LONG)analysis.onsetLevel / 3L));
    features->left = clamp100Long(left / 2L);
    features->right = clamp100Long(right / 2L);
    features->onset = analysis.onsetLevel;
    if (totalLevel > 0L) {
        features->pitch = (UWORD)(80U + (UWORD)((pitchWeighted / totalLevel) * 5L));
    } else {
        features->pitch = 0;
    }
    features->frame = analysis.frame;
}

static void decayAnalysis(void)
{
    UBYTE channel;

    for (channel = 0; channel < MUSIC_CHANNEL_COUNT; channel++) {
        analysis.channelLevel[channel] =
            (UWORD)(((LONG)analysis.channelLevel[channel] * 94L) / 100L);
        analysis.channelHit[channel] =
            (UWORD)(((LONG)analysis.channelHit[channel] * 58L) / 100L);
        if (analysis.channelLevel[channel] < 2) {
            analysis.channelLevel[channel] = 0;
        }
        if (analysis.channelHit[channel] < 2) {
            analysis.channelHit[channel] = 0;
        }
    }

    analysis.onsetLevel =
        (UWORD)(((LONG)analysis.onsetLevel * 62L) / 100L);
    if (analysis.onsetLevel < 2) {
        analysis.onsetLevel = 0;
    }
}

void Music_UpdateFeatures(struct AudioFeatures *features)
{
    if (features == NULL) {
        return;
    }

    if (!playing || currentModule == NULL || currentModule->bytes == NULL) {
        memset(features, 0, sizeof(*features));
        return;
    }

    analysis.frame++;
    advanceTicks();
    writeFeatures(features);
    decayAnalysis();
}
