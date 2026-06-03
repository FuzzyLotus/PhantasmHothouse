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
// Final intended control layout (locked target; see AGENTS.md)
// ============================================================
//   K1 MIX     Overall dry / clean delay mix.
//   K2 TIME    Delay time, pad-focused.
//   K3 SUSTAIN Feedback / buildup.
//   K4 HOLD    Held bed level.                  (not implemented yet)
//   K5 FILTER  Filtered repeats blend.          (currently plain tone LPF)
//   K6 SPACE   Reverb / reverse-reverb blend.   (not implemented yet)
//   SW1 FX     UP clean / MID filtered / DOWN filtered+space.
//   SW2 DIR    UP forward / MID hybrid / DOWN reverse.
//   SW3 HOLD   UP pure hold / MID live-over-hold / DOWN absorb-bleed.
//   FS1 HOLD   Hold/freeze performance control. (placeholder: LED1 only)
//   FS2 BYPASS Effect on/off.
//   LED1 HOLD  Hold/freeze status.
//   LED2 EFFECT Bypass/engaged status.
//
// DSP architecture: the clean delay is the core signal. Filter, space/reverb,
// and reverse layers are optional PARALLEL blends alongside the clean delay;
// they never permanently replace it. Only the clean delay + tone exist so far.

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
Lp1 tone_lpf;

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
Smoothed s_tone{9000.0f, 9000.0f, 0.05f};      // K5 tone (Hz), block-rate

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

// K5 tone: CCW dark (~1200 Hz) → CW bright (~9000 Hz).
static float KnobToToneHz(float knob) {
  const float t = fclamp(knob, 0.0f, 1.0f);
  return 1200.0f * powf(9000.0f / 1200.0f, t);
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
  s_tone.target = KnobToToneHz(knob_values[Hothouse::KNOB_5]);

  // Tone updated once per block (no per-sample expf).
  s_tone.Tick();
  tone_lpf.SetFreq(s_tone.current, sample_rate);

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

    // ----- Phantasmagoria-style custom DelBuf engine -----
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

    // K5 tone shapes the wet/feedback signal (dark -> bright).
    const float wet_tone = tone_lpf.Process(wet);
    fb_sig = wet_tone * s_feedback.current;

    // Constant-power dry/wet mix; pure dry at K1 minimum.
    float out_mono;
    if (s_mix.current <= 0.0005f) {
      out_mono = dry;
    } else {
      float dry_gain = 1.0f;
      float wet_gain = 0.0f;
      EqualPowerGains(s_mix.current, &dry_gain, &wet_gain);
      const float wet_out = gentle_saturate(wet_tone);
      out_mono = dry * dry_gain + wet_out * wet_gain;
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
  tone_lpf.Init(9000.0f, sample_rate);

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
