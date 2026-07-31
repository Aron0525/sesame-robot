#include "Arduino.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "sesame_audio/audio_hal.h"
#include "sesame_protocol/turn_state.h"
#include "sesame_robot/esp32_servo_driver.h"
#include "sesame_robot/robot_adapter.h"
#include "sesame_ui/oled_expression_display.h"
#include "sesame_voice/voice_controller.h"
#include "sesame_web/legacy_motion_runner.h"
#include "sesame_web/web_control_server.h"

namespace {
constexpr char kTag[] = "sesame_main";

void cancel_local_web_motion(void* context) {
  auto* runner = static_cast<sesame::web::LegacyMotionRunner*>(context);
  if (runner != nullptr) runner->stop();
}
}

extern "C" void app_main() {
  initArduino();
  Serial.begin(115200);

  sesame::protocol::TurnStateMachine turn_state;

  ESP_LOGI(kTag, "Sesame Robot V3 production firmware starting");
  ESP_LOGI(kTag, "Initial turn state: %s",
           sesame::protocol::to_string(turn_state.state()));

  // The HTTP control server may be started before the voice client connects
  // Wi-Fi. Bring up the shared ESP network/event foundations first; the
  // GatewayClient performs the idempotent STA configuration afterwards.
  esp_err_t network_result = esp_netif_init();
  if (network_result != ESP_OK && network_result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "Network stack initialization failed: %s",
             esp_err_to_name(network_result));
    return;
  }
  network_result = esp_event_loop_create_default();
  if (network_result != ESP_OK && network_result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "Network event loop initialization failed: %s",
             esp_err_to_name(network_result));
    return;
  }

  static sesame::ui::OledExpressionDisplay display;
  const esp_err_t display_result = display.initialize();
  if (display_result != ESP_OK) {
    ESP_LOGW(kTag, "OLED unavailable: %s", esp_err_to_name(display_result));
  }

  static sesame::robot::Esp32ServoDriver robot_driver(&display);
  const esp_err_t robot_result = robot_driver.initialize();
  if (robot_result != ESP_OK) {
    ESP_LOGE(kTag, "Servo driver initialization failed: %s",
             esp_err_to_name(robot_result));
    return;
  }

  static sesame::web::LegacyMotionRunner legacy_motion(&robot_driver, &display);
  static sesame::robot::RobotAdapter robot(&robot_driver, cancel_local_web_motion,
                                            &legacy_motion);
  static sesame::web::WebControlServer web_control(&robot_driver, &display,
                                                    &legacy_motion);
  const esp_err_t web_result = web_control.start();
  if (web_result != ESP_OK) {
    ESP_LOGW(kTag, "HTTP control page unavailable: %s", esp_err_to_name(web_result));
  }

  // Manual web, servo and OLED control must remain usable even when audio,
  // provisioning or the WSS gateway is unavailable.
  static sesame::audio::AudioHal audio;
  const esp_err_t audio_result = audio.initialize();
  if (audio_result != ESP_OK) {
    ESP_LOGE(kTag, "Audio initialization failed; web controls stay online: %s",
             esp_err_to_name(audio_result));
    return;
  }
  ESP_LOGI(kTag, "Audio HAL ready");

  static sesame::voice::VoiceController voice(&audio, &robot);
  const esp_err_t voice_result = voice.start();
  if (voice_result != ESP_OK) {
    ESP_LOGE(kTag,
             "Voice runtime did not start; web controls stay online: %s",
             esp_err_to_name(voice_result));
    return;
  }
  ESP_LOGI(kTag,
           "Opus/WSS voice runtime ready; HTTP page is available on the same Wi-Fi");
}
