#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::voice::zhima {

inline constexpr char kModelName[] = "zhima_wakeword_dashscope10_int8";
inline constexpr char kWakeWordText[] = "你好芝麻";
inline constexpr char kModelSha256[] = "2247f91d43b4d70fe17c90b1e999130c2b51476e044a86156b4ea250ba6ae305";
inline constexpr size_t kModelBytes = 22456;
inline constexpr uint32_t kSampleRateHz = 16000;
inline constexpr size_t kClipSamples = 16000;
inline constexpr size_t kFrameSize = 480;
inline constexpr size_t kFrameCount = 33;
inline constexpr size_t kFeatureBins = 32;
inline constexpr int kDftBins[kFeatureBins] = {6, 10, 13, 17, 21, 24, 28, 32, 35, 39, 43, 46, 50, 54, 57, 61, 65, 69, 72, 76, 80, 83, 87, 91, 94, 98, 102, 105, 109, 113, 116, 120};
inline constexpr float kFeatureMean = -17.3126373f;
inline constexpr float kFeatureStd = 2.72947984f;
inline constexpr float kInputScale = 0.0243355129f;
inline constexpr int kInputZeroPoint = -77;
inline constexpr float kWakeThreshold = 0.85f;
inline constexpr uint32_t kInferenceStrideMs = 200;
inline constexpr uint32_t kWakeCooldownMs = 2500;

}  // namespace sesame::voice::zhima
