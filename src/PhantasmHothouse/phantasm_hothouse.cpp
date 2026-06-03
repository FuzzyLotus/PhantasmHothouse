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

// ### Uncomment if IntelliSense can't resolve DaisySP-LGPL classes ###
// #include "daisysp-lgpl.h"

#include "daisysp.h"
#include "hothouse.h"

#define MAX_DELAY static_cast<size_t>(48000 * 1.0f)

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::Parameter;
using daisy::SaiHandle;
using daisysp::DelayLine;
using daisysp::fclamp;
using daisysp::fonepole;

Hothouse hw;
DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delay_line;

Parameter p_delay;

float current_delay = 2400.0f;
float delay_target = 2400.0f;
float feedback = 0.0f;
float mix = 0.0f;

// Hardware state retained for later DSP milestones.
Led led_freeze, led_effect;
bool bypass = true;
bool fs1_held = false;
float knob_values[Hothouse::KNOB_LAST] = {};
Hothouse::ToggleswitchPosition toggle_positions[3] = {
    Hothouse::TOGGLESWITCH_UNKNOWN, Hothouse::TOGGLESWITCH_UNKNOWN,
    Hothouse::TOGGLESWITCH_UNKNOWN};

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();

  // FS2 selects the dry bypass or engaged state.
  bypass ^= hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();
  fs1_held = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();

  for (size_t i = 0; i < Hothouse::KNOB_LAST; ++i) {
    knob_values[i] = hw.GetKnobValue(static_cast<Hothouse::Knob>(i));
  }
  toggle_positions[0] =
      hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1);
  toggle_positions[1] =
      hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2);
  toggle_positions[2] =
      hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3);

  led_freeze.Set(fs1_held ? 1.0f : 0.0f);
  led_effect.Set(bypass ? 0.0f : 1.0f);
  led_freeze.Update();
  led_effect.Update();

  // Block-rate controls (BasicMultiDelay style).
  delay_target = p_delay.Process();
  feedback = fclamp(knob_values[Hothouse::KNOB_3], 0.0f, 0.75f);
  mix = fclamp(knob_values[Hothouse::KNOB_1], 0.0f, 1.0f);

  for (size_t i = 0; i < size; ++i) {
    const float dry = in[0][i];

    if (bypass) {
      out[0][i] = dry;
      out[1][i] = dry;
      continue;
    }

    // K1 minimum: dry only (no wet contribution).
    if (mix <= 0.0f) {
      fonepole(current_delay, delay_target, 0.0002f);
      delay_line.SetDelay(current_delay);
      const float read = delay_line.Read();
      delay_line.Write(dry + feedback * read);
      out[0][i] = dry;
      out[1][i] = dry;
      continue;
    }

    fonepole(current_delay, delay_target, 0.0002f);
    delay_line.SetDelay(current_delay);

    const float read = delay_line.Read();
    delay_line.Write(dry + feedback * read);

    const float out_mono = dry * (1.0f - mix) + read * mix;
    out[0][i] = out_mono;
    out[1][i] = out_mono;
  }
}

int main() {
  hw.Init();
  hw.SetAudioBlockSize(1);  // Number of samples handled per callback
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  delay_line.Init();
  p_delay.Init(hw.knobs[Hothouse::KNOB_2], hw.AudioSampleRate() * 0.05f,
               static_cast<float>(MAX_DELAY), Parameter::LOGARITHMIC);

  led_freeze.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_effect.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(10);

    // Call System::ResetToBootloader() if FOOTSWITCH_1 is pressed for 2 seconds
    hw.CheckResetToBootloader();
  }
  return 0;
}
