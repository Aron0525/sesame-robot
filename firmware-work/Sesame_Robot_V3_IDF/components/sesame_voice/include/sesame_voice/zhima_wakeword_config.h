#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::voice::zhima {

inline constexpr char kModelName[] = "zhima_wakeword_five_tts_int8";
inline constexpr char kModelSha256[] = "9c20563309845fa620296a278e94877d15de0e0c23fee9bcd156388b764f1949";
inline constexpr size_t kModelBytes = 22528;

inline constexpr uint32_t kSampleRateHz = 16000;
inline constexpr size_t kClipSamples = 16000;
inline constexpr size_t kFrameSize = 480;
inline constexpr size_t kFrameCount = 33;
inline constexpr size_t kFeatureBins = 32;
inline constexpr int kDftBins[kFeatureBins] = {6, 10, 13, 17, 21, 24, 28, 32, 35, 39, 43, 46, 50, 54, 57, 61, 65, 69, 72, 76, 80, 83, 87, 91, 94, 98, 102, 105, 109, 113, 116, 120};
inline constexpr float kFeatureMean = -17.6193867f;
inline constexpr float kFeatureStd = 2.62766223f;
inline constexpr float kInputScale = 0.0244624838f;
inline constexpr int kInputZeroPoint = -80;

inline constexpr float kWakeThreshold = 0.86f;
inline constexpr uint32_t kInferenceStrideMs = 200;
inline constexpr uint32_t kWakeCooldownMs = 2500;

}  // namespace sesame::voice::zhima
