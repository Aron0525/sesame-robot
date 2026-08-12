#pragma once

#include <cstdint>

namespace sesame::voice::zhima {

// XiaoZhi-style custom wake words are ESP-SR MultiNet command phrases. They
// are configured as pinyin and run against the mn7_cn model in the ESP-SR
// `model` partition; this is intentionally not a custom TFLite/WakeNet model.
inline constexpr char kWakeWordText[] = "你好，芝麻";
inline constexpr char kWakeWordPinyin[] = "ni hao zhi ma";
inline constexpr char kMultinetModelName[] = "mn7_cn";
inline constexpr float kWakeThreshold = 0.20f;
inline constexpr int kDetectionDurationMs = 3000;

}  // namespace sesame::voice::zhima
