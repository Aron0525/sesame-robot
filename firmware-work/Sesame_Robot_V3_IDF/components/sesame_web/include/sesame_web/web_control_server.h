#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include "sesame_web/legacy_motion_runner.h"

namespace sesame::robot {
class Esp32ServoDriver;
}

namespace sesame::ui {
class OledExpressionDisplay;
}

namespace sesame::web {

class WebControlServer {
 public:
  WebControlServer(robot::Esp32ServoDriver* servos,
                   ui::OledExpressionDisplay* display,
                   LegacyMotionRunner* motion_runner);

  esp_err_t start();
  void stop();

 private:
  static esp_err_t root_handler(httpd_req_t* request);
  static esp_err_t command_handler(httpd_req_t* request);
  static esp_err_t get_settings_handler(httpd_req_t* request);
  static esp_err_t set_settings_handler(httpd_req_t* request);
  static esp_err_t status_handler(httpd_req_t* request);
  static esp_err_t catalog_handler(httpd_req_t* request);
  static esp_err_t api_command_handler(httpd_req_t* request);

  esp_err_t handle_command(httpd_req_t* request);
  esp_err_t handle_get_settings(httpd_req_t* request);
  esp_err_t handle_set_settings(httpd_req_t* request);
  esp_err_t handle_status(httpd_req_t* request);
  esp_err_t handle_catalog(httpd_req_t* request);
  esp_err_t handle_api_command(httpd_req_t* request);
  bool apply_action(const char* action);
  bool apply_manual_servo(int servo_number, int angle);
  void stop_all();

  robot::Esp32ServoDriver* servos_{nullptr};
  ui::OledExpressionDisplay* display_{nullptr};
  LegacyMotionRunner* motion_runner_{nullptr};
  httpd_handle_t server_{nullptr};
};

}  // namespace sesame::web

