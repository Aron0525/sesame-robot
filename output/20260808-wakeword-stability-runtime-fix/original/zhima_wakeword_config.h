#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::voice::zhima {

inline constexpr char kModelName[] = "zhima_wakeword_tts_v2_int8";
inline constexpr char kModelSha256[] =
    "79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065";
inline constexpr size_t kModelBytes = 22456;

// Feature and quantization values exported with this exact TFLite model.
inline constexpr uint32_t kSampleRateHz = 16000;
inline constexpr size_t kClipSamples = 16000;
inline constexpr size_t kFrameSize = 480;
inline constexpr size_t kFrameCount = 33;
inline constexpr size_t kFeatureBins = 32;
inline constexpr int kDftBins[kFeatureBins] = {
    6,  10, 13, 17, 21, 24, 28, 32, 35, 39, 43, 46, 50, 54, 57, 61,
    65, 69, 72, 76, 80, 83, 87, 91, 94, 98, 102, 105, 109, 113, 116, 120,
};
inline constexpr float kFeatureMean = -17.3653469f;
inline constexpr float kFeatureStd = 2.71605949f;
inline constexpr float kInputScale = 0.0244557578f;
inline constexpr int kInputZeroPoint = -77;

// This is a direct raw-score threshold. It rejects the observed 0.855469
// non-target live take while retaining the two observed target takes.
inline constexpr float kWakeThreshold = 0.86f;
inline constexpr uint32_t kInferenceStrideMs = 200;
inline constexpr uint32_t kWakeCooldownMs = 2500;

}  // namespace sesame::voice::zhima
