# MindLight video effect requirements

This document turns the three supplied recordings of the original MindLight /
Visual Aurals software into implementation requirements for Visual Audio.

Source captures:

- `/Users/manne990/Downloads/Untitled1.mp4`
- `/Users/manne990/Downloads/Untitled2.mp4`
- `/Users/manne990/Downloads/Untitled3.mp4`

Supplementary transcript:

- `/Users/manne990/Downloads/amiga-oddware-from-1987-mindlight-7-concert-music-visualizer.txt`

Generated analysis artifacts:

- `build/video-analysis/summary.md`
- `build/video-analysis/untitled1-frame-analysis.csv`
- `build/video-analysis/untitled2-frame-analysis.csv`
- `build/video-analysis/untitled3-frame-analysis.csv`
- `build/video-analysis/untitled1-segments.csv`
- `build/video-analysis/untitled2-segments.csv`
- `build/video-analysis/untitled3-segments.csv`
- `build/video-analysis/untitled1-sheet-2fps.png`
- `build/video-analysis/untitled2-sheet-2fps.png`
- `build/video-analysis/untitled3-sheet-2fps.png`

The CSV files include every decoded video frame. Each row records frame number,
time, foreground density, dominant colour class, motion score, edge orientation,
symmetry, component count, and a coarse visual mode classification.

## Method

The recordings are phone/video captures of a monitor rather than direct Amiga
framebuffer dumps. That means reflections, browser/player controls, the monitor
bezel, and exposure changes are present. Requirements below therefore focus on
stable visual behaviour seen across frames, not exact colour values or exact
pixel positions.

Analysis was run with:

```sh
tools/analyze_mindlight_videos.py \
  /Users/manne990/Downloads/Untitled1.mp4 \
  /Users/manne990/Downloads/Untitled2.mp4 \
  /Users/manne990/Downloads/Untitled3.mp4
```

Each frame was downscaled to 120x90 for statistics, while contact sheets were
generated separately for visual review.

## Transcript-Derived Clues

The YouTube transcript is useful but should be treated as secondary evidence:
it is an automatic transcript of a demo video, and it appears to contain several
speech-to-text errors. In context, "base" means bass, "trouble" means treble,
and "Visual auras" means Visual Aurals.

Implementation clues:

- The demo describes incoming sound as being amplified/analyzed before it
  becomes visual geometry. This reinforces the current model where audio
  features are coarse control signals rather than sampled waveform graphics.
- The clearest mapping phrase is bass-to-light/geometry and treble-to-colour.
  For Visual Audio, bass should primarily affect scale, density, impact, and
  geometry weight, while treble should primarily affect palette cycling,
  shimmer, sparkle, and fine fragmentation.
- The transcript explicitly lists colour palettes, colour cycling, drop screens,
  multiple backdrops, fader types, presets, images, and brushes. Those should be
  treated as separate composable modifiers rather than one-off effects.
- It mentions every parameter being tunable from bass depth to high-treble
  shimmer. The renderer should therefore expose sensitivity/gain-like controls
  per feature later, even if the first Workbench version only has defaults.
- It mentions MIDI control, live digitizer modes, Genlock, camera feedback, and
  VCR/DVD video sources. These are not MVP features, but they explain why the
  original visuals often look layered, noisy, and video-synthetic rather than
  purely geometric.
- The demo setup says the hardware was connected to the mouse port and used an
  external microphone instead of the built-in mic. That supports keeping our
  future hardware/input abstraction separate from the renderer.
- The demo says original software version 1.73 was used from a backup, and that
  users could run from floppy or hard drive. It also says live performers could
  save settings on floppies and move or back them up. That makes presets a
  first-class requirement, not just a convenience.

Additional requirements from the transcript:

- Add a preset/save/load concept for complete "moods": selected mode, modifiers,
  sensitivity, palette, and asset choices.
- Treat faders, drop screens, backdrops, brushes, and colour cycling as
  independent layers that can be recombined.
- Keep room in the architecture for future video/backdrop input even though the
  current Amiga-native MVP will not implement Genlock or live digitizer support.
- Add later UI controls for bass sensitivity and treble shimmer/sparkle.
- Favour generative variation: the demo stresses that each run produces unique
  output.

## Observed Effect Families

### 1. Radial Brush / Kaleidoscope Loops

Seen strongly in Untitled1 frames 0-168, 188-225, 269-307, 397-459 and in
Untitled2 around frames 478-588.

Observed behaviour:

- A repeated brush/stamp appears along circular, spiral, or snake-like paths.
- Stamps are multicolour and often palette-cycled, producing rainbow beads.
- Motion is path-based rather than simple bars: the object seems to crawl,
  rotate, or fold.
- Trails persist briefly, so older positions fade or ghost instead of being
  fully cleared every frame.
- Some scenes use bilateral or central symmetry, but not perfectly; asymmetry
  and drift are part of the look.

Requirements:

- `Visual Audio` must support a brush-stamp path renderer.
- The renderer must place repeated stamps along at least circle, spiral,
  figure-eight, and snake paths.
- Stamp phase, path radius, path rotation, and trail lifetime must be audio
  controllable.
- Bass/onset should expand the path radius and increase stamp size.
- Treble should increase colour cycling speed and stamp sparkle.
- Stereo balance should shift the path centre.

### 2. Dense Raster / Colour Noise Fields

Seen in Untitled1 frames 308-380 and 607-675, Untitled2 frames 0-359 and
608-688, and Untitled3 frames 72-319.

Observed behaviour:

- Large parts of the screen are filled with short dashes, speckles, and blocky
  texture.
- Density can rise from roughly one third of the screen to almost full-screen
  coverage.
- Texture is not smooth noise; it has Amiga-like chunky pixels, horizontal
  dash patterns, and repeated short segments.
- Black holes or dark masked regions cut into otherwise dense fields.
- Colour is usually high-contrast against black, with strong green, blue,
  cyan, white, yellow, and magenta cycling.

Requirements:

- Add a dense field renderer built from short horizontal/vertical dash cells,
  not antialiased pixels.
- The field renderer must support density from sparse to near full-screen.
- The renderer must support black mask objects that occlude the field.
- Bass/onset should cause sudden density jumps and full-screen flashes.
- Treble should increase dash fragmentation and bright speckles.
- Palette cycling must be independent from particle movement.

### 3. Horizontal Scanline / Bit-Split Fields

Seen in Untitled1 frames 545-593 and Untitled2 frames 238-257 and 636-659.

Observed behaviour:

- The screen breaks into many short horizontal marks.
- Rows have different phase offsets, giving a bit-split or scanline feel.
- The image can look like decomposed video memory rather than geometric drawing.
- Motion is often sideways row drift plus palette shimmer.

Requirements:

- Add a scanline mode made from row buffers or deterministic pseudo-random row
  patterns.
- Each row must have an independent x phase and colour phase.
- Mid/bass should control row amplitude and row thickness.
- Treble should control horizontal fragmentation.
- Onset should trigger row re-seeding or abrupt row displacement.

### 4. Wireframe Vector Geometry

Seen in Untitled1 frames 169-187, 226-255, 460-495 and Untitled2 frames
380-477, 711-751, 812-849.

Observed behaviour:

- Thin lines form triangles, polygons, starbursts, tunnels, and irregular
  connected shapes.
- Many shapes are outline-only, sometimes with black filled objects on top.
- Geometry is often sparse, leaving a mostly black background.
- Perspective-like motion appears in Untitled2: line bundles converge to a
  vanishing point or slide like a grid.
- Lines are bright and have hard Amiga pixel edges.

Requirements:

- Add a vector layer supporting lines, polygon outlines, starbursts, tunnels,
  and simple black-filled masks.
- Shapes must be able to persist for several frames as trails.
- Bass/onset should spawn or expand large polygons.
- Mid should alter vertex count and line length.
- Treble should add fine jitter and small secondary lines.
- Pitch should select shape family or rotation speed.

### 5. Particle Fields

Seen in Untitled1 frames 594-606, Untitled2 frames 360-379 and 505-607, and
Untitled3 frames 0-102.

Observed behaviour:

- Many separate dots or tiny clusters move over black.
- Particles are coloured individually and can form loose mirrored clouds.
- Density is moderate; particles remain visually distinct from dense-noise
  modes.
- Some particles look like small brush glyphs rather than single pixels.

Requirements:

- Add a particle system with at least 128 logical particles for window mode.
- Particle primitives must include single pixels, short dashes, and tiny brush
  stamps.
- Bass/onset should emit burst particles from centre or edges.
- Treble should increase particle count and brightness.
- Stereo balance should bias particle direction or emitter position.

### 6. Vertical Waterfall / Hanging Streaks

Dominant in Untitled3 frames 103-121 and 290-490, also in Untitled2 frames
752-788 and 850-884.

Observed behaviour:

- The screen is filled with vertical hanging strokes or columns.
- Strokes are broken into short dash chains, not smooth filled bars.
- Colours cluster heavily around blue/cyan with green, purple, orange, and
  yellow accents.
- The lower edge of the field changes over time, as if the field is shrinking,
  dripping, or being vertically clipped.
- The motion is slower than dense flashes, more like falling or folding fabric.

Requirements:

- Add a waterfall renderer using many vertical dash columns.
- Each column must have independent length, y offset, colour phase, and decay.
- Bass should change column length and lower boundary.
- Treble should create gaps and sparkling dash breaks.
- Onset should reset selected columns or cause a downward pulse.
- A blue/cyan-dominant palette preset is required for this mode.

### 7. Drop Screens, Blanking, and Hard Cuts

Seen as dark holds and abrupt high-motion frames in all recordings, especially
Untitled1 frames 381-396 and Untitled2 frames 689-710.

Observed behaviour:

- Scene changes are often abrupt rather than crossfaded.
- Some transitions briefly blank most of the screen.
- Dense fields can appear immediately after a dark hold.
- This looks deliberate and performance-oriented, not a rendering error.

Requirements:

- The visual engine must support blankers/drop screens as first-class effects.
- Onset or evolve changes may trigger a hard cut, black hold, wipe, or sudden
  palette swap.
- Blank duration should be short and controllable, around 4-24 frames.
- The renderer must not require smooth transitions between every visual.

## Quantitative Requirements

These numbers are based on the per-frame CSV analysis and should be treated as
acceptance targets, not exact historical constants.

- A running show must include both sparse and dense states:
  - sparse vector/hold density below `0.18`
  - dense field density above `0.55`
  - occasional near-filled raster states above `0.80`
- The effect engine must produce at least five distinct visual families within
  a 30 second evolve run.
- Dominant palette families must include:
  - blue/cyan
  - green/yellow
  - white/grey
  - magenta/red accents
- At least one mode must be vertically biased, one horizontally biased, and one
  radially/symmetrically biased.
- High-motion transition frames must be allowed to exceed normal frame-to-frame
  motion by at least 3x; the original recordings contain abrupt jumps.
- Effects should be able to run with persistent trails; clearing the full
  screen every frame is not sufficient for the observed look.

## Audio Mapping Requirements

The original hardware appears to have exposed coarse band/direct-channel data,
not raw sampled audio. Our MOD-driven feature model maps well to that if the
visual engine treats features as control signals instead of exact waveform data.

- `bass`
  - drives scale, density jumps, lower-boundary movement, large mask changes,
    and scene impact.
- `mid`
  - drives geometry complexity, row amplitude, path wobble, and brush count.
- `treble`
  - drives sparkle, dash fragmentation, colour cycling speed, and jitter.
- `left` / `right`
  - drive stereo panning, emitter position, mirror imbalance, and tunnel
    vanishing point.
- `onset`
  - triggers hard cuts, drop screens, bursts, reseeding, and evolve decisions.
- `pitch`
  - selects shape family, path phase, or palette range.

## Engine Requirements

- Introduce explicit `VisualMode` / scene definitions instead of one monolithic
  render path.
- Add persistent visual state for trails, particles, row seeds, waterfall
  columns, palette phase, masks, and evolve timing.
- Add a deterministic PRNG so modes look alive without requiring large tables.
- Reserve the bottom status strip in window mode; effects should render only
  inside the visual area unless fullscreen mode is active.
- Keep all primitives compatible with classic Amiga drawing:
  - hard pixels
  - no antialiasing
  - short line segments
  - filled rectangles
  - polygon outlines
  - optional future Blitter acceleration
- Do not depend on Workbench having many pens. For the current Workbench-window
  version, effects must degrade gracefully to the available pens. Fullscreen
  custom screen can later add richer palette cycling.

## MVP Implementation Plan

1. Split `Visual_Render` into mode-specific renderers:
   - radial brush loop
   - dense raster/noise field
   - wireframe vectors
   - particle field
   - vertical waterfall
2. Add `Visual_NextScene` and evolve selection over a scene table rather than
   a simple scene index.
3. Add persistent state arrays:
   - particles
   - row phases/seeds
   - waterfall columns
   - brush path phase
4. Add blanker/drop-screen transition state.
5. Add a small preset structure for complete moods, even before file save/load
   exists.
6. Add mode-specific palette presets using the existing pens first.
7. Later, when fullscreen/custom-screen support exists, add true palette
   cycling and more colours.

## Non-goals For The Next Pass

- Exact pixel-perfect cloning of original MindLight visuals.
- IFF brush/backdrop loading.
- Genlock/LIVE-style video feedback.
- AGA 256-colour mode.
- Fullscreen-only effects.

Those are historically relevant, but the next useful step is to reproduce the
observed effect families and their audio-driven behaviour in the Workbench
window.
