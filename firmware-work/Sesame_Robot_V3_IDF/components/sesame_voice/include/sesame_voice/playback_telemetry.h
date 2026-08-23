#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "sesame_voice/playback_policy.h"

namespace sesame::voice {

struct PlaybackStats {
  uint32_t generation_id{0};
  uint16_t buffered_packets{0};
  uint16_t buffered_ms{0};
  uint16_t startup_packets{static_cast<uint16_t>(kPlaybackStartupFrames)};
  uint16_t min_buffered_packets{0};
  uint16_t max_buffered_packets{0};
  uint16_t low_watermark_packets{
      static_cast<uint16_t>(kPlaybackLowWaterFrames)};
  uint16_t high_watermark_packets{
      static_cast<uint16_t>(kPlaybackHighWaterFrames)};
  uint32_t underflow_count{0};
  uint32_t dropped_packet_count{0};
  uint32_t stale_generation_count{0};
  uint32_t out_of_order_count{0};
  uint32_t sequence_discontinuity_count{0};
  uint32_t decode_last_us{0};
  uint32_t decode_max_us{0};
  uint32_t decode_avg_us{0};
  uint32_t i2s_last_us{0};
  uint32_t i2s_max_us{0};
  uint32_t i2s_avg_us{0};
  uint32_t rendered_frames{0};
  bool playback_started{false};
  bool paused{false};
  bool playback_complete{false};
};

// This state is shared by the WSS receive callback and the playback task.
// Atomics avoid having either real-time path wait for the voice control task.
class PlaybackTelemetry final {
 public:
  void begin_generation(uint32_t generation_id) {
    max_buffered_packets_.store(0, std::memory_order_relaxed);
    min_buffered_packets_.store(UINT16_MAX, std::memory_order_relaxed);
    underflow_count_.store(0, std::memory_order_relaxed);
    dropped_packet_count_.store(0, std::memory_order_relaxed);
    stale_generation_count_.store(0, std::memory_order_relaxed);
    out_of_order_count_.store(0, std::memory_order_relaxed);
    sequence_discontinuity_count_.store(0, std::memory_order_relaxed);
    decode_last_us_.store(0, std::memory_order_relaxed);
    decode_max_us_.store(0, std::memory_order_relaxed);
    decode_total_us_.store(0, std::memory_order_relaxed);
    decode_count_.store(0, std::memory_order_relaxed);
    i2s_last_us_.store(0, std::memory_order_relaxed);
    i2s_max_us_.store(0, std::memory_order_relaxed);
    i2s_total_us_.store(0, std::memory_order_relaxed);
    i2s_count_.store(0, std::memory_order_relaxed);
    rendered_frames_.store(0, std::memory_order_relaxed);
    generation_id_.store(generation_id, std::memory_order_release);
  }

  void note_buffered(size_t buffered_packets) {
    const uint16_t buffered = clamp_packet_count(buffered_packets);
    uint16_t minimum = min_buffered_packets_.load(std::memory_order_relaxed);
    while (minimum > buffered &&
           !min_buffered_packets_.compare_exchange_weak(
               minimum, buffered, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    uint16_t maximum = max_buffered_packets_.load(std::memory_order_relaxed);
    while (maximum < buffered &&
           !max_buffered_packets_.compare_exchange_weak(
               maximum, buffered, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
  }

  void note_underflow() {
    underflow_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void note_dropped_packet() {
    dropped_packet_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void note_stale_generation() {
    stale_generation_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void note_out_of_order() {
    out_of_order_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void note_sequence_discontinuity() {
    sequence_discontinuity_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void note_decode_duration(uint32_t duration_us) {
    note_duration(duration_us, decode_last_us_, decode_max_us_,
                  decode_total_us_, decode_count_);
  }

  void note_i2s_duration(uint32_t duration_us) {
    note_duration(duration_us, i2s_last_us_, i2s_max_us_, i2s_total_us_,
                  i2s_count_);
  }

  uint32_t note_rendered() {
    return rendered_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  uint32_t rendered_frames() const {
    return rendered_frames_.load(std::memory_order_relaxed);
  }

  bool healthy() const {
    return underflow_count_.load(std::memory_order_relaxed) == 0 &&
           dropped_packet_count_.load(std::memory_order_relaxed) == 0 &&
           stale_generation_count_.load(std::memory_order_relaxed) == 0 &&
           out_of_order_count_.load(std::memory_order_relaxed) == 0 &&
           sequence_discontinuity_count_.load(std::memory_order_relaxed) == 0;
  }

  PlaybackStats snapshot(size_t buffered_packets, bool playback_started,
                         bool paused, bool playback_complete = false) const {
    const uint16_t buffered = clamp_packet_count(buffered_packets);
    const uint16_t observed_minimum =
        min_buffered_packets_.load(std::memory_order_relaxed);
    return {
        .generation_id = generation_id_.load(std::memory_order_acquire),
        .buffered_packets = buffered,
        .buffered_ms = static_cast<uint16_t>(buffered * 20U),
        .startup_packets = static_cast<uint16_t>(kPlaybackStartupFrames),
        .min_buffered_packets =
            observed_minimum == UINT16_MAX ? buffered : observed_minimum,
        .max_buffered_packets =
            max_buffered_packets_.load(std::memory_order_relaxed),
        .low_watermark_packets =
            static_cast<uint16_t>(kPlaybackLowWaterFrames),
        .high_watermark_packets =
            static_cast<uint16_t>(kPlaybackHighWaterFrames),
        .underflow_count = underflow_count_.load(std::memory_order_relaxed),
        .dropped_packet_count =
            dropped_packet_count_.load(std::memory_order_relaxed),
        .stale_generation_count =
            stale_generation_count_.load(std::memory_order_relaxed),
        .out_of_order_count =
            out_of_order_count_.load(std::memory_order_relaxed),
        .sequence_discontinuity_count =
            sequence_discontinuity_count_.load(std::memory_order_relaxed),
        .decode_last_us = decode_last_us_.load(std::memory_order_relaxed),
        .decode_max_us = decode_max_us_.load(std::memory_order_relaxed),
        .decode_avg_us = average(decode_total_us_, decode_count_),
        .i2s_last_us = i2s_last_us_.load(std::memory_order_relaxed),
        .i2s_max_us = i2s_max_us_.load(std::memory_order_relaxed),
        .i2s_avg_us = average(i2s_total_us_, i2s_count_),
        .rendered_frames = rendered_frames_.load(std::memory_order_relaxed),
        .playback_started = playback_started,
        .paused = paused,
        .playback_complete = playback_complete,
    };
  }

 private:
  static void note_duration(uint32_t duration_us,
                            std::atomic<uint32_t>& last,
                            std::atomic<uint32_t>& maximum,
                            std::atomic<uint64_t>& total,
                            std::atomic<uint32_t>& count) {
    last.store(duration_us, std::memory_order_relaxed);
    uint32_t observed_maximum = maximum.load(std::memory_order_relaxed);
    while (observed_maximum < duration_us &&
           !maximum.compare_exchange_weak(
               observed_maximum, duration_us, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    total.fetch_add(duration_us, std::memory_order_relaxed);
    count.fetch_add(1, std::memory_order_relaxed);
  }

  static uint32_t average(const std::atomic<uint64_t>& total,
                          const std::atomic<uint32_t>& count) {
    const uint32_t samples = count.load(std::memory_order_relaxed);
    if (samples == 0) return 0;
    const uint64_t value = total.load(std::memory_order_relaxed) / samples;
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
  }

  static constexpr uint16_t clamp_packet_count(size_t value) {
    return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
  }

  std::atomic<uint32_t> generation_id_{0};
  std::atomic<uint16_t> max_buffered_packets_{0};
  std::atomic<uint16_t> min_buffered_packets_{UINT16_MAX};
  std::atomic<uint32_t> underflow_count_{0};
  std::atomic<uint32_t> dropped_packet_count_{0};
  std::atomic<uint32_t> stale_generation_count_{0};
  std::atomic<uint32_t> out_of_order_count_{0};
  std::atomic<uint32_t> sequence_discontinuity_count_{0};
  std::atomic<uint32_t> decode_last_us_{0};
  std::atomic<uint32_t> decode_max_us_{0};
  std::atomic<uint64_t> decode_total_us_{0};
  std::atomic<uint32_t> decode_count_{0};
  std::atomic<uint32_t> i2s_last_us_{0};
  std::atomic<uint32_t> i2s_max_us_{0};
  std::atomic<uint64_t> i2s_total_us_{0};
  std::atomic<uint32_t> i2s_count_{0};
  std::atomic<uint32_t> rendered_frames_{0};
};

}  // namespace sesame::voice
