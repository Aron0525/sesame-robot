#include <cassert>

#include "sesame_voice/playback_telemetry.h"

int main() {
  using namespace sesame::voice;

  PlaybackTelemetry telemetry;
  telemetry.begin_generation(7);

  PlaybackStats stats = telemetry.snapshot(0, false, false);
  assert(stats.generation_id == 7);
  assert(stats.buffered_packets == 0);
  assert(stats.max_buffered_packets == 0);
  assert(stats.low_watermark_packets == kPlaybackLowWaterFrames);
  assert(stats.high_watermark_packets == kPlaybackHighWaterFrames);
  assert(stats.underflow_count == 0);
  assert(stats.rendered_frames == 0);
  assert(!stats.playback_complete);
  assert(!stats.playback_started);
  assert(!stats.paused);

  telemetry.note_buffered(30);
  telemetry.note_buffered(12);
  telemetry.note_underflow();
  telemetry.note_dropped_packet();
  telemetry.note_stale_generation();
  telemetry.note_out_of_order();
  telemetry.note_sequence_discontinuity();
  telemetry.note_decode_duration(100);
  telemetry.note_decode_duration(200);
  telemetry.note_i2s_duration(300);
  telemetry.note_i2s_duration(500);
  telemetry.note_rendered();
  telemetry.note_rendered();

  stats = telemetry.snapshot(12, true, false);
  assert(stats.generation_id == 7);
  assert(stats.buffered_packets == 12);
  assert(stats.max_buffered_packets == 30);
  assert(stats.underflow_count == 1);
  assert(stats.dropped_packet_count == 1);
  assert(stats.stale_generation_count == 1);
  assert(stats.out_of_order_count == 1);
  assert(stats.sequence_discontinuity_count == 1);
  assert(stats.decode_last_us == 200);
  assert(stats.decode_max_us == 200);
  assert(stats.decode_avg_us == 150);
  assert(stats.i2s_last_us == 500);
  assert(stats.i2s_max_us == 500);
  assert(stats.i2s_avg_us == 400);
  assert(stats.rendered_frames == 2);
  assert(stats.playback_started);
  assert(!stats.paused);
  assert(telemetry.rendered_frames() == 2);

  telemetry.begin_generation(8);
  stats = telemetry.snapshot(0, false, true);
  assert(stats.generation_id == 8);
  assert(stats.max_buffered_packets == 0);
  assert(stats.underflow_count == 0);
  assert(stats.dropped_packet_count == 0);
  assert(stats.decode_avg_us == 0);
  assert(stats.rendered_frames == 0);
  assert(stats.paused);
  assert(telemetry.rendered_frames() == 0);
  assert(telemetry.healthy());
  return 0;
}
