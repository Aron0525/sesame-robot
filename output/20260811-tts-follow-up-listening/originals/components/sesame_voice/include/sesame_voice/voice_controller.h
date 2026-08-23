#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sesame_audio/audio_hal.h"
#include "sesame_audio/opus_codec.h"
#include "sesame_protocol/control_event.h"
#include "sesame_protocol/turn_state.h"
#include "sesame_robot/robot_adapter.h"
#include "sesame_transport/device_config.h"
#include "sesame_transport/gateway_client.h"
#include "sesame_voice/voice_turn_detector.h"
#include "sesame_voice/wake_vad_engine.h"
#include "sesame_voice/recording_button.h"

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
  struct DownlinkPacket {
    uint32_t generation_id;
    uint32_t sequence;
    uint16_t size;
    std::array<uint8_t, sesame::audio::OpusCodec::kMaxPacketBytes> data;
  };

  static void task_entry(void* context);
  void run();
  void handle_button(ButtonEvent event, uint64_t now_ms);
  void capture_and_process(uint64_t now_ms);
  void encode_and_send(const int16_t* pcm, size_t samples, uint64_t now_ms);
  void process_wake_vad_signals(uint64_t now_ms);
  void handle_turn_event(VoiceTurnEvent event, uint64_t now_ms);
  void begin_listening(uint64_t now_ms);
  void finish_listening();
  void play_pending_audio();
  esp_err_t send_control(sesame::protocol::ControlEventType type,
                         const char* payload_json,
                         const char* request_id = nullptr);
  void send_session_hello();
  void process_control_json(const char* data, size_t size);
  void begin_tts(uint32_t generation_id);
  void finish_tts(uint32_t generation_id);
  void complete_tts_if_drained();
  void flush_tts();

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
  TaskHandle_t task_{nullptr};
  std::atomic<bool> running_{false};
  std::atomic<bool> session_ready_{false};
  std::atomic<bool> tts_active_{false};
  std::atomic<bool> tts_stop_requested_{false};
  std::atomic<bool> voice_capture_enabled_{true};
  uint32_t control_sequence_{0};
  uint32_t expected_control_sequence_{0};
  uint32_t audio_sequence_{0};
  uint32_t turn_counter_{0};
  uint32_t active_generation_{0};
  uint32_t expected_downlink_sequence_{0};
  std::array<char, 101> session_id_{};
  std::array<char, 101> turn_id_{};
};

}  // namespace sesame::voice
