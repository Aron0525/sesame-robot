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
#include "sesame_transport/transport_policy.h"
#include "sesame_voice/capture_session.h"
#include "sesame_voice/conversation_store.h"
#include "sesame_voice/gateway_connection_state.h"
#include "sesame_voice/pcm_preroll_buffer.h"
#include "sesame_voice/recording_button.h"
#include "sesame_voice/voice_turn_detector.h"
#include "sesame_voice/wake_vad_engine.h"

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
  static constexpr size_t kMaxGatewayEventBytes = 2048;
  // Keep this deliberately small: TLS and Wi-Fi need a contiguous internal
  // allocation, while the voice task drains this queue every loop iteration.
  static constexpr UBaseType_t kGatewayEventQueueDepth = 4;
  static constexpr UBaseType_t kOutboundQueueDepth = 6;
  // WSS/TLS writes have a substantially deeper call chain than queueing an
  // audio frame. Keep the sender isolated from the realtime voice task and
  // budget enough internal stack for mbedTLS plus ESP transport layers.
  static constexpr uint32_t kOutboundTaskStackBytes = 24576;
  // ESP Opus SILK encoding uses more than 12 KiB on real speech. Keep a
  // measured safety margin in PSRAM so the larger stack does not starve
  // mbedTLS, which is configured to allocate from internal RAM only.
  static constexpr uint32_t kVoiceTaskStackBytes = 32768;
  static constexpr int kGatewayEventsPerTick = 16;

  enum class GatewayEventKind : uint8_t {
    kConnected,
    kDisconnected,
    kText,
  };

  // Ordered control callbacks execute on the WebSocket task. TTS packets use
  // the separate compact downlink queue, so an audio burst cannot crowd out
  // tts.stop or interrupt confirmation.
  struct GatewayEvent {
    GatewayEventKind kind{GatewayEventKind::kText};
    uint16_t size{0};
    std::array<uint8_t, kMaxGatewayEventBytes> data{};
  };

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
  static void outbound_task_entry(void* context);
  void run();
  void outbound_loop();
  void maintain_gateway_connection(uint64_t now_ms);
  void schedule_gateway_retry(uint64_t now_ms);
  void process_gateway_events();
  void process_gateway_event(const GatewayEvent& event);
  void handle_gateway_connected();
  void handle_gateway_disconnected();
  void enqueue_downlink_packet(const uint8_t* data, size_t size);
  void enqueue_gateway_event(GatewayEventKind kind, const void* data,
                             size_t size);
  esp_err_t enqueue_outbound_text(const char* data, size_t size);
  esp_err_t enqueue_outbound_binary(const uint8_t* data, size_t size);
  esp_err_t enqueue_outbound(OutboundFrameKind kind, const void* data,
                             size_t size, TickType_t wait_ticks);
  void discard_outbound_frames();
  void handle_button(ButtonEvent event, uint64_t now_ms);
  void process_wake_vad_signals(uint64_t now_ms);
  void begin_wake_ack();
  void play_wake_ack_frame(uint64_t now_ms);
  void stop_wake_ack();
  bool start_listening(uint64_t now_ms, CaptureSource source);
  void finish_listening(const char* trigger);
  void drain_pcm_uplink(size_t maximum_frames);
  void capture_and_send(const int16_t* pcm, uint64_t now_ms);
  void play_pending_audio();
  esp_err_t send_control(sesame::protocol::ControlEventType type,
                         const char* payload_json,
                         const char* request_id = nullptr,
                         bool include_active_turn = true);
  void send_session_hello();
  void process_control_json(const char* data, size_t size);
  void begin_tts(uint32_t generation_id);
  void finish_tts(uint32_t generation_id);
  void complete_tts_if_drained();
  void fail_tts_playback(const char* reason);
  void flush_tts(bool clear_downlink = true);
  void send_action_result(sesame::robot::ActionDecision decision,
                          const char* request_id);
  void send_operator_result(bool accepted, const char* request_id);

  sesame::audio::AudioHal* audio_;
  sesame::robot::RobotAdapter* robot_;
  sesame::audio::OpusCodec codec_;
  ConversationStore conversation_store_;
  sesame::transport::StoredDeviceConfig config_{};
  sesame::transport::GatewayClient gateway_;
  sesame::protocol::TurnStateMachine turn_state_;
  GatewayConnectionState gateway_connection_;
  RecordingButton button_{80};
  CaptureSession capture_session_;
  WakeVadEngine wake_vad_;
  VoiceTurnDetector wake_turn_detector_{{3000, 800, 10000, 3, 2}};
  // The 16-KiB PCM history lives in PSRAM so it cannot consume the internal
  // contiguous heap required by Wi-Fi and mbedTLS.
  PcmPreRollBuffer* pcm_preroll_{nullptr};
  bool wake_ack_active_{false};
  size_t wake_ack_offset_samples_{0};
  QueueHandle_t downlink_queue_{nullptr};
  QueueHandle_t gateway_event_queue_{nullptr};
  QueueHandle_t outbound_queue_{nullptr};
  TaskHandle_t task_{nullptr};
  TaskHandle_t outbound_task_{nullptr};
  std::atomic<bool> running_{false};
  std::atomic<bool> session_ready_{false};
  std::atomic<bool> tts_active_{false};
  std::atomic<bool> tts_stop_requested_{false};
  sesame::transport::ReconnectSchedule gateway_reconnect_schedule_{};
  uint64_t gateway_connect_deadline_ms_{0};
  uint64_t next_gateway_attempt_ms_{0};
  std::atomic<uint32_t> outbound_connection_epoch_{1};
  std::atomic<bool> transport_fault_requested_{false};
  std::atomic<bool> downlink_fault_requested_{false};
  std::atomic<bool> first_uplink_pending_{false};
  uint32_t control_sequence_{0};
  uint32_t expected_control_sequence_{0};
  uint32_t audio_sequence_{0};
  uint32_t turn_counter_{0};
  uint32_t planned_generation_{0};
  uint32_t active_generation_{0};
  uint32_t expected_downlink_sequence_{0};
  uint32_t uplink_frame_count_{0};
  uint32_t pending_interrupt_generation_{0};
  bool interrupt_pending_{false};
  std::array<char, 101> session_id_{};
  std::array<char, 101> turn_id_{};
  std::array<char, 101> pending_interrupt_turn_id_{};
  // WebSocket callbacks are serialized by the client event task. Keep their
  // large queue work items in controller storage instead of consuming that
  // task's TLS-sensitive stack on every received frame.
  GatewayEvent gateway_event_work_{};
  DownlinkPacket downlink_work_packet_{};
  // This object is static storage through VoiceController. Keeping the 1-KiB
  // WSS work item off the sender task stack prevents the payload buffer from
  // colliding with the deep TLS call chain.
  OutboundFrame outbound_work_frame_{};
};

}  // namespace sesame::voice
