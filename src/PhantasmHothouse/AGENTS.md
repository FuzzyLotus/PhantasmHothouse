# PhantasmHothouse

## Platform

Cleveland Music Co. Hothouse / Daisy Seed firmware project.

## License

GPL project intentionally. Preserve existing GPL notices where applicable.

## Main Working Folder

`src/PhantasmHothouse` only.

## Important Rules

- Do not edit files outside `src/PhantasmHothouse` unless I explicitly approve it.
- Do not modify `libDaisy`, `DaisySP`, or other example projects.
- Do not touch `src/buzzbox_octa_squawker`.
- Always inspect before editing.
- Make small incremental changes.
- After every code change, run:

  ```bash
  make -C src/PhantasmHothouse clean
  make -C src/PhantasmHothouse
  make -C src/PhantasmHothouse program-dfu
  ```

- The flash artifact for this project is `src/PhantasmHothouse/build/phantasm_hothouse.bin`.
  When summarizing build output, refer to the `.bin` as the flash target, not
  the `.hex`. Flash via `make -C src/PhantasmHothouse program-dfu` (uses the
  `.bin`); do not instruct flashing the `.hex`.
- If the build fails, stop and show the exact error.
- Do not continue adding features on top of a broken build.
- Keep the code readable and suitable for embedded real-time audio.
- Avoid heap allocation in the audio callback.
- Avoid expensive unnecessary work in the per-sample loop.
- Use parameter smoothing for anything that can click.
- Use SDRAM for large delay/freeze buffers.
- Always use `hw.SetAudioBlockSize(1)` for this project. A previous v0.2 delay
  build used block size 48 and produced a constant whine whenever wet signal
  was present; block size 1 fixed it. Do not change the audio block size
  without explicitly asking first.
- Keep the dry path clean and preserve amp feel.
- This is not an Echoplex/tape-delay clone.
- Do not copy the Echoplex DSP identity from the Hothouse examples.
- The final identity is a Phantasmagoria-inspired reverse/freeze delay for Hothouse.

## Final Physical Control Layout (LOCKED)

This is the locked physical layout. Controls may be read/smoothed before their
feature exists, but their final meaning and physical position must not be
reassigned.

Physical grouping concept:
- Top row knobs = clean delay engine.
- Bottom row knobs = performance layers.
- Switch row = mode / world selectors.

```
Top row knobs:      K1 MIX        K2 TIME       K3 SUSTAIN
Bottom row knobs:   K4 HOLD       K5 FILTER     K6 SPACE
Switch row:         SW1 FX        SW2 DIR       SW3 HOLD
Footswitches:       FS1 HOLD      FS2 BYPASS
```

| Control | Row | Name | Behavior |
|---------|-----|------|----------|
| K1 | top | MIX | Overall dry / clean delay mix. |
| K2 | top | TIME | Delay time, pad-focused. |
| K3 | top | SUSTAIN | Feedback / buildup. |
| K4 | bottom | HOLD | Held bed level. |
| K5 | bottom | FILTER | Filtered repeats blend. |
| K6 | bottom | SPACE | Reverb / reverse-reverb blend. |
| SW1 | switch | FX | UP = clean delay only; MIDDLE = filtered delay layer; DOWN = filtered delay + space/reverb layer. |
| SW2 | switch | DIR | UP = forward; MIDDLE = hybrid; DOWN = reverse / reverse space. |
| SW3 | switch | HOLD | UP = pure hold; MIDDLE = live delay over hold; DOWN = absorb / bleed. |
| FS1 | footsw | HOLD | Hold/freeze performance control. |
| FS2 | footsw | BYPASS | Effect on/off. |
| LED1 | led | HOLD | Hold/freeze status. |
| LED2 | led | EFFECT | Bypass/engaged status. |

### DSP Architecture Note

- The clean delay is the core signal (top row engine).
- Filter, space/reverb, reverse/reverb, and hold are optional layers blended
  around the clean delay (bottom row + switches).
- These layers must NOT permanently replace the clean delay; they sit beside it.

### LED Handling Rule

- `AudioCallback` may update logical state such as `bypass` and `fs1_held`.
- `AudioCallback` must NOT call `led_*.Set()` or `led_*.Update()`.
- LED Set/Update calls live in the `main` while loop.
- `bypass` and `fs1_held` are `volatile bool` (written in audio, read in main).

## Milestones

### v0.1 Clean Hardware Skeleton

- clean mono passthrough
- FS2 bypass toggle
- LED2 effect/bypass state
- FS1 placeholder detection
- LED1 lit while FS1 is held
- read all 6 knobs
- read all 3 toggles
- no real DSP yet

### v0.2 Basic Delay

- SDRAM delay buffer
- mix
- time
- feedback
- tone
- delay range toggle
- smoothing
- safe feedback limiting

### v0.3 Reverse Delay

- forward/reverse read behavior
- direction toggle
- smooth crossfade
- no clicks when switching direction

### v0.4 Freeze

- FS1 freeze behavior
- freeze capture
- freeze playback
- LED1 freeze indication
- freeze level control
- reverse works with freeze

### v0.5 Accumulate / Performance Behavior

- hold-to-accumulate
- safe layer level
- musical degradation/evolution options
- freeze character toggle

### v1.0 Release

- README
- controls documentation
- build instructions
- flashing notes
- CHANGELOG
- known limitations
