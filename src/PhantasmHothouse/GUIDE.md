# PhantasmHothouse User Guide

This is a short guide for someone using the pedal for the first time. It explains
what each control does, how the freeze works, and what is still being built.

PhantasmHothouse is a delay pedal with reverse, space, and freeze layers. It is
made for pads, long repeats, and sustained ambient beds. The clean delay is the
core of the sound, and the other layers sit around it. It is not a slapback or
tape echo clone.

## Quick start

1. Plug your instrument into the input and your amp or interface into the output.
2. Set K1 (MIX) to about noon so you hear dry and wet together.
3. Set K2 (TIME) to about noon for a medium delay.
4. Set K3 (SUSTAIN) low at first so repeats fade instead of piling up.
5. Leave K4, K5, K6 low, and set all three switches to their UP position.
6. Play. You now have a clean delay. From here you can bring in the other layers.

## Knobs

Top row:

- K1 MIX: Balance between your dry signal and the effect. Fully left is dry only.
  As you turn right, more of the wet delay and layers come through.
- K2 TIME: Delay time. Short settings give tight repeats. Long settings give
  slow, spread out repeats. The range goes from about 20 ms up to 4 seconds.
- K3 SUSTAIN: How much the delay feeds back on itself. Low settings give a few
  repeats that fade. Higher settings give long trails and buildup. It is held
  below runaway so it stays under control.

Bottom row:

- K4 HOLD: Level of the frozen bed when freeze is on. Fully left is close to off.
  Noon is roughly the same level as the normal wet. Fully right is a modest boost.
  It only affects the sound while freeze is active.
- K5 FILTER: Amount of the filtered repeat layer. At zero you get the clean
  delay. As you turn it up, a band filtered version of the repeats comes in and
  becomes the main character near the top.
- K6 SPACE: Amount of the reverb and space layer. Around noon it feels like a
  balanced wet space sitting next to the clean delay. This layer is only added
  when SW1 is in the DOWN position.

## Switches

- SW1 FX: Chooses which layers are active.
  - UP: clean delay only.
  - MIDDLE: clean delay plus the filtered layer (K5).
  - DOWN: clean delay plus the filtered layer plus the space layer (K6).
- SW2 DIR: Chooses the direction of the wet delay voice.
  - UP: forward.
  - MIDDLE: hybrid, forward with reverse around it.
  - DOWN: reverse focused.
- SW3 HOLD: Chooses how freeze behaves.
  - UP: pure freeze. The frozen bed stays as it is and does not take in new playing.
  - MIDDLE: live over freeze. The bed holds while you play a normal delay on top.
  - DOWN: absorb and bleed. New playing slowly seeps into the frozen bed.

## Footswitches

- FS1 HOLD: This is the main performance control for freeze.
  - Tap while not frozen: turns freeze on. The current delay bed is held and
    keeps sustaining.
  - Tap while frozen: soft release. The frozen bed fades out over a short time
    and the pedal returns to a normal delay. New playing enters the delay again
    within a fraction of a second.
  - Press and hold about a quarter second while frozen: emergency kill. The
    frozen bed cuts out fast and cleanly, and the delay comes back ready to use.

- FS2 BYPASS:
  - Tap: toggles bypass. When bypassed the output is your dry signal only.
  - Hold while frozen: Soft Veil. The frozen bed blooms into a warm, smeared
    delay cloud (stacking / soft self-oscillation). Release and the bloom fades
    while the locked freeze underneath stays. Live delay over freeze
    (SW3 MIDDLE/DOWN) keeps playing normally on top.

Bootloader: holding FS1 and FS2 together for about 3 seconds puts the pedal into
update mode. FS1 by itself never does this, because FS1 is a performance control.

## LEDs

- LED1 HOLD: On when freeze is active, off when it is not.
- LED2 EFFECT: Effect state indicator.

## Flashing for the first time (blank Daisy Seed)

If the Daisy Seed is blank or is running different firmware, the FS1 plus FS2
footswitch combo will not work yet, because that combo depends on this firmware
already running. On a blank board you enter update mode with the two small
buttons on the Daisy Seed itself.

The two buttons on the Daisy Seed are BOOT and RESET.

1. Connect the Daisy Seed to your computer with a USB cable that carries data.
2. Put the board into DFU mode with the buttons:
   - Press and hold the BOOT button.
   - While still holding BOOT, press and release the RESET button.
   - Release the BOOT button.
   The board is now waiting in DFU mode. Your computer should see a device named
   something like STM32 BOOTLOADER.
3. Send the firmware. You have two easy options:
   - Web programmer: open the Daisy web programmer in a supported browser, connect
     to the board, and upload `phantasm_hothouse.bin`.
   - Command line: from the project, run `make -C src/PhantasmHothouse program-dfu`.
     This uses dfu-util to write the firmware, so dfu-util needs to be installed.
4. When the write finishes, the pedal restarts and runs PhantasmHothouse. The
   LEDs and footswitches behave as described in this guide.

You only need the button method for the very first flash or if the board ever
gets into a state where the firmware is not running. After PhantasmHothouse is
installed, later updates can use the FS1 plus FS2 combo instead of the buttons.

## Updating the firmware later

Once the pedal already runs PhantasmHothouse, put it into update mode using the
FS1 plus FS2 hold described above, then load `phantasm_hothouse.bin` over DFU with
either the web programmer or `make -C src/PhantasmHothouse program-dfu`.

If you are building from source, the commands are:

```
make -C src/PhantasmHothouse clean
make -C src/PhantasmHothouse
```

The built file lands in `src/PhantasmHothouse/build/phantasm_hothouse.bin`.

## What still needs to be implemented

This is a work in progress. The items below are known and planned.

- Optimization pass: enable flush to zero for denormals to avoid rare processing
  spikes on long fades, reduce some per sample math, and review the output
  headroom limit. These are planned and were set aside to fix the freeze bugs first.
- Freeze evolution: the DOWN absorb and bleed behavior and other slow evolving
  textures may be refined further.
- Retuning the delay time while a freeze is held is only an idea for now and is
  not implemented, since it can cause pitch artifacts on this freeze design.
- General polish toward a tagged v1.0 release once the above are settled.

## A few tips

- Start simple. Get a sound you like on the clean delay before adding filter or
  space.
- K3 controls how long things last. If the sound builds up too much, bring it down.
- Freeze is most useful for holding a chord or texture and then playing over it
  with SW3 in the MIDDLE position.
- If a freeze ever gets away from you, hold FS1 for the emergency kill to reset
  the bed quickly.
