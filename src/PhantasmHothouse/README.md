# PhantasmHothouse

Contributed by David Viau \<<dalexviau@gmail.com>\>

GPL firmware for Cleveland Music Co. Hothouse / Daisy Seed.

**Current baseline:** [`phantasm-hothouse-v0.8f`](https://github.com/FuzzyLotus/PhantasmHothouse/releases/tag/phantasm-hothouse-v0.8f)

Phantasmagoria-inspired reverse/freeze delay. Not an Echoplex clone.

## Description

Pad-focused delay with parallel filter, space, reverse direction, and delay-line freeze hold modes.

### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | MIX | Dry / wet |
| KNOB 2 | TIME | Delay time, pad curve (20–4000 ms) |
| KNOB 3 | SUSTAIN | Feedback |
| KNOB 4 | HOLD | Frozen wet level |
| KNOB 5 | FILTER | Filtered repeats blend |
| KNOB 6 | SPACE | Reverb / space blend (SW1 DOWN) |
| SWITCH 1 | FX | **UP** clean / **MID** filtered / **DOWN** filtered+space |
| SWITCH 2 | DIR | **UP** forward / **MID** hybrid / **DOWN** reverse |
| SWITCH 3 | HOLD | **UP** pure freeze / **MID** live-over / **DOWN** absorb + live-over |
| FOOTSWITCH 1 | HOLD | Live: tap = freeze. Frozen: short tap = soft release; hold ~250 ms = kill |
| FOOTSWITCH 2 | Bypass / Soft Veil | Short tap = bypass. Hold while frozen = delay bloom (warm stack / soft self-osc) |
| FS1 + FS2 | Bootloader | Hold both ~3 s |

### Build

```bash
make -C src/PhantasmHothouse clean
make -C src/PhantasmHothouse
```

Audio block size must remain `1`.
