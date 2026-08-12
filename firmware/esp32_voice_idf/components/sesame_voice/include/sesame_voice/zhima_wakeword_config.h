#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::voice::zhima {

// Generated from the validated 19-recording 真人 wake-word dataset.
inline constexpr char kModelName[] = "nihao_zhima_real19_int8";
inline constexpr char kWakeWordText[] = "你好芝麻";
inline constexpr char kModelSha256[] = "6771a84c417ace896a3257321b21d9ab1306a7d03e264c2ffb1a06dccac2dd1a";
inline constexpr size_t kModelBytes = 22456;

inline constexpr uint32_t kSampleRateHz = 16000;
inline constexpr size_t kClipSamples = 16000;
inline constexpr size_t kFrameSize = 480;
inline constexpr size_t kFrameCount = 33;
inline constexpr size_t kFeatureBins = 32;
inline constexpr int kDftBins[kFeatureBins] = {6, 10, 13, 17, 21, 24, 28, 32, 35, 39, 43, 46, 50, 54, 57, 61, 65, 69, 72, 76, 80, 83, 87, 91, 94, 98, 102, 105, 109, 113, 116, 120};
inline constexpr float kFeatureMean = -17.9795761f;
inline constexpr float kFeatureStd = 2.57871799f;
inline constexpr float kInputScale = 0.0263375342f;
inline constexpr int kInputZeroPoint = -88;

inline constexpr float kWakeThreshold = 0.92f;
inline constexpr uint32_t kInferenceStrideMs = 200;
inline constexpr uint32_t kWakeCooldownMs = 2500;

}  // namespace sesame::voice::zhima
