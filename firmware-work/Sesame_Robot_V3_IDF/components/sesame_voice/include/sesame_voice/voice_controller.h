#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sesame_audio/audio_hal.h"
#include "sesame_audio/opus_codec.h"
#include "sesame_protocol/control_event.h"
#include "sesame_protocol/accepted_sequence.h"
#include "sesame_protocol/turn_state.h"
#include "sesame_robot/robot_adapter.h"
#include "sesame_transport/device_config.h"
#include "sesame_transport/gateway_client.h"
#include "sesame_voice/voice_turn_detector.h"
#include "sesame_voice/wake_vad_engine.h"
#include "sesame_voice/recording_button.h"
#include "sesame_voice/playback_policy.h"
#include "sesame_voice/playback_control.h"
#include "sesame_voice/playback_telemetry.h"
#include "sesame_voice/playback_turn_binding.h"

namespace sesame::voice {

class VoiceController final : public sesame::transport::GatewayObserver {
 public:
  VoiceController(sesame::audio::AudioHal* audio,
                  sesame::robot::RobotAdapter* robot);
  ~VoiceController();

  VoiceController(const VoiceController&) = delete;
  VoiceController& operator=(const VoiceController&) = delete;

  esp_err_t start();
  void stop();

  void on_gateway_connected() override;
  void on_gateway_disconnected() override;
  void on_gateway_text(const char* data, size_t size) override;
  void on_gateway_binary(const uint8_t* data, size_t size) override;

 private:
  static constexpr UBaseType_t kOutboundQueueDepth = 6;
  static constexpr uint32_t kOutboundTaskStackBytes = 24576;

  struct DownlinkPacket {
    uint32_t generation_id;
    uint32_t sequence;
    uint16_t size;
    std::array<uint8_t, sesame::audio::OpusCodec::kMaxPacketBytes> data;
  };

  enum class OutboundFrameKind : uint8_t {
    kText,
    kBinary,
  };

  struct OutboundFrame {
    OutboundFrameKind kind{OutboundFrameKind::kText};
    uint16_t size{0};
    uint32_t connection_epoch{0};
    std::array<uint8_t, 1024> data{};
  };

  static void task_entry(void* context);
  static void playback_task_entry(void* context);
  static void outbound_task_entry(void* context);
  void run();
  void run_playback();
  void run_outbound();
  void handle_button(ButtonEvent event, uint64_t now_ms);
  void capture_and_process(uint64_t now_ms);
  void encode_and_send(const int16_t* pcm, size_t samples, uint64_t now_ms);
  void process_wake_vad_signals(uint64_t now_ms);
  void handle_turn_event(VoiceTurnEvent event, uint64_t now_ms);
  void begin_listening(uint64_t now_ms, CaptureTrigger capture_trigger);
  void finish_listening();
  void cancel_listening(const char* reason);
  esp_err_t send_control(sesame::protocol::ControlEventType type,
                         const char* payload_json,
                         const char* request_id = nullptr,
                         bool include_active_turn = true,
                         const char* turn_id_override = nullptr);
  esp_err_t send_control_locked(sesame::protocol::ControlEventType type,
                                const char* payload_json,
                                const char* request_id,
                                bool include_active_turn,
                                const char* turn_id_override);
  esp_err_t send_session_hello_locked();
  esp_err_t enqueue_outbound_text(const char* data, size_t size);
  esp_err_t enqueue_outbound_binary(const uint8_t* data, size_t size);
  esp_err_t enqueue_outbound(OutboundFrameKind kind, const void* data,
                             size_t size, TickType_t wait_ticks);
  void discard_outbound_frames();
  void process_control_json(const char* data, size_t size);
  void begin_tts(uint32_t generation_id, const char* incoming_turn_id,
                 bool is_gateway_local_test);
  void finish_tts(uint32_t generation_id);
  void complete_tts_if_drained();
  void flush_tts(bool reset_turn_state = true);
  void queue_playback_stats(bool playback_started,
                            bool playback_complete = false);
  void send_pending_playback_stats();
  void send_operator_result(bool accepted, const char* request_id);

  sesame::audio::AudioHal* audio_;
  sesame::robot::RobotAdapter* robot_;
  sesame::audio::OpusCodec codec_;
  sesame::transport::StoredDeviceConfig config_{};
  sesame::transport::GatewayClient gateway_;
  sesame::protocol::TurnStateMachine turn_state_;
  WakeVadEngine wake_vad_;
  VoiceTurnDetector turn_detector_{{
      .wake_to_speech_timeout_ms = 3000,
      .endpoint_silence_ms = 800,
      .maximum_listen_ms = 10000,
  }};
  RecordingButton button_{40, 30000};
  QueueHandle_t downlink_queue_{nullptr};
  QueueHandle_t playback_stats_queue_{nullptr};
  QueueHandle_t outbound_queue_{nullptr};
  SemaphoreHandle_t control_send_mutex_{nullptr};
  TaskHandle_t task_{nullptr};
  TaskHandle_t playback_task_{nullptr};
  TaskHandle_t outbound_task_{nullptr};
  std::atomic<bool> running_{false};
  std::atomic<bool> session_ready_{false};
  std::atomic<bool> tts_active_{false};
  std::atomic<bool> tts_stop_requested_{false};
  std::atomic<bool> tts_playback_complete_{false};
  std::atomic<bool> playback_busy_{false};
  std::atomic<bool> playback_started_{false};
  std::atomic<bool> voice_capture_enabled_{true};
  std::atomic<bool> transport_fault_requested_{false};
  std::atomic<uint32_t> outbound_connection_epoch_{1};
  sesame::protocol::AcceptedSequence control_sequence_{};
  uint32_t expected_control_sequence_{0};
  sesame::protocol::AcceptedSequence audio_sequence_{};
  uint32_t turn_counter_{0};
  std::atomic<uint32_t> active_generation_{0};
  PlaybackTelemetry playback_telemetry_{};
  PlaybackControl playback_control_{};
  PlaybackTurnBinding playback_turn_{};
  std::atomic<uint32_t> expected_downlink_sequence_{0};
  std::array<char, 101> session_id_{};
  std::array<char, 101> turn_id_{};
  OutboundFrame outbound_work_frame_{};
};

}  // namespace sesame::voice
