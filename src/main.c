#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/asl.h>
#include <devices/inputevent.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "input.h"
#include "music.h"
#include "visual.h"

#define WIN_MIN_WIDTH  240
#define WIN_MIN_HEIGHT 160
#define RAW_ESC        0x45
#define RAW_SPACE      0x40
#define RAW_O          0x18
#define RAW_S          0x21
#define RAW_V          0x34
#define MOD_PATH_SIZE  256
#define TITLE_SIZE     96

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *AslBase;

struct ModuleSelection {
    char path[MOD_PATH_SIZE];
    char drawer[MOD_PATH_SIZE];
    char file[TITLE_SIZE];
    char title[TITLE_SIZE];
    char status[TITLE_SIZE];
    struct MusicModule *module;
};

struct AppDisplay {
    struct Screen *screen;
    struct Window *window;
    BOOL custom;
};

static const UWORD customPalette[32] = {
    0x000, 0x111, 0xfff, 0x48f, 0x0af, 0x0ff, 0x0f8, 0x8f2,
    0xff0, 0xfa0, 0xf60, 0xf22, 0xf0a, 0xa4f, 0x74f, 0x44f,
    0x222, 0x333, 0x555, 0x777, 0x999, 0xbbb, 0xddf, 0x8cf,
    0x4f8, 0xbf4, 0xfd4, 0xf94, 0xf6c, 0xf5f, 0x98f, 0x58f
};

static struct Window *openWorkbenchWindow(const char *title)
{
    return OpenWindowTags(NULL,
        WA_Title,       (ULONG)(CONST_STRPTR)title,
        WA_Left,        40,
        WA_Top,         30,
        WA_Width,       420,
        WA_Height,      260,
        WA_MinWidth,    WIN_MIN_WIDTH,
        WA_MinHeight,   WIN_MIN_HEIGHT,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar,     TRUE,
        WA_SizeGadget,  TRUE,
        WA_Activate,    TRUE,
        WA_RMBTrap,     TRUE,
        WA_Flags,       WFLG_SMART_REFRESH,
        WA_IDCMP,       IDCMP_CLOSEWINDOW |
                        IDCMP_NEWSIZE |
                        IDCMP_REFRESHWINDOW |
                        IDCMP_RAWKEY,
        TAG_DONE);
}

static struct Screen *openCustomScreen(void)
{
    struct Screen *screen;

    screen = OpenScreenTags(NULL,
        SA_Type,      CUSTOMSCREEN,
        SA_DisplayID, LORES_KEY,
        SA_Width,     320,
        SA_Height,    256,
        SA_Depth,     5,
        SA_Title,     (ULONG)(CONST_STRPTR)"Visual Audio",
        SA_ShowTitle, FALSE,
        TAG_DONE);

    if (screen == NULL) {
        screen = OpenScreenTags(NULL,
            SA_Type,      CUSTOMSCREEN,
            SA_DisplayID, LORES_KEY,
            SA_Width,     320,
            SA_Height,    256,
            SA_Depth,     4,
            SA_Title,     (ULONG)(CONST_STRPTR)"Visual Audio",
            SA_ShowTitle, FALSE,
            TAG_DONE);
    }

    if (screen != NULL) {
        LoadRGB4(&screen->ViewPort, customPalette,
                 (screen->RastPort.BitMap->Depth >= 5) ? 32L : 16L);
    }

    return screen;
}

static struct Window *openCustomWindow(struct Screen *screen, const char *title)
{
    return OpenWindowTags(NULL,
        WA_Title,        (ULONG)(CONST_STRPTR)title,
        WA_CustomScreen, (ULONG)screen,
        WA_Left,         0,
        WA_Top,          0,
        WA_Width,        screen->Width,
        WA_Height,       screen->Height,
        WA_Backdrop,     TRUE,
        WA_Borderless,   TRUE,
        WA_Activate,     TRUE,
        WA_RMBTrap,      TRUE,
        WA_Flags,        WFLG_SMART_REFRESH,
        WA_IDCMP,        IDCMP_CLOSEWINDOW |
                         IDCMP_REFRESHWINDOW |
                         IDCMP_RAWKEY,
        TAG_DONE);
}

static BOOL openAppDisplay(struct AppDisplay *display,
                           BOOL custom,
                           const char *title)
{
    display->screen = NULL;
    display->window = NULL;
    display->custom = custom;

    if (custom) {
        display->screen = openCustomScreen();
        if (display->screen == NULL) {
            return FALSE;
        }

        display->window = openCustomWindow(display->screen, title);
        if (display->window == NULL) {
            CloseScreen(display->screen);
            display->screen = NULL;
            return FALSE;
        }
        return TRUE;
    }

    display->window = openWorkbenchWindow(title);
    return display->window != NULL;
}

static void closeAppDisplay(struct AppDisplay *display)
{
    if (display->window != NULL) {
        CloseWindow(display->window);
        display->window = NULL;
    }
    if (display->screen != NULL) {
        CloseScreen(display->screen);
        display->screen = NULL;
    }
    display->custom = FALSE;
}

static BOOL switchAppDisplay(struct AppDisplay *display,
                             BOOL custom,
                             const char *title)
{
    BOOL oldCustom;

    oldCustom = display->custom;
    closeAppDisplay(display);
    if (openAppDisplay(display, custom, title)) {
        return TRUE;
    }

    return openAppDisplay(display, oldCustom, title);
}

static void handleRefresh(struct Window *window)
{
    BeginRefresh(window);
    EndRefresh(window, TRUE);
}

static void copyString(char *dst, const char *src, ULONG dstSize)
{
    ULONG i;

    if (dstSize == 0) {
        return;
    }

    i = 0;
    while (i + 1 < dstSize && src != NULL && src[i] != 0) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void appendString(char *dst, const char *src, ULONG dstSize)
{
    ULONG i;
    ULONG j;

    if (dstSize == 0 || src == NULL) {
        return;
    }

    i = 0;
    while (i < dstSize && dst[i] != 0) {
        i++;
    }

    j = 0;
    while (i + 1 < dstSize && src[j] != 0) {
        dst[i] = src[j];
        i++;
        j++;
    }
    dst[i] = 0;
}

static void buildSelectedPath(struct ModuleSelection *selection,
                              const char *drawer,
                              const char *file)
{
    ULONG len;

    selection->path[0] = 0;
    selection->drawer[0] = 0;

    if (drawer != NULL && drawer[0] != 0) {
        copyString(selection->drawer, drawer, MOD_PATH_SIZE);
        copyString(selection->path, drawer, MOD_PATH_SIZE);
        len = strlen(selection->path);
        if (len > 0 &&
            selection->path[len - 1] != ':' &&
            selection->path[len - 1] != '/') {
            appendString(selection->path, "/", MOD_PATH_SIZE);
        }
    }

    appendString(selection->path, file, MOD_PATH_SIZE);

    copyString(selection->file, file, TITLE_SIZE);
    copyString(selection->title, "Visual Audio - ", TITLE_SIZE);
    appendString(selection->title, file, TITLE_SIZE);
}

static BOOL drawerExists(const char *drawer)
{
    BPTR lock;

    if (drawer == NULL || drawer[0] == 0) {
        return FALSE;
    }

    lock = Lock((CONST_STRPTR)drawer, ACCESS_READ);
    if (lock == 0) {
        return FALSE;
    }

    UnLock(lock);
    return TRUE;
}

static BOOL requestModuleFile(struct Window *window, struct ModuleSelection *selection)
{
    struct FileRequester *requester;
    ULONG initialDrawerTag;
    CONST_STRPTR initialDrawer;
    BOOL selected;

    if (AslBase == NULL) {
        return FALSE;
    }

    initialDrawerTag = TAG_IGNORE;
    initialDrawer = NULL;
    if (drawerExists(selection->drawer)) {
        initialDrawerTag = ASLFR_InitialDrawer;
        initialDrawer = (CONST_STRPTR)selection->drawer;
    }

    requester = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window,         (ULONG)window,
        ASLFR_TitleText,      (ULONG)(CONST_STRPTR)"Select ProTracker MOD",
        ASLFR_RejectIcons,    TRUE,
        ASLFR_DoPatterns,     TRUE,
        ASLFR_InitialPattern, (ULONG)(CONST_STRPTR)"#?.mod|mod.#?",
        initialDrawerTag,     (ULONG)initialDrawer,
        TAG_DONE);

    if (requester == NULL) {
        return FALSE;
    }

    selected = FALSE;
    if (AslRequestTags(requester, TAG_DONE)) {
        buildSelectedPath(selection,
                          (const char *)requester->fr_Drawer,
                          (const char *)requester->fr_File);
        SetWindowTitles(window,
                        (CONST_STRPTR)selection->title,
                        (CONST_STRPTR)~0L);
        selected = TRUE;
    }

    FreeAslRequest(requester);

    return selected;
}

static void loadAndPlaySelectedModule(struct Window *window,
                                      struct ModuleSelection *selection)
{
    struct MusicModule *newModule;
    struct MusicModule *oldModule;

    if (selection->path[0] == 0) {
        copyString(selection->status, "no module", TITLE_SIZE);
        return;
    }

    newModule = Music_LoadModule(selection->path);
    if (newModule == NULL) {
        copyString(selection->status, "load failed", TITLE_SIZE);
        return;
    }

    if (!Music_PlayModule(newModule)) {
        Music_FreeModule(newModule);
        copyString(selection->status, "play failed", TITLE_SIZE);
        return;
    }

    oldModule = selection->module;
    selection->module = newModule;
    if (oldModule != NULL) {
        Music_FreeModule(oldModule);
    }

    copyString(selection->status, "playing", TITLE_SIZE);
    SetWindowTitles(window,
                    (CONST_STRPTR)selection->title,
                    (CONST_STRPTR)~0L);
}

static BOOL chooseAndPlayModule(struct Window *window,
                                struct ModuleSelection *selection)
{
    if (!requestModuleFile(window, selection)) {
        return FALSE;
    }

    loadAndPlaySelectedModule(window, selection);
    return TRUE;
}

static void drawModuleStatus(struct Window *window,
                             const struct ModuleSelection *selection)
{
    struct RastPort *rp;
    WORD x0;
    WORD y0;
    WORD w;
    WORD h;
    char label[MOD_PATH_SIZE + 6];

    rp = window->RPort;
    x0 = window->BorderLeft;
    y0 = window->BorderTop;
    w = (WORD)(window->Width - window->BorderLeft - window->BorderRight);
    h = (WORD)(window->Height - window->BorderTop - window->BorderBottom);

    if (w < 100 || h < 32) {
        return;
    }

    SetAPen(rp, 1);
    RectFill(rp,
             (WORD)(x0 + 4),
             (WORD)(y0 + h - 15),
             (WORD)(x0 + w - 5),
             (WORD)(y0 + h - 4));

    SetAPen(rp, 3);
    SetBPen(rp, 1);
    SetDrMd(rp, JAM2);

    copyString(label, "MOD: ", sizeof(label));
    if (selection->file[0] != 0) {
        appendString(label, selection->file, sizeof(label));
    } else {
        appendString(label, "none", sizeof(label));
    }
    appendString(label, " - ", sizeof(label));
    appendString(label, selection->status, sizeof(label));

    Move(rp, (WORD)(x0 + 8), (WORD)(y0 + h - 6));
    Text(rp, (CONST_STRPTR)label, (WORD)strlen(label));
}

static BOOL processMessages(struct AppDisplay *display,
                            struct VisualState *visual,
                            struct ModuleSelection *selection,
                            BOOL *toggleDisplay)
{
    struct IntuiMessage *msg;
    ULONG classValue;
    UWORD code;
    BOOL running;
    struct Window *window;

    running = TRUE;
    window = display->window;

    while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
        classValue = msg->Class;
        code = msg->Code;
        ReplyMsg((struct Message *)msg);

        switch (classValue) {
            case IDCMP_CLOSEWINDOW:
                running = FALSE;
                break;

            case IDCMP_REFRESHWINDOW:
                handleRefresh(window);
                break;

            case IDCMP_RAWKEY:
                if ((code & IECODE_UP_PREFIX) == 0) {
                    if (code == RAW_ESC) {
                        running = FALSE;
                    } else if (code == RAW_SPACE) {
                        Visual_ToggleFreeze(visual);
                    } else if (code == RAW_O) {
                        chooseAndPlayModule(window, selection);
                    } else if (code == RAW_S) {
                        *toggleDisplay = TRUE;
                    } else if (code == RAW_V) {
                        Visual_NextScene(visual);
                    }
                }
                break;

            default:
                break;
        }
    }

    return running;
}

int main(int argc, char **argv)
{
    struct AppDisplay display;
    struct InputSource input;
    struct AudioFeatures features;
    struct VisualState visual;
    struct ModuleSelection selection;
    BOOL running;
    BOOL toggleDisplay;
    BOOL customDisplay;

    (void)argc;
    (void)argv;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37L);
    if (IntuitionBase == NULL) {
        return 20;
    }

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 37L);
    if (GfxBase == NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 37L);

    Input_Init(&input);
    Visual_Init(&visual);
    Music_Init();

    selection.path[0] = 0;
    selection.drawer[0] = 0;
    selection.file[0] = 0;
    selection.module = NULL;
    copyString(selection.title, "Visual Audio", TITLE_SIZE);
    copyString(selection.status, "synthetic input", TITLE_SIZE);

    customDisplay = FALSE;
    if (!openAppDisplay(&display, customDisplay, selection.title)) {
        Music_Shutdown();
        if (AslBase != NULL) {
            CloseLibrary(AslBase);
        }
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    chooseAndPlayModule(display.window, &selection);

    running = TRUE;

    while (running) {
        toggleDisplay = FALSE;
        running = processMessages(&display, &visual, &selection, &toggleDisplay);
        if (running && toggleDisplay) {
            customDisplay = customDisplay ? FALSE : TRUE;
            if (!switchAppDisplay(&display, customDisplay, selection.title)) {
                running = FALSE;
            }
            customDisplay = display.custom;
        }

        if (!running) {
            break;
        }

        if (Music_IsPlaying()) {
            Music_UpdateFeatures(&features);
        } else {
            Input_UpdateDemo(&input, &features);
        }
        Visual_Render(display.window, &visual, &features);
        drawModuleStatus(display.window, &selection);
        WaitTOF();
    }

    Music_FreeModule(selection.module);
    closeAppDisplay(&display);
    Music_Shutdown();
    if (AslBase != NULL) {
        CloseLibrary(AslBase);
    }
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
