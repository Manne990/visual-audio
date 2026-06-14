# Visual Audio

Native Amiga C experiment inspired by MindLight / Visual Aurals.

The first target is deliberately small:

- starts from Workbench or Shell
- opens a normal Intuition window on the Workbench screen
- can switch to a borderless custom screen for a richer palette
- opens an ASL file requester for choosing a MOD file
- renders audio-reactive visuals in the active display mode
- keeps audio/control input abstract so we can later add Paula, tracker, or external hardware backends

## Build target

The code is written as AmigaOS 2.x/3.x style C and should stay close to C89.

The Makefile follows the same toolchain shape as the ANA project:

- uses local `m68k-amigaos-gcc` when available
- otherwise runs `m68k-amigaos-gcc` through Docker image `amigadev/crosstools:m68k-amigaos-gcc10_amd64`
- uses `xdftool` from `amitools` to format and populate the ADF

```sh
make amiga
```

Typical output:

```text
build/amiga/VisualAudio
```

The host build artifact keeps the compact name `VisualAudio`, but the ADF copies
it as the Workbench program `Visual Audio`.

Create an ADF:

```sh
make adf
```

Typical ADF output:

```text
build/adf/visual-audio.adf
```

The disk volume label is `Visual Audio`.

Run the ADF in FS-UAE on macOS:

```sh
make run-adf
```

Copy the executable or generated ADF to an Amiga or emulator. The program starts in its own window on the public Workbench screen and can switch to a custom screen while running.

The ADF includes `Visual Audio.info`, so the program appears as a Workbench tool icon when the disk window is opened.

If `xdftool` is not on `PATH`, the Makefile creates a local `amitools` virtualenv under `build/tools/amitools-venv`.

`make adf` and `make run-adf` also remove FS-UAE's writable floppy overlay for `visual-audio.adf` before testing. FS-UAE Launcher can otherwise keep a stale `visual-audio.sdf` save image that makes a newly built ADF still appear as an old or broken disk.

## Current input model

When a MOD is loaded, the visual engine is driven from the module player's state: per-channel volumes, note periods, pattern rows, row timing, and common tracker effects. That produces bass, mid, treble, stereo direction, onset pulses, and a pitch-like value for the visuals.

If no MOD is playing, the app falls back to a synthetic visual-input backend so the window remains animated.

On startup the app asks for a ProTracker MOD file through `asl.library`. Press `O` while the window is active to choose another MOD, `S` to toggle between Workbench-window and custom-screen display, `V` to advance to the next visual mode, and space to freeze/unfreeze the current frame. The selected module is loaded into Chip RAM and played with the bundled `ptplayer` backend.

Workbench-window mode uses the public screen's available pens. Custom-screen mode opens a 320x256 low-resolution screen, asks for 32 colors with a 16-color fallback, and loads Visual Audio's own palette.

The visual engine is mode-based. Current modes cover radial brush loops, dense raster fields, wireframe geometry, particles, vertical waterfall streaks, and dark drop-screen transitions. Strong onsets can reseed or advance modes automatically.

Current music limits:

- classic 4-channel ProTracker signatures only: `M.K.`, `M!K!`, `FLT4`, `4CHN`
- maximum module size: 1 MB
- analysis is tracker-state based, not FFT or sample-waveform based, so modules with similar note density and channel volumes can still look similar

Real Amiga audio-reactivity has a hard limitation: AmigaOS exposes four output audio channels, but it does not provide a general system-wide audio loopback/mixer that another process can read. Tracker programs also often drive Paula directly instead of using `audio.device`.

So the answer to "can it react to a MOD playing in a tracker?" is:

- **Reliably, yes, if our program plays the MOD itself** and feeds the visual engine from the module player's channel volumes, periods, samples, and pattern events.
- **Reliably, yes, if the tracker cooperates** by sending level/event data to this program through a small bridge.
- **Not reliably as a generic background listener** while an arbitrary tracker owns Paula directly. A best-effort Paula snooper may be possible, but it will be tracker- and hardware-behavior dependent.

Practical later backends:

- **MOD/player backend:** our program plays the module itself and analyzes pattern/sample state.
- **Tracker bridge:** a cooperating tracker/exporter sends levels or events to us.
- **Paula snoop backend:** best-effort hardware/register observation; useful for experiments but not guaranteed with all trackers.
- **External MindLight-style hardware:** controller-port input, closest to the original design.

See [docs/protracker-bridge.md](docs/protracker-bridge.md) for the ProTracker 3.61 bridge plan.

See [docs/mindlight-video-effect-requirements.md](docs/mindlight-video-effect-requirements.md) for requirements extracted from the supplied recordings of the original MindLight / Visual Aurals software.
