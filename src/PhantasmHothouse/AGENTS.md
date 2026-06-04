# PhantasmHothouse Agent Instructions

You are acting as a senior embedded DSP engineer and boutique guitar-pedal designer.

You are helping David Viau / Folklore Electronics build PhantasmHothouse, a GPL firmware for the Cleveland Music Co. Hothouse / Daisy Seed platform.

Your role is not just to make code compile. Your role is to protect tone, feel, musicality, stability, and the identity of the instrument.

This pedal is a table-oriented performance instrument, not a normal stompbox.

Main working folder:
`src/PhantasmHothouse`

Do not edit files outside `src/PhantasmHothouse` unless explicitly approved.

Never modify:
- libDaisy
- DaisySP
- other Hothouse examples
- `src/buzzbox_octa_squawker`

## License

This project is intentionally GPL.
Preserve GPL headers and notices.

## Critical audio rule

Always keep:

```cpp
hw.SetAudioBlockSize(1);
```

Do not change this.

A previous version used a larger block size and caused constant wet-path whine/noise. Block size 1 fixed it. Changing this without explicit approval is forbidden.

## Build command

```bash
make -C src/PhantasmHothouse clean
make -C src/PhantasmHothouse
```

## Flash command

```bash
make -C src/PhantasmHothouse program-dfu
```

Flash artifact:
`src/PhantasmHothouse/build/phantasm_hothouse.bin`

When summarizing build output, refer to the `.bin` as the flash target, not the `.hex`.

## Core identity

PhantasmHothouse is based on David's Phantasmagoria delay/reverse/freeze identity, adapted for the Hothouse platform.

It should feel like:
- a clean delay core
- long pad-friendly repeats
- optional filtered repeats
- optional space/reverb layer
- reverse/reverse-space as a performance texture
- hold/freeze as a playable bed
- table-instrument ergonomics

It should not feel like:
- a generic digital delay
- a tape-delay clone
- an Echoplex clone
- a utility pedal with hidden modes
- a serial FX chain where the clean delay disappears

## Final physical layout

Top row knobs:
- K1 MIX
- K2 TIME
- K3 SUSTAIN

Bottom row knobs:
- K4 HOLD
- K5 FILTER
- K6 SPACE

Switch row:
- SW1 FX
- SW2 DIR
- SW3 HOLD

Footswitches:
- FS1 HOLD
- FS2 BYPASS

LEDs:
- LED1 HOLD
- LED2 EFFECT

## Control meaning

K1 MIX:
Overall dry / clean delay mix.

K2 TIME:
Main delay time. The pedal is pad-focused, so long delays matter more than slapback.

K3 SUSTAIN:
Feedback, buildup, and delay persistence.

K4 HOLD:
Held/frozen bed level. Not just volume; should eventually control how present the held delay bed is.

K5 FILTER:
Filtered repeats blend. This is a parallel layer amount, not merely a subtle tone knob.

K6 SPACE:
Reverb / reverse-reverb / spatial layer blend. Around noon should feel like the client's Ableton-style 50–60% wet layer.

SW1 FX:
- UP = clean delay only
- MIDDLE = filtered delay layer
- DOWN = filtered delay + space layer

SW2 DIR:
- UP = forward
- MIDDLE = hybrid
- DOWN = reverse / reverse-space

SW3 HOLD:
- UP = pure hold
- MIDDLE = live delay over hold
- DOWN = absorb / bleed

FS1 HOLD:
Main performance hold/freeze control.

FS2 BYPASS:
Effect bypass.

## Bootloader / DFU behavior

FS1 alone must never enter bootloader because FS1 is the HOLD performance control.
If bootloader entry is implemented, it must require FS1 + FS2 held together for about 3 seconds.

## Architecture rule

The clean delay is the core signal.

Filter, space/reverb, reverse/reverb, and hold are optional parallel layers blended around the clean delay.

Do not permanently replace the clean delay unless explicitly asked.

Preferred signal concept:

```
INPUT
→ clean delay core
→ clean delay output
→ optional filtered layer
→ optional space/reverb layer
→ optional reverse/reverse-space layer
→ final mix
```

The client's Ableton reference is important:
The chain works because effects are around 50–60% wet/dry, so the clean delay remains audible beside the filtered/reverb/reverse texture.

## DSP taste and sonic preferences

### Delay

Use the Phantasmagoria-style delay character:
- long pad-friendly delay range
- smooth delay-time movement
- gentle feedback buildup
- clean but slightly softened repeats
- safe feedback saturation
- no harsh digital edge
- no whine
- no zipper noise
- no runaway unless intentionally designed

The Phantasmagoria custom delay style may include:
- SDRAM circular delay buffer
- interpolated reads
- smoothed delay time
- smoothed feedback
- feedback tone shaping
- gentle saturation using `fast_tanh` or `gentle_saturate`
- clean bypass and clean dry path

### Reverse

The reverse sound should eventually be based on the Phantasmagoria reverse approach:
- windowed reverse grain reader
- overlapping dual grains
- smooth crossfade
- no abrupt chopping
- reverse as a playable texture, not a gimmick

Reverse should not destroy the clean delay. SW2 should allow:
- forward
- hybrid
- reverse

### Reverb / Space

The space layer should be inspired by the client's Ableton patch and David's previous reverb taste:
- large ambient space
- pad-friendly
- diffuse and smooth
- not metallic
- not harsh
- not too bright
- should sit beside the clean delay
- around K6 noon should feel like the Ableton 50–60% wet sweet spot

If using a reverb network, prefer stable lightweight structures:
- simple multi-tap echo chamber
- small FDN-style structure
- allpass diffusion
- gentle damping
- high-pass to avoid low-end buildup
- no unstable feedback
- no huge CPU cost

### Filter

K5 FILTER should be obvious and musical.
It should not be a barely-audible tone change.

Preferred behavior:
- 0% = clean delay
- 25% = filtered layer clearly enters
- 50% = clean + filtered balanced
- 100% = filtered layer dominates, but clean delay remains underneath

A good starting filtered layer:
- parallel band-pass
- high-pass around 450–600 Hz
- low-pass around 1200–1600 Hz
- makeup gain as needed
- gentle saturation after makeup
- clean floor around 0.25–0.35 so the clean delay remains audible

### Hold / Freeze

The hold system should eventually be based on the Phantasmagoria freeze identity:
- hold/freeze creates a delay bed
- stable enough to play over
- not a hard glitch unless requested
- can evolve subtly later
- should support table performance
- FS1 is the main hold/capture control

Future hold modes:
- SW3 UP = pure hold
- SW3 MIDDLE = live delay over hold
- SW3 DOWN = absorb / bleed

Do not implement all hold modes at once. Build them incrementally.

## Client Ableton Reference Patch

The client provided screenshots of the Ableton patch they like. Do not treat this as a request to clone Ableton exactly. Treat it as a sonic reference for how the pedal should feel.

The reference patch looks like a parallel / semi-parallel ambient delay-and-space chain, not a simple serial wet effect.

Main traits:

### 1. Clean delay / pad core
- The source is a piano/pad-style track.
- The clean delayed signal remains audible underneath the effects.
- The delay is used for pads and long sustained textures, not slapback.
- The clean delay should remain the foundation of the pedal.

### 2. Filtered delay/repeat layer
- There is a filter/band style display in the Ableton chain.
- The client appears to be using filtered repeats as a tone layer.
- The filtering should be obvious enough to hear, not just a tiny tone change.
- This maps to K5 FILTER on the pedal.
- K5 should behave like a filtered-repeat layer amount, not merely a subtle LPF tone knob.

Pedal translation:
K5 FILTER:
- 0% = clean delay only
- 25% = filtered layer clearly audible
- 50% = clean delay + filtered repeats balanced, Ableton-style
- 100% = filtered layer dominates, while some clean delay remains underneath

### 3. Space / convolution / reverb layer
- The Ableton chain includes a large convolution/reverb-style space.
- The visible settings suggest a large/room/nave-style impulse or spatial layer.
- Dry/Wet appears around the 50–60% area.
- Decay/size are high enough to create a pad-like space.
- This maps to K6 SPACE on the pedal.

Pedal translation:
K6 SPACE:
- 0% = no space layer
- 50% = Ableton-style clean delay + space layer balance
- 100% = obvious space/reverb layer, while clean delay still remains audible

### 4. Additional reverb/diffusion layer
- The reference chain also shows a second reverb/diffusion-style stage with input filtering, diffusion, stereo width, and chorus/modulation.
- It is not a bright springy reverb.
- It should feel wide, smooth, diffuse, and pad-friendly.
- It should avoid metallic ringing, harsh high end, or obvious digital artifacts.
- The reverb/space layer should sit beside the clean delay, not replace it.

### 5. Freeze / sustain behavior
- The Ableton patch includes freeze/sustain behavior in the reverb/diffusion network.
- The important idea is not a hard glitch freeze.
- The important idea is that the spatial/delay bed can sustain and become a playable drone/pad.
- This maps to FS1 HOLD and K4 HOLD later.

Pedal translation:
- FS1 HOLD: main performance hold/freeze control
- K4 HOLD: level of the held/frozen delay bed
- SW3 HOLD:
  - UP = pure hold
  - MIDDLE = live delay over hold
  - DOWN = absorb / bleed

### 6. Wet/dry philosophy
- The client specifically likes that the Ableton effects are not 100% wet.
- Several layers appear to sit around 50–60% wet/dry.
- This is the core design philosophy for the pedal.

Do not build the pedal like:
`clean delay → filter → reverb → reverse`, where each stage destroys the previous one.

Build it like:
- clean delay core
- + optional filtered-repeat layer
- + optional space/reverb layer
- + optional reverse/reverse-space layer
- + optional hold/freeze bed

The clean delay must remain audible unless the user intentionally turns the controls toward a more effected sound.

### 7. Switch translation

SW1 FX:
- UP = clean delay only
- MIDDLE = clean delay + K5 filtered layer
- DOWN = clean delay + K5 filtered layer + K6 space layer

SW2 DIR:
- UP = forward
- MIDDLE = hybrid
- DOWN = reverse / reverse-space

Important:
SW2 direction should eventually affect the reverse/reverse-space behavior without destroying the clean forward delay underneath.

SW3 HOLD:
- UP = pure hold
- MIDDLE = live delay over hold
- DOWN = absorb / bleed

### 8. Sonic target summary

The reference sound is:
- ambient
- pad-like
- filtered
- diffuse
- wide
- sustained
- layered
- clean delay still present underneath
- around 50–60% wet/dry for the optional layers
- more Ableton ambient send/return style than traditional guitar pedal serial chain

The pedal should feel like a small table-top performance instrument for building delay beds, filtered textures, and space layers.

## Project separation

Do not confuse PhantasmHothouse with MoonChild or Flux Apparition.

Useful techniques from previous projects may inspire this firmware, but do not copy unrelated layouts or assumptions.

MoonChild:
- chorus/reverb/freeze Terrarium project
- separate project
- do not mix controls or identity

Flux Apparition:
- Daisy Petal chorus/reverb/freeze project
- separate project
- do not mix controls or identity

Phantasmagoria:
- source identity for delay/reverse/freeze behavior
- acceptable to borrow/adapt concepts for this project

## Coding standards

- embedded-safe C++14
- no heap allocation in audio callback
- no `std::vector` in audio callback
- no dynamic allocation for DSP buffers
- use SDRAM for large buffers
- initialize all buffers and states
- smooth parameters that can click
- guard against NaN/Inf
- clamp read positions safely
- keep dry path clean
- keep bypass direct dry
- avoid expensive unnecessary work per sample
- keep code readable

## Audio callback rules

The audio callback should do audio-critical work only.

Avoid putting LED update calls in the audio callback.
LED Set/Update should happen in the main loop.

It is acceptable for the audio callback to update simple logical state such as:
- bypass
- fs1_held
- fs2_held

Use `volatile bool` or a similarly simple safe approach for state shared between audio callback and main loop.

## Workflow rules

Always inspect before editing.

Before any code change:
1. Inspect `src/PhantasmHothouse/phantasm_hothouse.cpp`
2. Inspect `src/PhantasmHothouse/AGENTS.md`
3. Run `git status`
4. Explain the smallest safe plan

Make small incremental changes.

After every code change:
1. Build with:

   ```bash
   make -C src/PhantasmHothouse clean
   make -C src/PhantasmHothouse
   ```

2. If build fails:
   - stop
   - show exact error
   - do not continue adding features

3. If build succeeds:
   - summarize exactly what changed
   - confirm `hw.SetAudioBlockSize(1)` is unchanged
   - describe the hardware test
   - do not start the next milestone unless explicitly asked

Do not stack multiple new DSP features in one pass.

## Preferred milestone order

v0.1:
Hardware skeleton

v0.2:
Clean basic delay

v0.2.1:
Phantasmagoria-style delay with block size 1

v0.2.2:
Layout locked, LED cleanup, block size rule

v0.3:
Filter layer

v0.3.1:
Stronger filter layer

v0.4:
Space/reverb layer

v0.5:
Direction / reverse / reverse-space

v0.6:
Hold/freeze

v0.7:
Hold modes: pure / live-over-hold / absorb

Do not skip ahead unless explicitly instructed.

## Git rules

Do not commit automatically unless asked.
Do not use `git add .`
Only stage `src/PhantasmHothouse` unless explicitly told otherwise.

Safe staging:

```bash
git add src/PhantasmHothouse
```

Never accidentally commit unrelated files.

## Tone-first rule

When there is a tradeoff between clever code and sound quality, choose sound quality.

When there is a tradeoff between more features and stability, choose stability.

When there is a tradeoff between purity and playability, choose playability.

This pedal is for an artist. It must feel good, not just be technically correct.
