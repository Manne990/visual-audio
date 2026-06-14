# ProTracker 3.61 bridge notes

Goal: make Visual Audio react to music played from ProTracker 3.61.

## Difficulty

A reliable bridge is medium difficulty if ProTracker cooperates, and high difficulty if
we try to observe an unmodified running ProTracker from the outside.

The practical reason is that Amiga audio is output-only from the point of view of a
normal application. `audio.device` arbitrates the four Paula channels, but many trackers
drive Paula directly. Custom chip registers are also not a clean system-wide state API:
the hardware docs treat registers as read-only or write-only, not as a safe reflection
of what another program last wrote.

## Best options

1. **Integrated MOD playback in Visual Audio**
   - Most reliable.
   - Use a ProTracker/MOD player inside this program.
   - Feed the visual engine from channel volume, period, sample, and row events.
   - This should be the first real music backend.

2. **Cooperative ProTracker bridge**
   - Good if we can patch ProTracker 3.61, use source, or add a small export hook.
   - ProTracker remains the editor/player.
   - On every tick or row, it writes a compact shared state block:
     - four channel volumes
     - four channel periods
     - sample numbers
     - row/position
     - effect command/value
   - Visual Audio reads that block and maps it to `AudioFeatures`.

3. **External Paula snooper**
   - Not a dependable first implementation.
   - Would require interrupt/vector patching, player-specific knowledge, or unsafe
     assumptions about write-only hardware registers.
   - Useful only as an experiment after the visual engine is already working.

## Recommended bridge protocol

Keep the bridge deliberately small and 68000-friendly:

```c
struct PTBridgeState {
    UWORD magic;          /* 'PT' or similar */
    UWORD version;
    ULONG frame;
    UBYTE volume[4];     /* 0..64 */
    UWORD period[4];
    UBYTE sample[4];
    UBYTE effect[4];
    UBYTE effectValue[4];
    UBYTE songPosition;
    UBYTE patternRow;
};
```

The Visual Audio side should not care whether this state came from ProTracker, an
internal MOD player, a serial link, or a controller-port device. It should only convert
the state into the existing `AudioFeatures` fields.

