#include <gufo/ext/generated/fish_drama3_constants.hpp>
#include <gufo/ext/json.hpp>
#include <gufo/ext/registry.hpp>
#include <gufo/ext/tts.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "hip/synthesis_runtime.hpp"

namespace gufo::models::fish_drama3 {

bool ProbeFishDrama3(std::string_view model_root);
bool ResolveFishDrama3Artifacts(std::string_view model_root,
                                std::string* error);

#if defined(ENGINE_ENABLE_HIP)
namespace {

constexpr std::uint32_t kModelSampleRate = 44100;

std::vector<float> ToMonoAndResample(const ext::TtsAudioBuffer& audio,
                                     std::uint32_t target_rate) {
  if (audio.samples.empty()) {
    return {};
  }
  const std::uint32_t channels = std::max<std::uint32_t>(1, audio.channels);
  std::vector<float> mono;
  if (channels == 1) {
    mono = audio.samples;
  } else {
    const std::size_t frames = audio.samples.size() / channels;
    mono.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
      float sum = 0.0F;
      for (std::uint32_t c = 0; c < channels; ++c) {
        sum += audio.samples[i * channels + c];
      }
      mono[i] = sum / static_cast<float>(channels);
    }
  }
  const std::uint32_t src_rate =
      audio.sample_rate == 0 ? target_rate : audio.sample_rate;
  if (src_rate == target_rate || mono.size() <= 1) {
    return mono;
  }
  const std::size_t out_len = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::llround(
             static_cast<double>(mono.size()) * target_rate / src_rate)));
  std::vector<float> resampled(out_len);
  const double ratio = static_cast<double>(src_rate) / target_rate;
  for (std::size_t i = 0; i < out_len; ++i) {
    const double src_pos = i * ratio;
    const std::size_t idx0 =
        std::min(static_cast<std::size_t>(src_pos), mono.size() - 1);
    const std::size_t idx1 = std::min(idx0 + 1, mono.size() - 1);
    const float frac = static_cast<float>(src_pos - static_cast<double>(idx0));
    resampled[i] = mono[idx0] * (1.0F - frac) + mono[idx1] * frac;
  }
  return resampled;
}

void AppendU16(std::string* out, std::uint16_t v) {
  const char bytes[2] = {static_cast<char>(v & 0xFFU),
                         static_cast<char>((v >> 8U) & 0xFFU)};
  out->append(bytes, 2);
}

void AppendU32(std::string* out, std::uint32_t v) {
  const char bytes[4] = {static_cast<char>(v & 0xFFU),
                         static_cast<char>((v >> 8U) & 0xFFU),
                         static_cast<char>((v >> 16U) & 0xFFU),
                         static_cast<char>((v >> 24U) & 0xFFU)};
  out->append(bytes, 4);
}

std::string EncodeWavPcm16(std::span<const float> samples,
                           std::uint32_t sample_rate) {
  const auto data_bytes = static_cast<std::uint32_t>(samples.size() * 2U);
  std::string out;
  out.reserve(44U + data_bytes);
  out.append("RIFF", 4);
  AppendU32(&out, 36U + data_bytes);
  out.append("WAVEfmt ", 8);
  AppendU32(&out, 16U);
  AppendU16(&out, 1U);
  AppendU16(&out, 1U);
  AppendU32(&out, sample_rate);
  AppendU32(&out, sample_rate * 2U);
  AppendU16(&out, 2U);
  AppendU16(&out, 16U);
  out.append("data", 4);
  AppendU32(&out, data_bytes);
  for (const float s : samples) {
    const float bounded = std::clamp(std::isfinite(s) ? s : 0.0F, -1.0F, 1.0F);
    const auto pcm = static_cast<std::int16_t>(bounded * 32767.0F);
    AppendU16(&out, static_cast<std::uint16_t>(pcm));
  }
  return out;
}

class FishDrama3TtsRunner final : public ext::TtsRunner {
public:
  explicit FishDrama3TtsRunner(
      std::unique_ptr<hip::SynthesisHipRuntime> runtime)
      : runtime_(std::move(runtime)) {}

  bool Synthesize(const ext::SynthesisRequest& request,
                  const ext::CancellationCheck& is_cancelled,
                  ext::SynthesisResult* result, std::string* error) override {
    fish_drama3::SynthesisRequest req;
    req.text = request.text;
    req.reference_audio_mono =
        ToMonoAndResample(request.reference_audio, kModelSampleRate);
    req.reference_sample_rate = kModelSampleRate;
    req.reference_text = request.reference_text;
    req.sampling.temperature =
        request.sampling.sample ? request.sampling.temperature : 0.0F;
    req.sampling.top_p = request.sampling.top_p;
    req.sampling.top_k = static_cast<int>(request.sampling.top_k);
    req.sampling.seed = request.sampling.seed;
    req.sampling.max_new_frames = request.max_new_tokens;
    req.on_audio = request.on_audio;

    fish_drama3::SynthesisResult gen_res;
    if (!runtime_->Generate(req, is_cancelled, &gen_res, error)) {
      return false;
    }
    if (result != nullptr) {
      result->samples = std::move(gen_res.samples);
      result->sample_count = gen_res.sample_count;
      result->sample_rate = gen_res.sample_rate;
      result->code_groups = gen_res.code_groups;
      result->codes.clear();
      result->codes.reserve(gen_res.codes.codes.size());
      for (const auto c : gen_res.codes.codes) {
        result->codes.push_back(static_cast<std::uint32_t>(c));
      }
    }
    return true;
  }

  std::vector<std::string> Voices() const override {
    return {"voice-clone", "default"};
  }

  ext::TtsVariant Variant() const override { return ext::TtsVariant::kBase; }

  std::string ModelId() const override { return "fish-drama-3"; }

private:
  std::unique_ptr<hip::SynthesisHipRuntime> runtime_;
};

std::unique_ptr<ext::TtsRunner> CreateFishDrama3TtsRunner(
    const std::string& model_root, std::size_t maximum_context_tokens,
    std::string* error) {
  auto runtime = hip::SynthesisHipRuntime::Create(
      model_root, maximum_context_tokens, error);
  if (!runtime) {
    return nullptr;
  }
  return std::make_unique<FishDrama3TtsRunner>(std::move(runtime));
}

int RunFishDrama3Speak(const ext::SpeakOptions& o) {
  if (o.text.empty()) {
    std::cerr << "Error: Fish Drama-3 speak requires non-empty --text\n";
    return 2;
  }
  std::string error;
  const auto load_begin = std::chrono::steady_clock::now();
  auto runtime =
      hip::SynthesisHipRuntime::Create(o.model, o.context, &error);
  if (!runtime) {
    std::cerr << "Error loading Fish Drama-3: " << error << "\n";
    return 1;
  }
  const double load_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - load_begin)
                             .count();

  fish_drama3::SynthesisRequest req;
  req.text = o.text;
  req.sampling.max_new_frames = o.max_frames;
  req.sampling.seed = o.seed;
  req.sampling.temperature = o.temperature;
  req.sampling.top_k = static_cast<int>(o.top_k);

  const auto gen_begin = std::chrono::steady_clock::now();
  fish_drama3::SynthesisResult res;
  if (!runtime->Generate(req, o.cancelled, &res, &error)) {
    std::cerr << "Error generating speech: " << error << "\n";
    return (o.cancelled && o.cancelled()) ? 130 : 1;
  }
  const double total_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - gen_begin)
                              .count();

  if (!o.output_path.empty()) {
    const std::string wav = EncodeWavPcm16(res.samples, res.sample_rate);
    std::ofstream out(o.output_path, std::ios::binary);
    out.write(wav.data(), static_cast<std::streamsize>(wav.size()));
    if (!out) {
      std::cerr << "Error: cannot write " << o.output_path << "\n";
      return 1;
    }
  }
  const double seconds =
      static_cast<double>(res.samples.size()) / res.sample_rate;
  const std::size_t frames = res.codes.frames;
  if (o.format == "json") {
    json::Value report = json::Value::object();
    report["schema"] = "gufo.fish-drama3.speech.v1";
    report["text"] = o.text;
    report["frames"] = static_cast<std::int64_t>(frames);
    report["audio_seconds"] = seconds;
    json::Value t = json::Value::object();
    t["load_ms"] = load_ms;
    t["total_ms"] = total_ms;
    t["real_time_factor"] =
        total_ms > 0.0 ? (seconds * 1000.0 / total_ms) : 0.0;
    report["timings"] = std::move(t);
    std::cout << report.dump() << "\n";
  } else if (!o.output_path.empty()) {
    std::cerr << "wrote " << o.output_path << ": " << seconds << " s, "
              << frames << " frames in " << total_ms << " ms\n";
  }
  return 0;
}

}  // namespace
#endif

}  // namespace gufo::models::fish_drama3

void fish_drama3_provider_register(gufo::ext::Registry& registry) {
  registry.AddProvider(
      gufo::ext::generated::extension_fish_drama3::kProvider_fish_drama3_tts);
#if defined(ENGINE_ENABLE_HIP)
  using namespace gufo::models::fish_drama3;
  registry.AddTtsProvider({
      .id = "fish-drama3-tts",
      .probe = &ProbeFishDrama3,
      .resolve = &ResolveFishDrama3Artifacts,
      .create = &CreateFishDrama3TtsRunner,
      .speak_driver = &RunFishDrama3Speak,
  });
#endif
}
