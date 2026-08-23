#include "sesame_ui/oled_expression_display.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"

#include "sesame_ui/face_bitmaps.h"
#undef const

namespace sesame::ui {
namespace {

constexpr char kTag[] = "sesame_oled";
constexpr gpio_num_t kSdaPin = GPIO_NUM_8;
constexpr gpio_num_t kSclPin = GPIO_NUM_9;
constexpr uint8_t kAddress = 0x3C;
constexpr uint32_t kI2cFrequencyHz = 400'000;
constexpr uint32_t kI2cTimeoutMs = 1000;
constexpr uint32_t kAnimationTickMs = 20;

struct FaceFrames {
  std::string_view name;
  std::array<const uint8_t*, 6> frames;
  uint8_t fps;
};

constexpr uint8_t fps_for(std::string_view name) {
  if (name == "point") return 5;
  if (name == "dead") return 2;
  if (name == "idle_blink") return 7;
  return 1;
}

#define X(name) \
  FaceFrames{#name, {epd_bitmap_##name, epd_bitmap_##name##_1, \
                     epd_bitmap_##name##_2, epd_bitmap_##name##_3, \
                     epd_bitmap_##name##_4, epd_bitmap_##name##_5}, \
             fps_for(#name)},
const FaceFrames kFaces[] = {FACE_LIST};
#undef X

const FaceFrames* face_for(std::string_view expression) {
  // The upstream list contains the historical spelling "defualt", but its
  // bitmap is absent. Use the known-good idle frame for the protocol-level
  // default expression so OLED initialization can never depend on a null weak
  // bitmap symbol.
  const std::string_view requested =
      expression == "default" ? std::string_view{"idle"} : expression;
  for (const FaceFrames& face : kFaces) {
    if (face.name == requested) return &face;
  }
  return nullptr;
}

const FaceFrames* idle_face() { return face_for("idle"); }

uint8_t frame_count(const std::array<const uint8_t*, 6>& frames) {
  uint8_t count = 0;
  for (const uint8_t* frame : frames) {
    if (frame == nullptr) break;
    ++count;
  }
  return count;
}

template <size_t N>
void copy_string(std::array<char, N>* destination, std::string_view value) {
  const size_t size = std::min(value.size(), destination->size() - 1);
  std::memcpy(destination->data(), value.data(), size);
  (*destination)[size] = '\0';
}

}  // namespace

esp_err_t OledExpressionDisplay::initialize() {
  if (initialized_) return ESP_OK;

  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

  const i2c_master_bus_config_t bus_config{
      .i2c_port = I2C_NUM_0,
      .sda_io_num = kSdaPin,
      .scl_io_num = kSclPin,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .intr_priority = 0,
      .trans_queue_depth = 0,
      .flags = {.enable_internal_pullup = true, .allow_pd = false},
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus_), kTag,
                      "create OLED I2C bus");

  const i2c_device_config_t device_config{
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = kAddress,
      .scl_speed_hz = kI2cFrequencyHz,
      .scl_wait_us = 0,
      .flags = {.disable_ack_check = false},
  };
  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_, &device_config, &device_),
                      kTag, "add OLED I2C device");

  constexpr std::array<uint8_t, 25> kInitCommands{
      0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D,
      0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF,
      0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
  };
  for (uint8_t command : kInitCommands) {
    ESP_RETURN_ON_ERROR(send_command(command), kTag, "initialize SSD1306");
  }
  initialized_ = true;
  if (!show_expression("default")) return ESP_FAIL;
  if (xTaskCreatePinnedToCore(animation_task_entry, "sesame_oled_anim", 4096,
                              this, 3, &animation_task_, tskNO_AFFINITY) !=
      pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool OledExpressionDisplay::show_expression(std::string_view expression,
                                            FaceAnimationMode mode) {
  if (expression == "idle") return enter_idle();
  if (!initialized_ || mutex_ == nullptr ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }
  const bool shown = set_expression_locked(expression, mode, false);
  xSemaphoreGive(mutex_);
  return shown;
}

bool OledExpressionDisplay::set_animation_mode(FaceAnimationMode mode) {
  if (!initialized_ || mutex_ == nullptr ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }
  animation_mode_ = mode;
  frame_direction_ = 1;
  animation_finished_ = false;
  xSemaphoreGive(mutex_);
  return true;
}

bool OledExpressionDisplay::enter_idle() {
  if (!initialized_ || mutex_ == nullptr ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }
  idle_active_ = true;
  idle_blink_active_ = false;
  idle_blinks_remaining_ = 0;
  const bool shown = set_expression_locked("idle", FaceAnimationMode::kBoomerang,
                                           true);
  schedule_idle_blink_locked(3000, 7000);
  xSemaphoreGive(mutex_);
  return shown;
}

void OledExpressionDisplay::exit_idle() {
  if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return;
  }
  idle_active_ = false;
  idle_blink_active_ = false;
  idle_blinks_remaining_ = 0;
  xSemaphoreGive(mutex_);
}

std::string_view OledExpressionDisplay::current_expression() const {
  return current_expression_.data();
}

size_t OledExpressionDisplay::expression_count() const {
  size_t count = 0;
  for (const FaceFrames& face : kFaces) {
    if (frame_count(face.frames) > 0) ++count;
  }
  return count;
}

std::string_view OledExpressionDisplay::expression_name(size_t index) const {
  for (const FaceFrames& face : kFaces) {
    if (frame_count(face.frames) == 0) continue;
    if (index == 0) return face.name;
    --index;
  }
  return {};
}

bool OledExpressionDisplay::set_expression_locked(std::string_view expression,
                                                   FaceAnimationMode mode,
                                                   bool keep_idle) {
  const FaceFrames* face = face_for(expression);
  if (face == nullptr) {
    ESP_LOGW(kTag, "unknown expression: %.*s", static_cast<int>(expression.size()),
             expression.data());
    return false;
  }
  uint8_t count = frame_count(face->frames);
  std::string_view canonical =
      expression == "defualt" ? std::string_view{"default"} : expression;
  if (count == 0) {
    // Some upstream action names intentionally have no bitmap (for example
    // the historical `defualt` spelling and `stand`). Never dereference a
    // weak/null frame pointer: retain a usable OLED and make the fallback
    // visible through the canonical state instead.
    face = idle_face();
    count = face == nullptr ? 0 : frame_count(face->frames);
    if (count == 0) {
      ESP_LOGE(kTag, "no usable OLED fallback bitmap");
      return false;
    }
    ESP_LOGW(kTag, "expression bitmap unavailable; falling back to idle");
    canonical = "idle";
  }

  current_frames_ = face->frames;
  current_frame_count_ = count;
  current_frame_index_ = 0;
  current_fps_ = std::max<uint8_t>(face->fps, 1);
  animation_mode_ = mode;
  frame_direction_ = 1;
  animation_finished_ = false;
  last_frame_ms_ = esp_log_timestamp();
  if (!keep_idle) {
    idle_active_ = false;
    idle_blink_active_ = false;
    idle_blinks_remaining_ = 0;
  }

  const esp_err_t result = write_bitmap(current_frames_[0]);
  if (result == ESP_OK) copy_string(&current_expression_, canonical);
  return result == ESP_OK;
}

void OledExpressionDisplay::tick_animation() {
  if (!initialized_ || mutex_ == nullptr || xSemaphoreTake(mutex_, 0) != pdTRUE) {
    return;
  }
  const uint32_t now_ms = esp_log_timestamp();

  if (idle_active_ && !idle_blink_active_ &&
      static_cast<int32_t>(now_ms - next_idle_blink_ms_) >= 0) {
    idle_blink_active_ = true;
    idle_blinks_remaining_ = (esp_random() % 100U) < 30U ? 1 : 0;
    set_expression_locked("idle_blink", FaceAnimationMode::kOnce, true);
  } else if (idle_active_ && idle_blink_active_ && animation_finished_) {
    idle_blink_active_ = false;
    set_expression_locked("idle", FaceAnimationMode::kBoomerang, true);
    if (idle_blinks_remaining_ > 0) {
      --idle_blinks_remaining_;
      schedule_idle_blink_locked(120, 220);
    } else {
      schedule_idle_blink_locked(3000, 7000);
    }
  }

  const uint32_t interval_ms = 1000U / std::max<uint8_t>(current_fps_, 1);
  if (current_frame_count_ > 1 &&
      !(animation_mode_ == FaceAnimationMode::kOnce && animation_finished_) &&
      now_ms - last_frame_ms_ >= interval_ms) {
    last_frame_ms_ = now_ms;
    if (animation_mode_ == FaceAnimationMode::kLoop) {
      current_frame_index_ =
          static_cast<uint8_t>((current_frame_index_ + 1) % current_frame_count_);
    } else if (animation_mode_ == FaceAnimationMode::kOnce) {
      if (current_frame_index_ + 1 >= current_frame_count_) {
        current_frame_index_ = current_frame_count_ - 1;
        animation_finished_ = true;
      } else {
        ++current_frame_index_;
      }
    } else if (frame_direction_ > 0) {
      if (current_frame_index_ + 1 >= current_frame_count_) {
        frame_direction_ = -1;
        if (current_frame_index_ > 0) --current_frame_index_;
      } else {
        ++current_frame_index_;
      }
    } else if (current_frame_index_ == 0) {
      frame_direction_ = 1;
      if (current_frame_count_ > 1) ++current_frame_index_;
    } else {
      --current_frame_index_;
    }
    write_bitmap(current_frames_[current_frame_index_]);
  }
  xSemaphoreGive(mutex_);
}

void OledExpressionDisplay::schedule_idle_blink_locked(uint32_t minimum_ms,
                                                        uint32_t maximum_ms) {
  const uint32_t range = maximum_ms > minimum_ms ? maximum_ms - minimum_ms : 1;
  next_idle_blink_ms_ = esp_log_timestamp() + minimum_ms +
                        (esp_random() % range);
}

void OledExpressionDisplay::animation_task_entry(void* context) {
  auto* self = static_cast<OledExpressionDisplay*>(context);
  while (true) {
    self->tick_animation();
    vTaskDelay(pdMS_TO_TICKS(kAnimationTickMs));
  }
}

esp_err_t OledExpressionDisplay::send_command(uint8_t command) {
  const std::array<uint8_t, 2> data{0x00, command};
  return i2c_master_transmit(device_, data.data(), data.size(), kI2cTimeoutMs);
}

esp_err_t OledExpressionDisplay::write_bitmap(const uint8_t* source) {
  if (source == nullptr) return ESP_ERR_INVALID_ARG;
  std::array<uint8_t, kBitmapBytes> page_data{};
  for (size_t y = 0; y < kHeight; ++y) {
    for (size_t x = 0; x < kWidth; ++x) {
      const uint8_t source_byte = source[y * (kWidth / 8) + (x / 8)];
      if ((source_byte & (0x80u >> (x % 8))) != 0) {
        page_data[(y / 8) * kWidth + x] |= 1u << (y % 8);
      }
    }
  }

  ESP_RETURN_ON_ERROR(send_command(0x21), kTag, "set SSD1306 column mode");
  ESP_RETURN_ON_ERROR(send_command(0x00), kTag, "set SSD1306 column start");
  ESP_RETURN_ON_ERROR(send_command(0x7F), kTag, "set SSD1306 column end");
  ESP_RETURN_ON_ERROR(send_command(0x22), kTag, "set SSD1306 page mode");
  ESP_RETURN_ON_ERROR(send_command(0x00), kTag, "set SSD1306 page start");
  ESP_RETURN_ON_ERROR(send_command(0x07), kTag, "set SSD1306 page end");

  std::array<uint8_t, 17> packet{};
  packet[0] = 0x40;
  for (size_t offset = 0; offset < page_data.size(); offset += 16) {
    std::memcpy(packet.data() + 1, page_data.data() + offset, 16);
    ESP_RETURN_ON_ERROR(i2c_master_transmit(device_, packet.data(), packet.size(),
                                             kI2cTimeoutMs),
                        kTag, "write SSD1306 pixels");
  }
  return ESP_OK;
}

}  // namespace sesame::ui

