#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "3dsmsc/audio/aac_decoder.hpp"
#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/audio/flac_decoder.hpp"
#include "3dsmsc/audio/mp3_decoder.hpp"
#include "3dsmsc/library/track.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using test::entry;
using test::FakeFileSystem;
using test::make_track;

void test_aac_decoder() {
  std::array<std::int16_t, 8192> output{};
  std::size_t written = 0;
  threedsmsc::PcmBlockInfo info;

  threedsmsc::Track raw_track;
  raw_track.path = "tests/fixtures/silence.aac";
  raw_track.format = threedsmsc::AudioFormat::Aac;
  threedsmsc::AacDecoder raw_decoder;
  assert(raw_decoder.open(raw_track));
  assert(raw_decoder.decode(output.data(), output.size(), written, info) ==
         threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);

  threedsmsc::Track m4a_track;
  m4a_track.path = "tests/fixtures/silence.m4a";
  m4a_track.format = threedsmsc::AudioFormat::M4a;
  threedsmsc::AacDecoder m4a_decoder;
  assert(m4a_decoder.open(m4a_track));
  assert(m4a_decoder.seek(1000));
  assert(m4a_decoder.decode(output.data(), output.size(), written, info) ==
         threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);
}

void test_flac_decoder() {
  threedsmsc::Track track;
  track.path = "tests/fixtures/silence.flac";
  track.format = threedsmsc::AudioFormat::Flac;
  threedsmsc::FlacDecoder decoder;
  assert(decoder.open(track));
  assert(decoder.seek(1000));
  std::array<std::int16_t, 2304> output{};
  std::size_t written = 0;
  threedsmsc::PcmBlockInfo info;
  const threedsmsc::DecodeResult result =
      decoder.decode(output.data(), output.size(), written, info);
  assert(result == threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);
}

threedsmsc::DecodeResult decode_to_end(threedsmsc::AudioDecoder& decoder, std::size_t& frames) {
  std::array<std::int16_t, 8192> output{};
  frames = 0;
  while (true) {
    std::size_t written = 0;
    threedsmsc::PcmBlockInfo info{};
    const threedsmsc::DecodeResult result =
        decoder.decode(output.data(), output.size(), written, info);
    if (result != threedsmsc::DecodeResult::FrameReady)
      return result;
    ++frames;
    assert(frames < 100000);
  }
}

void test_decoders_play_to_the_end() {
  threedsmsc::Track mp3;
  mp3.path = "tests/fixtures/silence.mp3";
  mp3.format = threedsmsc::AudioFormat::Mp3;
  threedsmsc::Mp3Decoder mp3_decoder;
  assert(mp3_decoder.open(mp3));
  std::size_t frames = 0;
  assert(decode_to_end(mp3_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 20);
  {
    // The 2.04 second fixture must decode to ~2 seconds of stereo PCM, not half of it.
    threedsmsc::Mp3Decoder timed;
    assert(timed.open(mp3));
    std::array<std::int16_t, 8192> pcm{};
    std::uint64_t pcm_frames = 0;
    std::size_t written = 0;
    threedsmsc::PcmBlockInfo info{};
    while (timed.decode(pcm.data(), pcm.size(), written, info) ==
           threedsmsc::DecodeResult::FrameReady) {
      pcm_frames += written / info.channels;
    }
    const std::uint64_t milliseconds = pcm_frames * 1000 / 44100;
    assert(milliseconds > 1900 && milliseconds < 2200);
  }

  // Real songs are far larger than the 32 KB input window; a long file must not overflow it.
  const std::filesystem::path long_path =
      std::filesystem::temp_directory_path() / "3dsmsc-long.mp3";
  {
    std::ifstream source("tests/fixtures/silence.mp3", std::ios::binary);
    std::stringstream content;
    content << source.rdbuf();
    std::ofstream output(long_path, std::ios::binary);
    for (int copy = 0; copy < 8; ++copy)
      output << content.str();
  }
  threedsmsc::Track long_mp3;
  long_mp3.path = long_path.string();
  long_mp3.format = threedsmsc::AudioFormat::Mp3;
  threedsmsc::Mp3Decoder long_decoder;
  assert(long_decoder.open(long_mp3));
  std::size_t long_frames = 0;
  const threedsmsc::DecodeResult long_result = decode_to_end(long_decoder, long_frames);
  assert(long_result == threedsmsc::DecodeResult::EndOfStream);
  assert(long_frames >= 8 * frames - 8);
  {
    // Seeking deep into a long file lands near the target and leaves the stream decodable.
    threedsmsc::Mp3Decoder seeker;
    assert(seeker.open(long_mp3));
    assert(seeker.seek(10000));
    std::array<std::int16_t, 8192> pcm{};
    std::size_t written = 0;
    threedsmsc::PcmBlockInfo info{};
    std::uint64_t remaining_frames = 0;
    while (seeker.decode(pcm.data(), pcm.size(), written, info) ==
           threedsmsc::DecodeResult::FrameReady) {
      remaining_frames += written / info.channels;
    }
    const std::uint64_t remaining_ms = remaining_frames * 1000 / 44100;
    assert(remaining_ms > 5500 && remaining_ms < 6500);  // 8 x 2.04s = 16.3s total
  }
  std::error_code cleanup_error;
  std::filesystem::remove(long_path, cleanup_error);

  threedsmsc::Track flac;
  flac.path = "tests/fixtures/silence.flac";
  flac.format = threedsmsc::AudioFormat::Flac;
  threedsmsc::FlacDecoder flac_decoder;
  assert(flac_decoder.open(flac));
  assert(decode_to_end(flac_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 1);

  threedsmsc::Track aac;
  aac.path = "tests/fixtures/silence.aac";
  aac.format = threedsmsc::AudioFormat::Aac;
  threedsmsc::AacDecoder aac_decoder;
  assert(aac_decoder.open(aac));
  assert(decode_to_end(aac_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 1);

  threedsmsc::Track m4a;
  m4a.path = "tests/fixtures/silence.m4a";
  m4a.format = threedsmsc::AudioFormat::M4a;
  threedsmsc::AacDecoder m4a_decoder;
  assert(m4a_decoder.open(m4a));
  // The player refuses to seek files whose decoder cannot (a raw .aac stream has no index).
  assert(m4a_decoder.can_seek());
  assert(!aac_decoder.can_seek());
  assert(mp3_decoder.can_seek());
  assert(decode_to_end(m4a_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 1);
}

void test_mp3_decoder() {
  threedsmsc::Track track;
  track.path = "tests/fixtures/silence.mp3";
  track.format = threedsmsc::AudioFormat::Mp3;
  threedsmsc::Mp3Decoder decoder;
  assert(decoder.open(track));
  assert(decoder.seek(1000));
  std::array<std::int16_t, 2304> output{};
  std::size_t written = 0;
  threedsmsc::PcmBlockInfo info;
  const threedsmsc::DecodeResult result =
      decoder.decode(output.data(), output.size(), written, info);
  assert(result == threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);

  while (decoder.decode(output.data(), output.size(), written, info) ==
         threedsmsc::DecodeResult::FrameReady) {
  }
}

double equalised_gain(double frequency, const std::int8_t* gains, std::uint32_t sample_rate) {
  constexpr std::size_t frames = 44100;
  constexpr double amplitude = 6000.0;
  const double pi = 3.14159265358979;
  std::vector<std::int16_t> samples(frames * 2);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto value = static_cast<std::int16_t>(
        amplitude * std::sin(2.0 * pi * frequency * static_cast<double>(frame) / sample_rate));
    samples[frame * 2] = value;
    samples[frame * 2 + 1] = value;
  }
  threedsmsc::Equalizer equalizer;
  equalizer.configure(gains, sample_rate);
  equalizer.process(samples.data(), frames);
  double sum = 0.0;
  std::size_t counted = 0;
  for (std::size_t frame = frames / 2; frame < frames; ++frame) {
    sum += static_cast<double>(samples[frame * 2]) * samples[frame * 2];
    ++counted;
  }
  const double output_rms = std::sqrt(sum / static_cast<double>(counted));
  return output_rms / (amplitude / std::sqrt(2.0));
}

void test_equalizer() {
  std::int8_t flat[threedsmsc::eq_band_count] = {};
  threedsmsc::Equalizer idle;
  idle.configure(flat, 44100);
  assert(!idle.active());
  std::int16_t untouched[8] = {100, -100, 2000, -2000, 30000, -30000, 7, -7};
  idle.process(untouched, 4);
  assert(untouched[2] == 2000 && untouched[5] == -30000);  // a flat EQ changes nothing

  // +12 dB at 1 kHz: the tone grows ~4x, minus the automatic headroom (half the boost = -6 dB).
  std::int8_t boost_1k[threedsmsc::eq_band_count] = {};
  boost_1k[5] = 12;
  const double boosted = equalised_gain(1000.0, boost_1k, 44100);
  assert(boosted > 1.8 && boosted < 2.2);  // 3.98 * 0.5

  // -12 dB at 1 kHz: no boost anywhere, so no headroom cut; the tone drops to ~25%.
  std::int8_t cut_1k[threedsmsc::eq_band_count] = {};
  cut_1k[5] = -12;
  const double cut = equalised_gain(1000.0, cut_1k, 44100);
  assert(cut > 0.22 && cut < 0.29);

  // Far from the edited band the tone is untouched apart from the headroom.
  std::int8_t boost_8k[threedsmsc::eq_band_count] = {};
  boost_8k[8] = 12;
  const double distant = equalised_gain(125.0, boost_8k, 44100);
  assert(distant > 0.47 && distant < 0.53);  // only the 0.5 headroom factor

  // 16 kHz is skipped at low sample rates, where it would sit above the audio band.
  std::int8_t boost_16k[threedsmsc::eq_band_count] = {};
  boost_16k[9] = 12;
  threedsmsc::Equalizer low_rate;
  low_rate.configure(boost_16k, 22050);
  assert(!low_rate.active());

  // Loud boosted audio saturates at full scale instead of wrapping around: a wrapped sample
  // would jump by ~65536 between neighbours.
  std::int8_t loud[threedsmsc::eq_band_count] = {};
  loud[5] = 12;
  threedsmsc::Equalizer saturating;
  saturating.configure(loud, 44100);
  std::vector<std::int16_t> tone(20000);
  for (std::size_t frame = 0; frame < tone.size() / 2; ++frame) {
    const auto value =
        static_cast<std::int16_t>(32000.0 * std::sin(2.0 * 3.14159265 * 1000.0 * frame / 44100.0));
    tone[frame * 2] = value;
    tone[frame * 2 + 1] = value;
  }
  saturating.process(tone.data(), tone.size() / 2);
  int peak = 0;
  int largest_jump = 0;
  for (std::size_t index = 2; index < tone.size(); index += 2) {
    peak = std::max(peak, std::abs(static_cast<int>(tone[index])));
    largest_jump = std::max(largest_jump, std::abs(tone[index] - tone[index - 2]));
  }
  assert(peak >= 32700);         // it really did hit full scale
  assert(largest_jump < 20000);  // and never wrapped

  // Presets and names.
  assert(std::string(threedsmsc::eq_preset_name(flat)) == "Flat");
  assert(std::string(threedsmsc::eq_preset_name(boost_1k)) == "Custom");
  std::size_t count = 0;
  const threedsmsc::EqPreset* presets = threedsmsc::eq_presets(count);
  assert(count >= 5);
  assert(threedsmsc::eq_next_preset(flat) == 1);
  assert(threedsmsc::eq_next_preset(presets[count - 1].gains) == 0);  // wraps around
  assert(threedsmsc::eq_next_preset(boost_1k) == 0);                  // custom -> first preset
}

}

void run_audio_tests() {
  test_aac_decoder();
  test_flac_decoder();
  test_decoders_play_to_the_end();
  test_mp3_decoder();
  test_equalizer();
}
