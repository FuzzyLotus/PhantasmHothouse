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
//   K6 SPACE   Reverb / space blend.            (v0.4: parallel reverb layer, SW1 DOWN)
// Switch row (mode / world selectors):
//   SW1 FX     UP clean / MID filtered / DOWN filtered+space.
//   SW2 DIR    UP forward / MID hybrid / DOWN reverse.  (v0.5: dual-grain reverse)
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
using daisysp::fclamp;
using daisysp::fonepole;

// ============================================================
// Constants
// ============================================================
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr float kHalfPi = 1.57079632679489661923f;

// Main delay buffer: 216000 samples == 4.5 s at 48 kHz. Larger than the audio
// delay range so the reverse reader has room for a long perceived reverse event
// (at normal reverse speed a perceived N-second event needs a ~2N-second sweep).
static constexpr size_t kMaxDelay = 216000;

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

// Parallel SPACE layer (K6, enabled only when SW1 == DOWN): a lightweight
// multi-tap echo-chamber reverb fed from the delay layer. Added in parallel so
// the clean + filtered delay always remain audible underneath.
static constexpr size_t kRevSize = 48000;        // 1.0 s at 48 kHz
static constexpr int kRevTapA = 3984;            // Phantasmagoria reverb taps
static constexpr int kRevTapB = 7248;
static constexpr int kRevTapC = 10896;
static constexpr int kRevTapD = 14928;
static constexpr float kRevFeedback = 0.6f;      // conservative decay for first pass
static constexpr float kRevLpHz = 6000.0f;       // tame highs (pad-like, not harsh)
static constexpr float kRevHpHz = 120.0f;        // remove rumble from the smear
static constexpr float kSpaceLevel = 1.0f;       // makeup for the added space layer

// Parallel REVERSE layer (SW2 DIR): the Phantasmagoria dual-grain reverse
// reader sweeps the rolling main delay buffer. SW2 selects the wet direction
// voice (forward / hybrid / reverse), which also feeds the filter, space, and
// feedback source so the reverse bed stays glued to the delay.
static constexpr float kReverseOffsetMs = 1.0f;    // tiny minimum read distance
// K2 controls the PERCEIVED reverse event length. At normal reverse speed the
// read distance must grow ~2 samples/sample, so a perceived event of E ms needs
// a sweep of ~2*E ms of buffer history:
//   sweepSamples = eventMs * kReverseSpeed * sr/1000
//   freq         = kReverseSpeed * sr / sweepSamples   (== 1000 / eventMs)
// This keeps correct reverse pitch/speed while K2 sets how long the reverse bed
// feels (not just a fast 1 s grain).
static constexpr float kReverseSpeed = 2.0f;          // read-distance growth (samp/samp)
static constexpr float kReverseEventMinMs = 650.0f;   // shortest perceived reverse
static constexpr float kReverseEventMaxMs = 1900.0f;  // longest perceived reverse
// Reverse tone + a second light smoothing pole soften hard-attack grain
// transients without over-darkening; the reverse stays clear and present.
static constexpr float kReverseToneHz = 5500.0f;    // main reverse low-pass (conservative)
static constexpr float kReverseSmoothHz = 6000.0f;  // extra light transient softener
static constexpr float kReverseMakeup = 1.2f;       // keep reverse present, not smeared
// SW2 DIR selects the wet delay VOICE (forward / hybrid / reverse). The same
// amounts drive the direction voice, the filter source, the space source, and
// the feedback source, so reverse stays glued to the delay bed.
static constexpr float kFwdWetUp = 1.0f;     // UP: forward only
static constexpr float kRevWetUp = 0.0f;
static constexpr float kFwdWetMid = 0.75f;   // MIDDLE: forward clearly present...
static constexpr float kRevWetMid = 0.45f;   //         ...with reverse blooming around it
static constexpr float kFwdWetDown = 0.0f;   // DOWN: reverse only in the wet voice
static constexpr float kRevWetDown = 1.0f;   //       reverse only

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
// Phantasmagoria dual-grain reverse reader
// ============================================================
// Dual-overlap Hann-window granular reverse reader over the rolling
// interpolated main delay buffer. A grain phase ramps 0 -> 1; the read distance
// behind the write pointer GROWS with phase, so playback walks from newer audio
// toward older audio (the reverse feel). Two grains 180 degrees apart (phase and
// phase + 0.5) are Hann-windowed (raised cosine) and normalized by their window
// sum, which keeps the reverse texture smooth and continuous (organic, not hard
// block reversal). Hann windows sum to a constant at the 180-degree offset.
struct ReverseGrainReader {
  float phase = 0.0f;
  float offset_ms = kReverseOffsetMs; // minimum read distance

  void Init() {
    phase = 0.0f;
    offset_ms = kReverseOffsetMs;
  }

  inline float ReadDistance(float ph, float sr, float sweep_samps,
                            float mod_samps) const {
    return ph * sweep_samps + offset_ms * (sr / 1000.0f) + mod_samps;
  }

  // Advances one sample and returns the normalized dual-grain reverse sample.
  // sweep_samps and freq_hz are supplied per sample: K2 sets the window length
  // while freq_hz = kReverseSpeed / sweepSeconds keeps the read distance growing
  // ~2 samples/sample, so reverse playback speed/pitch stays correct. The grain
  // phase runs continuously and is never reset on K2 moves.
  inline float Process(const DelBuf& src, float sr, float sweep_samps,
                       float freq_hz, float mod_samps) {
    phase += freq_hz / sr;
    if (phase >= 1.0f) phase -= 1.0f;

    const float pa = phase;
    float pb = phase + 0.5f;
    if (pb >= 1.0f) pb -= 1.0f;

    // Raised-cosine (Hann) windows: smoother grain fade-in/out than triangular,
    // so hard attacks glue together instead of sounding choppy. With the second
    // grain at phase + 0.5 the two Hann windows sum to a constant (~1.0), giving
    // a continuous, even crossfade.
    const float win_a = 0.5f - 0.5f * cosf(kTwoPi * pa);
    const float win_b = 0.5f - 0.5f * cosf(kTwoPi * pb);

    const float ga = src.Read(ReadDistance(pa, sr, sweep_samps, mod_samps));
    const float gb = src.Read(ReadDistance(pb, sr, sweep_samps, mod_samps));

    const float denom = fmaxf(win_a + win_b, 0.001f);
    return (ga * win_a + gb * win_b) / denom;
  }
};

// ============================================================
// SDRAM storage and DSP objects
// ============================================================
static float DSY_SDRAM_BSS main_buf[kMaxDelay];
static float DSY_SDRAM_BSS rev_buf[kRevSize];

Hothouse hw;
DelBuf main_delay;
Lp1 tone_lpf;     // fixed tone on the clean delay / feedback path
Hp1 filter_hp;    // parallel FILTER-layer band-pass: high-pass stage (K5)
Lp1 filter_lp;    // parallel FILTER-layer band-pass: low-pass stage (K5)
DelBuf rev_delay; // parallel SPACE-layer echo-chamber buffer (K6 / SW1 DOWN)
Lp1 rev_lp;       // SPACE-layer input low-pass (pad-like)
Hp1 rev_hp;       // SPACE-layer output high-pass (remove rumble)
ReverseGrainReader reverse_reader;  // parallel REVERSE layer source (SW2 DIR)
Lp1 reverse_lp;                     // gentle pad-friendly tone for reverse
Lp1 reverse_smooth;                 // extra light pole softening reverse transients

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
float rev_fb = 0.0f;  // SPACE-layer reverb feedback (separate from clean loop)

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
Smoothed s_space{0.0f, 0.0f, 0.0010f};         // K6 SPACE blend (0..1)
Smoothed s_space_gate{0.0f, 0.0f, 0.0015f};    // SW1 DOWN enable (click-free)
Smoothed s_fwd_wet{1.0f, 1.0f, 0.0015f};       // SW2 DIR forward wet amount (click-free)
Smoothed s_rev_wet{0.0f, 0.0f, 0.0015f};       // SW2 DIR reverse wet amount (click-free)

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
  s_fx_gate.target =
      (toggle_positions[0] == Hothouse::TOGGLESWITCH_UP) ? 0.0f : 1.0f;
  // SW1 DOWN additionally enables the parallel SPACE layer (K6).
  s_space_gate.target =
      (toggle_positions[0] == Hothouse::TOGGLESWITCH_DOWN) ? 1.0f : 0.0f;

  // SW2 DIR: separate forward/reverse wet amounts for the delay voice.
  //   UP     = forward only.
  //   MIDDLE = hybrid: forward clearly audible with reverse around it.
  //   DOWN   = reverse-dominant: forward greatly reduced, reverse takes over.
  // K1 dry and FS2 bypass are unaffected — only the WET delay voice changes.
  float fwd_wet_target = kFwdWetUp;
  float rev_wet_target = kRevWetUp;
  if (toggle_positions[1] == Hothouse::TOGGLESWITCH_MIDDLE) {
    fwd_wet_target = kFwdWetMid;
    rev_wet_target = kRevWetMid;
  } else if (toggle_positions[1] == Hothouse::TOGGLESWITCH_DOWN) {
    fwd_wet_target = kFwdWetDown;
    rev_wet_target = kRevWetDown;
  }
  s_fwd_wet.target = fwd_wet_target;
  s_rev_wet.target = rev_wet_target;

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
    s_space_gate.Tick();
    s_fwd_wet.Tick();
    s_rev_wet.Tick();

    // ----- Phantasmagoria-style custom DelBuf engine (clean core) -----
    // Write dry + feedback; gently saturate the write when feedback is high so
    // the loop compresses instead of running away. Feedback stays based on the
    // clean forward path only; feeding reverse back into this same rolling buffer
    // gets reversed again on later passes and turns forward-ish.
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

    // Forward voice: fixed-tone, gently saturated forward read.
    const float wet_tone = tone_lpf.Process(wet);
    const float forward_voice = gentle_saturate(wet_tone);

    // ----- Reverse voice (Phantasmagoria dual-grain reader) -----
    // Reads the SAME rolling main delay buffer (Phantasmagoria dual-grain). K2
    // sets the PERCEIVED reverse event length via the already-smoothed
    // s_delay.current: the sweep spans ~2x that length of buffer history, and
    // the grain rate is derived so the read distance always grows ~2
    // samples/sample — reverse pitch/speed stays correct (no octaver) while K2
    // changes how long the reverse bed feels. Sweep is clamped within the
    // buffer; phase is never reset on K2 moves; s_delay is smoothed (zipper-free).
    const float delay_ms = s_delay.current / (sample_rate / 1000.0f);
    const float reverse_event_ms =
        fclamp(delay_ms, kReverseEventMinMs, kReverseEventMaxMs);
    float rev_sweep_samps =
        reverse_event_ms * kReverseSpeed * (sample_rate / 1000.0f);
    rev_sweep_samps =
        fclamp(rev_sweep_samps, 1.0f, static_cast<float>(kMaxDelay) - 4.0f);
    const float rev_freq_hz = kReverseSpeed * sample_rate / rev_sweep_samps;
    float reverse_grain = reverse_reader.Process(main_delay, sample_rate,
                                                 rev_sweep_samps, rev_freq_hz, 0.0f);
    if (IsBad(reverse_grain)) reverse_grain = 0.0f;
    const float reverse_soft =
        reverse_smooth.Process(reverse_lp.Process(reverse_grain));
    float reverse_voice = gentle_saturate(reverse_soft * kReverseMakeup);
    if (IsBad(reverse_voice)) reverse_voice = 0.0f;

    // ----- SW2 DIRECTION voice: the main wet delay voice -----
    // SW2 selects the wet delay WORLD, not just an added layer:
    //   UP     = forward only          (fwd 1.0  / rev 0.0)
    //   MIDDLE = hybrid forward+reverse (fwd 0.75 / rev 0.45)
    //   DOWN   = reverse only           (fwd 0.0  / rev 1.0)
    // The filter and space derive from this voice; the main buffer feedback stays
    // clean-forward so reverse repeats do not become reverse-of-reverse.
    const float direction_voice =
        forward_voice * s_fwd_wet.current + reverse_voice * s_rev_wet.current;

    // Main delay feedback remains clean-forward in all SW2 modes. The audible
    // wet voice can be reverse-only, but the rolling buffer should not contain
    // reverse-fed repeats that get reversed again on the next pass.
    fb_sig = wet_tone * s_feedback.current;
    if (IsBad(fb_sig)) fb_sig = 0.0f;

    // ----- Parallel FILTER layer (SW1 + K5), fed from the DIRECTION voice -----
    // Band-pass (high-pass -> low-pass) of the SELECTED direction voice, with
    // makeup gain, blended alongside it. So the filter follows SW2: forward
    // repeats in UP, hybrid in MIDDLE, reverse repeats in DOWN. Filter state
    // advances every sample so SW1 enabling is click-free.
    const float band = filter_lp.Process(filter_hp.Process(direction_voice));
    float filtered_layer = gentle_saturate(band * kFilterMakeup);
    // Mild extra saturation so the louder filtered layer stays smooth, not harsh.
    filtered_layer = gentle_saturate(filtered_layer * 1.2f);
    // K5 curve so the filtered layer enters faster (25% already clearly filtered).
    const float gate = fclamp(s_fx_gate.current * s_filter.current, 0.0f, 1.0f);
    const float f = powf(gate, 0.65f);
    // Direction voice tapers to kFilterCleanFloor; filtered layer is ADDED so K5
    // is mostly a "how much filtered layer" control and dominates at max.
    const float clean_gain = 1.0f - (1.0f - kFilterCleanFloor) * f;
    const float delay_out = direction_voice * clean_gain + filtered_layer * f;

    // ----- Parallel SPACE layer (SW1 DOWN + K6), fed from delay_out -----
    // Fed from the direction + filter voice, so SW2 DOWN + SW1 DOWN is genuine
    // reverse-space (not forward-space with reverse pasted on top). Runs every
    // sample (continuous state so SW1 DOWN is click-free) but is only ADDED to
    // the output when SW1 == DOWN, scaled by K6. Its own feedback (rev_fb) is
    // separate from the main delay loop.
    float rev_in = rev_lp.Process(delay_out + rev_fb * kRevFeedback);
    if (IsBad(rev_in)) rev_in = 0.0f;
    rev_delay.Write(rev_in);
    const float rv = (rev_delay.Read(static_cast<float>(kRevTapA)) +
                      rev_delay.Read(static_cast<float>(kRevTapB)) +
                      rev_delay.Read(static_cast<float>(kRevTapC)) +
                      rev_delay.Read(static_cast<float>(kRevTapD))) *
                     0.25f;
    rev_fb = rv;
    float space_layer = gentle_saturate(rev_hp.Process(rv));
    if (IsBad(space_layer)) space_layer = 0.0f;
    // K6 amount, gated by SW1 DOWN. At 0% the space layer adds nothing.
    const float sp = fclamp(s_space_gate.current * s_space.current, 0.0f, 1.0f);
    const float delay_plus_space = delay_out + space_layer * (kSpaceLevel * sp);

    // Constant-power dry/wet mix; pure dry at K1 minimum.
    float out_mono;
    if (s_mix.current <= 0.0005f) {
      out_mono = dry;
    } else {
      float dry_gain = 1.0f;
      float wet_gain = 0.0f;
      EqualPowerGains(s_mix.current, &dry_gain, &wet_gain);
      out_mono = dry * dry_gain + delay_plus_space * wet_gain;
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
  rev_delay.Init(rev_buf, kRevSize);
  rev_lp.Init(kRevLpHz, sample_rate);
  rev_hp.Init(kRevHpHz, sample_rate);
  reverse_reader.Init();
  reverse_lp.Init(kReverseToneHz, sample_rate);
  reverse_smooth.Init(kReverseSmoothHz, sample_rate);

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
