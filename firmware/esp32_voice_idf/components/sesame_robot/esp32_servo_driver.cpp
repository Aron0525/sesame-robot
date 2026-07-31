#include "sesame_robot/esp32_servo_driver.h"

#include <array>
#include <cstring>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#include "sesame_robot/motion_plan.h"
#include "sesame_robot/servo_calibration.h"
#include "sesame_ui/oled_expression_display.h"

namespace sesame::robot {
namespace {

constexpr char kTag[] = "sesame_servo";
constexpr std::array<int, 8> kServoPins{4, 5, 6, 7, 10, 11, 12, 13};
constexpr uint32_t kServoFrequencyHz = 50;
constexpr uint32_t kServoPeriodUs = 1'000'000 / kServoFrequencyHz;
constexpr uint32_t kMinimumPulseUs = 732;
constexpr uint32_t kMaximumPulseUs = 2929;
constexpr uint32_t kDutyMaximum = (1u << 14) - 1;

struct MotionCommand {
  Esp32ServoDriver::MotionId action;
};

}  // namespace

Esp32ServoDriver::Esp32ServoDriver(sesame::ui::OledExpressionDisplay* display)
    : executor_(this), display_(display) {}

esp_err_t Esp32ServoDriver::initialize() {
  if (running_.load()) return ESP_OK;

  ledc_timer_config_t timer_config{};
  timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_config.duty_resolution = LEDC_TIMER_14_BIT;
  timer_config.timer_num = LEDC_TIMER_0;
  timer_config.freq_hz = kServoFrequencyHz;
  timer_config.clk_cfg = LEDC_AUTO_CLK;
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), kTag,
                      "configure servo PWM timer");

  for (size_t index = 0; index < kServoPins.size(); ++index) {
    ledc_channel_config_t channel_config{};
    channel_config.gpio_num = kServoPins[index];
    channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_config.channel = static_cast<ledc_channel_t>(index);
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = LEDC_TIMER_0;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), kTag,
                        "configure servo PWM channel");
  }
  pwm_configured_.store(true);
  release_all();

  command_queue_ = xQueueCreate(4, sizeof(MotionCommand));
  if (command_queue_ == nullptr) {
    pwm_configured_.store(false);
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCore(task_entry, "sesame_servo", 4096, this, 5,
                              &task_, 1) != pdPASS) {
    vQueueDelete(command_queue_);
    command_queue_ = nullptr;
    pwm_configured_.store(false);
    return ESP_ERR_NO_MEM;
  }
  running_.store(true);
  ESP_LOGI(kTag,
           "S0-S7 PWM ready; outputs remain released until an allowed action");
  return ESP_OK;
}

bool Esp32ServoDriver::safe_for_motion() const {
  return running_.load() && !stop_requested_.load() &&
         !web_control_active_.load();
}

bool Esp32ServoDriver::execute_action(const char* action, uint32_t) {
  if (!safe_for_motion() || action == nullptr || command_queue_ == nullptr) {
    return false;
  }

  MotionCommand command{};
  if (std::strcmp(action, "rest") == 0) {
    command.action = MotionId::kRest;
  } else if (std::strcmp(action, "stand") == 0) {
    command.action = MotionId::kStand;
  } else if (std::strcmp(action, "wave") == 0) {
    command.action = MotionId::kWave;
  } else {
    return false;
  }
  return xQueueSend(command_queue_, &command, 0) == pdPASS;
}

bool Esp32ServoDriver::set_expression(const char* expression, uint32_t) {
  return expression != nullptr && display_ != nullptr &&
         display_->show_expression(expression);
}

void Esp32ServoDriver::emergency_stop() {
  for (std::atomic<bool>& valid : manual_angle_valid_) valid.store(false);
  manual_control_active_.store(false);
  web_control_active_.store(false);
  stop_requested_.store(true);
  release_all();
}

bool Esp32ServoDriver::begin_web_motion() {
  if (!running_.load()) return false;
  // A legacy movement deliberately takes ownership away from any held manual
  // sliders before its task starts issuing poses.
  manual_control_active_.store(false);
  for (std::atomic<bool>& valid : manual_angle_valid_) valid.store(false);
  web_control_active_.store(true);
  stop_requested_.store(true);
  release_all();
  return true;
}

bool Esp32ServoDriver::begin_manual_control() {
  if (!running_.load()) return false;

  // Repeated slider updates must not release the prior PWM outputs. On the
  // first manual update after a movement/safety stop, cancel that owner and
  // start from released outputs; subsequent updates retain all saved angles.
  if (web_control_active_.load() && manual_control_active_.load()) {
    return true;
  }
  web_control_active_.store(true);
  manual_control_active_.store(false);
  for (std::atomic<bool>& valid : manual_angle_valid_) valid.store(false);
  stop_requested_.store(true);
  release_all();
  return true;
}

void Esp32ServoDriver::end_web_control(bool release_outputs) {
  manual_control_active_.store(false);
  web_control_active_.store(false);
  if (release_outputs) {
    for (std::atomic<bool>& valid : manual_angle_valid_) valid.store(false);
    stop_requested_.store(true);
    release_all();
  }
}

bool Esp32ServoDriver::set_manual_angle(uint8_t servo_index, uint8_t angle) {
  if (!web_control_active_.load() || servo_index >= kServoPins.size() ||
      angle > 180) {
    return false;
  }
  manual_angles_[servo_index].store(angle);
  manual_angle_valid_[servo_index].store(true);
  manual_control_active_.store(true);
  return write_logical_angle(servo_index, angle);
}

bool Esp32ServoDriver::set_motion_angle(uint8_t servo_index, uint8_t angle) {
  if (!web_control_active_.load() || manual_control_active_.load() ||
      servo_index >= kServoPins.size() || angle > 180) {
    return false;
  }
  return write_logical_angle(servo_index, angle);
}

void Esp32ServoDriver::apply_pose(
    const std::array<uint8_t, 8>& angles) {
  if (!pwm_configured_.load()) return;
  for (size_t index = 0; index < angles.size(); ++index) {
    static_cast<void>(write_logical_angle(static_cast<uint8_t>(index),
                                          angles[index]));
  }
}

void Esp32ServoDriver::release_all() {
  if (!pwm_configured_.load()) return;
  for (size_t index = 0; index < kServoPins.size(); ++index) {
    const auto channel = static_cast<ledc_channel_t>(index);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
  }
}

void Esp32ServoDriver::apply_manual_pose() {
  if (!pwm_configured_.load() || !web_control_active_.load() ||
      !manual_control_active_.load()) {
    return;
  }
  for (size_t index = 0; index < kServoPins.size(); ++index) {
    // A slider session may have set only a subset of the eight channels.
    // Never infer an unset channel as 0 degrees when restoring its held pose.
    if (!manual_angle_valid_[index].load()) continue;
    static_cast<void>(write_logical_angle(
        static_cast<uint8_t>(index), manual_angles_[index].load()));
  }
}

void Esp32ServoDriver::task_entry(void* context) {
  static_cast<Esp32ServoDriver*>(context)->run();
}

void Esp32ServoDriver::run() {
  while (true) {
    if (stop_requested_.exchange(false)) {
      executor_.cancel();
      xQueueReset(command_queue_);
      // MotionExecutor::cancel releases all channels. Restore a held manual
      // pose only when a slider is still the active owner; safety stop and a
      // legacy action both keep outputs released instead.
      apply_manual_pose();
      continue;
    }

    MotionCommand command{};
    if (xQueueReceive(command_queue_, &command, pdMS_TO_TICKS(10)) == pdTRUE) {
      executor_.start(motion_plan_for(action_name(command.action)),
                      esp_log_timestamp());
    }
    executor_.tick(esp_log_timestamp());
  }
}

const char* Esp32ServoDriver::action_name(MotionId action) {
  switch (action) {
    case MotionId::kRest:
      return "rest";
    case MotionId::kStand:
      return "stand";
    case MotionId::kWave:
      return "wave";
  }
  return "";
}

bool Esp32ServoDriver::write_logical_angle(uint8_t servo_index,
                                           uint8_t logical_angle) {
  if (!pwm_configured_.load() || servo_index >= kServoPins.size() ||
      logical_angle > 180) {
    return false;
  }
  const auto channel = static_cast<ledc_channel_t>(servo_index);
  const uint8_t physical_angle =
      physical_angle_for_servo(servo_index, logical_angle);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty_for_angle(physical_angle));
  return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel) == ESP_OK;
}

uint32_t Esp32ServoDriver::duty_for_angle(uint8_t angle) {
  const uint32_t pulse_us =
      kMinimumPulseUs +
      (static_cast<uint32_t>(angle) * (kMaximumPulseUs - kMinimumPulseUs)) /
          180;
  return (pulse_us * kDutyMaximum) / kServoPeriodUs;
}

}  // namespace sesame::robot
