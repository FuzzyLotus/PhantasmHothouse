// PhantasmHothouse for Hothouse DIY DSP Platform
// Copyright (C) 2024 David Viau <dalexviau@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// ============================================================
// Final physical control layout (LOCKED; see AGENTS.md)
// ============================================================
// Top row knobs (clean delay engine):
//   K1 MIX     Overall dry / clean delay mix.
//   K2 TIME    Delay time, pad-focused.
//   K3 SUSTAIN Feedback / buildup.
// Bottom row knobs (performance layers):
//   K4 HOLD    Held bed level.                  (not implemented yet)
//   K5 FILTER  Filtered repeats blend.          (v0.3.1: parallel band-pass layer)
//   K6 SPACE   Reverb / reverse-reverb blend.   (read/smoothed, not used yet)
// Switch row (mode / world selectors):
//   SW1 FX     UP clean / MID filtered / DOWN filtered+space(reserved).
//   SW2 DIR    UP forward / MID hybrid / DOWN reverse.        (read, unused)
//   SW3 HOLD   UP pure hold / MID live-over-hold / DOWN absorb-bleed. (unused)
// Footswitches:
//   FS1 HOLD   Hold/freeze performance control. (placeholder: LED1 only)
//   FS2 BYPASS Effect on/off.
// LEDs:
//   LED1 HOLD   Hold/freeze status.
//   LED2 EFFECT Bypass/engaged status.
//
// DSP architecture: the clean delay (top row) is the core signal. Filter,
// space/reverb, reverse, and hold are optional PARALLEL layers blended around
// the clean delay (bottom row + switches); they never permanently replace it.
// Only the clean delay + tone exist so far.

// ### Uncomment if IntelliSense can't resolve DaisySP-LGPL classes ###
// #include "daisysp-lgpl.h"

#include <cmath>
#include <cstring>

#include "daisysp.h"
#include "hothouse.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;
using daisysp::fclamp;
using daisysp::fonepole;

// ============================================================
// Constants
// ============================================================
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr float kHalfPi = 1.57079632679489661923f;

// Phantasmagoria-style main buffer: 96000 samples == 2.0 s at 48 kHz.
static constexpr size_t kMaxDelay = 96000;

static constexpr float kTimeMinMs = 20.0f;
static constexpr float kTimeMaxMs = 1800.0f;

static constexpr float kMaxFeedback = 0.78f;

// Fixed tone of the clean delay / feedback path (was K5-driven before v0.3).
static constexpr float kFeedbackToneHz = 8000.0f;

// Parallel FILTER layer (K5): an explicit band-pass for an obvious, characterful
// "filtered repeats" texture rather than a subtly-darker delay.
static constexpr float kFilterHpHz = 500.0f;     // high-pass corner (narrow mid band)
static constexpr float kFilterLpHz = 1400.0f;    // low-pass corner
static constexpr float kFilterMakeup = 3.5f;     // band-pass loses energy; compensate
// Clean delay retained underneath even at K5 = 100% (stays audible, but the
// filtered layer clearly dominates at max).
static constexpr float kFilterCleanFloor = 0.25f;

// ============================================================
// Fast math / saturation (Phantasmagoria feel)
// ============================================================
static inline float fast_tanh(float x) { return x / (1.0f + fabsf(x)); }

static inline float gentle_saturate(float x) {
  float ax = fabsf(x);
  if (ax < 0.8f) return x;
  float sign = (x >= 0.0f) ? 1.0f : -1.0f;
  return sign * (0.8f + (ax - 0.8f) / (1.0f + (ax - 0.8f) * 0.5f));
}

static inline float LogLerpMs(float min_ms, float max_ms, float t) {
  t = fclamp(t, 0.0f, 1.0f);
  return min_ms * powf(max_ms / min_ms, t);
}

// ============================================================
// One-pole low-pass tone shaper (Phantasmagoria Lp1)
// ============================================================
struct Lp1 {
  float y = 0.0f;
  float c = 1.0f;

  void Init(float freq, float sr) {
    y = 0.0f;
    SetFreq(freq, sr);
  }
  void SetFreq(float freq, float sr) { c = 1.0f - expf(-kTwoPi * freq / sr); }
  inline float Process(float x) {
    y += c * (x - y);
    return y;
  }
};

// One-pole high-pass (returns input minus its low-passed part).
struct Hp1 {
  float y = 0.0f;
  float c = 1.0f;

  void Init(float freq, float sr) {
    y = 0.0f;
    SetFreq(freq, sr);
  }
  void SetFreq(float freq, float sr) { c = 1.0f - expf(-kTwoPi * freq / sr); }
  inline float Process(float x) {
    y += c * (x - y);
    return x - y;
  }
};

// ============================================================
// Custom circular delay buffer (Phantasmagoria DelBuf)
// ============================================================
struct DelBuf {
  float* buf = nullptr;
  size_t len = 0;
  size_t wp = 0;

  void Init(float* mem, size_t n) {
    buf = mem;
    len = n;
    wp = 0;
    memset(buf, 0, n * sizeof(float));
  }

  inline void Write(float s) {
    buf[wp] = s;
    wp = (wp + 1) % len;
  }

  inline float Read(float samps) const {
    samps = fclamp(samps, 1.0f, static_cast<float>(len - 2));
    float r = static_cast<float>(wp) - samps;
    if (r < 0.0f) r += static_cast<float>(len);
    int i0 = static_cast<int>(r);
    float fr = r - static_cast<float>(i0);
    float a = buf[i0 % len];
    float b = buf[(i0 + 1) % len];
    return a * (1.0f - fr) + b * fr;
  }
};

// ============================================================
// SDRAM storage and DSP objects
// ============================================================
static float DSY_SDRAM_BSS main_buf[kMaxDelay];

Hothouse hw;
DelBuf main_delay;
Lp1 tone_lpf;     // fixed tone on the clean delay / feedback path
Hp1 filter_hp;    // parallel FILTER-layer band-pass: high-pass stage (K5)
Lp1 filter_lp;    // parallel FILTER-layer band-pass: low-pass stage (K5)

// Hardware state retained for later DSP milestones.
Led led_freeze, led_effect;
// Written in AudioCallback, read in main — volatile for cross-context visibility.
volatile bool bypass = true;
volatile bool fs1_held = false;
float knob_values[Hothouse::KNOB_LAST] = {};
Hothouse::ToggleswitchPosition toggle_positions[3] = {
    Hothouse::TOGGLESWITCH_UNKNOWN, Hothouse::TOGGLESWITCH_UNKNOWN,
    Hothouse::TOGGLESWITCH_UNKNOWN};

float sample_rate = 48000.0f;
float fb_sig = 0.0f;

// ============================================================
// Smoothed parameters
// ============================================================
struct Smoothed {
  float current;
  float target;
  float coeff;
  inline void Tick() { fonepole(current, target, coeff); }
};

Smoothed s_delay{2400.0f, 2400.0f, 0.0002f};  // samples — slow, click-free
Smoothed s_feedback{0.0f, 0.0f, 0.0010f};      // K3 sustain / feedback
Smoothed s_mix{0.0f, 0.0f, 0.0010f};           // K1 mix
Smoothed s_filter{0.0f, 0.0f, 0.0010f};        // K5 FILTER blend (0..1)
Smoothed s_fx_gate{0.0f, 0.0f, 0.0015f};       // SW1 FX enable (click-free)
Smoothed s_space{0.0f, 0.0f, 0.0010f};         // K6 SPACE — read/smoothed, unused

// ============================================================
// K2 TIME — pad-focused piecewise curve
// ============================================================
//   0% – 15%  :   20 ms →  350 ms
//  15% – 55%  :  350 ms → 1200 ms
//  55% – 100% : 1200 ms → 1800 ms
static float KnobToDelayMs(float knob) {
  knob = fclamp(knob, 0.0f, 1.0f);
  if (knob <= 0.15f) {
    return LogLerpMs(20.0f, 350.0f, knob / 0.15f);
  }
  if (knob <= 0.55f) {
    return LogLerpMs(350.0f, 1200.0f, (knob - 0.15f) / 0.40f);
  }
  return LogLerpMs(1200.0f, kTimeMaxMs, (knob - 0.55f) / 0.45f);
}

static float KnobToDelaySamples(float knob) {
  const float ms = fclamp(KnobToDelayMs(knob), kTimeMinMs, kTimeMaxMs);
  return ms * (sample_rate / 1000.0f);
}

static inline void EqualPowerGains(float mix, float* dry_gain, float* wet_gain) {
  const float angle = fclamp(mix, 0.0f, 1.0f) * kHalfPi;
  *dry_gain = cosf(angle);
  *wet_gain = sinf(angle);
}

static inline bool IsBad(float x) { return !std::isfinite(x); }

// ============================================================
// Audio callback
// ============================================================
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();

  if (hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge()) {
    bypass = !bypass;
  }
  fs1_held = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();

  for (size_t i = 0; i < Hothouse::KNOB_LAST; ++i) {
    knob_values[i] = hw.GetKnobValue(static_cast<Hothouse::Knob>(i));
  }
  toggle_positions[0] = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1);
  toggle_positions[1] = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2);
  toggle_positions[2] = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3);

  // Block-rate parameter targets.
  s_mix.target = knob_values[Hothouse::KNOB_1];
  s_delay.target = KnobToDelaySamples(knob_values[Hothouse::KNOB_2]);
  s_feedback.target = fclamp(knob_values[Hothouse::KNOB_3], 0.0f, kMaxFeedback);
  s_filter.target = fclamp(knob_values[Hothouse::KNOB_5], 0.0f, 1.0f);
  s_space.target = fclamp(knob_values[Hothouse::KNOB_6], 0.0f, 1.0f);

  // SW1 FX: UP = clean only (gate 0); MIDDLE/DOWN = filtered layer (gate 1).
  // DOWN behaves as MIDDLE for now; SPACE/reverb is added here next milestone.
  s_fx_gate.target =
      (toggle_positions[0] == Hothouse::TOGGLESWITCH_UP) ? 0.0f : 1.0f;

  for (size_t i = 0; i < size; ++i) {
    const float dry = in[0][i];

    // Bypass: direct dry input only.
    if (bypass) {
      out[0][i] = dry;
      out[1][i] = dry;
      continue;
    }

    s_delay.Tick();
    s_mix.Tick();
    s_feedback.Tick();
    s_filter.Tick();
    s_fx_gate.Tick();
    s_space.Tick();

    // ----- Phantasmagoria-style custom DelBuf engine (clean core) -----
    // Write dry + feedback; gently saturate the write when feedback is high
    // so the loop compresses instead of running away.
    float del_in = dry + fb_sig;
    if (s_feedback.current > 0.3f) {
      del_in = fast_tanh(del_in * 0.5f) * 2.0f;
    }
    if (IsBad(del_in)) del_in = dry;
    main_delay.Write(del_in);

    const float read_pos =
        fclamp(s_delay.current, 1.0f, static_cast<float>(kMaxDelay) - 2.0f);
    float wet = main_delay.Read(read_pos);
    if (IsBad(wet)) wet = 0.0f;

    // Clean delay path (fixed tone) — this is the core signal and the ONLY
    // thing fed back, so the feedback loop is unaffected by the FX layer.
    const float wet_tone = tone_lpf.Process(wet);
    fb_sig = wet_tone * s_feedback.current;
    const float clean_layer = gentle_saturate(wet_tone);

    // ----- Parallel FILTER layer (SW1 + K5) -----
    // Band-pass (high-pass -> low-pass) copy of the same delay read, with makeup
    // gain, blended ALONGSIDE the clean delay for an obvious filtered-repeats
    // texture. Filter state advances every sample (even when disabled) so there
    // is no click when SW1 enables it. The clean delay always stays underneath.
    const float band = filter_lp.Process(filter_hp.Process(wet));
    float filtered_layer = gentle_saturate(band * kFilterMakeup);
    // Mild extra saturation so the louder filtered layer stays smooth, not harsh.
    filtered_layer = gentle_saturate(filtered_layer * 1.2f);
    // K5 curve so the filtered layer enters faster (25% already clearly filtered).
    const float gate = fclamp(s_fx_gate.current * s_filter.current, 0.0f, 1.0f);
    const float f = powf(gate, 0.65f);
    // Clean tapers down to kFilterCleanFloor; filtered layer is ADDED so K5 is
    // mostly a "how much filtered layer" control and dominates at max.
    const float clean_gain = 1.0f - (1.0f - kFilterCleanFloor) * f;
    const float delay_out = clean_layer * clean_gain + filtered_layer * f;

    // Constant-power dry/wet mix; pure dry at K1 minimum.
    float out_mono;
    if (s_mix.current <= 0.0005f) {
      out_mono = dry;
    } else {
      float dry_gain = 1.0f;
      float wet_gain = 0.0f;
      EqualPowerGains(s_mix.current, &dry_gain, &wet_gain);
      out_mono = dry * dry_gain + delay_out * wet_gain;
    }

    if (IsBad(out_mono)) out_mono = dry;
    out_mono = fclamp(out_mono, -1.5f, 1.5f);

    out[0][i] = out_mono;
    out[1][i] = out_mono;
  }
}

int main() {
  hw.Init();
  // Project rule: block size MUST be 1 (block size 48 caused wet-path whine).
  hw.SetAudioBlockSize(1);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  sample_rate = hw.AudioSampleRate();

  main_delay.Init(main_buf, kMaxDelay);
  tone_lpf.Init(kFeedbackToneHz, sample_rate);
  filter_hp.Init(kFilterHpHz, sample_rate);
  filter_lp.Init(kFilterLpHz, sample_rate);

  led_freeze.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_effect.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(10);

    // LEDs updated at ~100 Hz, not audio rate.
    led_freeze.Set(fs1_held ? 1.0f : 0.0f);
    led_effect.Set(bypass ? 0.0f : 1.0f);
    led_freeze.Update();
    led_effect.Update();

    // Call System::ResetToBootloader() if FOOTSWITCH_1 is pressed for 2 seconds
    hw.CheckResetToBootloader();
  }
  return 0;
}
