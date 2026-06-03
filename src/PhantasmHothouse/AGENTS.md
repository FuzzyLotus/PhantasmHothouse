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
  ```

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
