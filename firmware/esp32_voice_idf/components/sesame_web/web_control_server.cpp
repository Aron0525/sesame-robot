#include "sesame_web/web_control_server.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"

#include "sesame_robot/esp32_servo_driver.h"
#include "sesame_ui/oled_expression_display.h"
#include "sesame_web/captive_portal_html.h"
#include "sesame_web/web_command.h"

namespace sesame::web {
namespace {

constexpr char kTag[] = "sesame_web";
constexpr size_t kMaxQueryLength = 256;
constexpr size_t kMaxBodyLength = 512;

WebControlServer* server_from(httpd_req_t* request) {
  return static_cast<WebControlServer*>(request->user_ctx);
}

bool get_query_value(httpd_req_t* request, const char* key, char* output,
                     size_t output_size) {
  const size_t query_length = httpd_req_get_url_query_len(request);
  if (query_length == 0 || query_length >= kMaxQueryLength) return false;
  std::array<char, kMaxQueryLength> query{};
  if (httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query.data(), key, output, output_size) == ESP_OK;
}

bool parse_integer(const char* value, int* result) {
  if (value == nullptr || value[0] == '\0' || result == nullptr) return false;
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0') {
    return false;
  }
  if constexpr (sizeof(long) > sizeof(int)) {
    if (parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
      return false;
    }
  }
  *result = static_cast<int>(parsed);
  return true;
}

void send_json(httpd_req_t* request, cJSON* document, const char* status = "200 OK") {
  char* payload = cJSON_PrintUnformatted(document);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_status(request, status);
  if (payload != nullptr) {
    httpd_resp_send(request, payload, HTTPD_RESP_USE_STRLEN);
    cJSON_free(payload);
  } else {
    httpd_resp_send(request, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
  }
}

esp_err_t send_result(httpd_req_t* request, bool ok, const char* message,
                      const char* status = "200 OK") {
  cJSON* document = cJSON_CreateObject();
  if (document == nullptr) return ESP_ERR_NO_MEM;
  cJSON_AddBoolToObject(document, "ok", ok);
  cJSON_AddStringToObject(document, "status", ok ? "ok" : "error");
  cJSON_AddStringToObject(document, "message", message);
  send_json(request, document, status);
  cJSON_Delete(document);
  return ESP_OK;
}

}  // namespace

WebControlServer::WebControlServer(robot::Esp32ServoDriver* servos,
                                   ui::OledExpressionDisplay* display,
                                   LegacyMotionRunner* motion_runner)
    : servos_(servos), display_(display), motion_runner_(motion_runner) {}

esp_err_t WebControlServer::start() {
  if (server_ != nullptr) return ESP_OK;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 7;
  config.lru_purge_enable = true;
  ESP_RETURN_ON_ERROR(httpd_start(&server_, &config), kTag,
                      "start HTTP control server");

  const std::array<httpd_uri_t, 7> handlers{{
      {.uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = this},
      {.uri = "/cmd", .method = HTTP_GET, .handler = command_handler, .user_ctx = this},
      {.uri = "/getSettings", .method = HTTP_GET, .handler = get_settings_handler, .user_ctx = this},
      {.uri = "/setSettings", .method = HTTP_GET, .handler = set_settings_handler, .user_ctx = this},
      {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = this},
      {.uri = "/api/catalog", .method = HTTP_GET, .handler = catalog_handler, .user_ctx = this},
      {.uri = "/api/command", .method = HTTP_POST, .handler = api_command_handler, .user_ctx = this},
  }};
  for (const httpd_uri_t& handler : handlers) {
    const esp_err_t result = httpd_register_uri_handler(server_, &handler);
    if (result != ESP_OK) {
      httpd_stop(server_);
      server_ = nullptr;
      return result;
    }
  }
  ESP_LOGI(kTag, "HTTP control page ready on port %d", config.server_port);
  return ESP_OK;
}

void WebControlServer::stop() {
  if (server_ != nullptr) httpd_stop(server_);
  server_ = nullptr;
}

esp_err_t WebControlServer::root_handler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, index_html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebControlServer::command_handler(httpd_req_t* request) {
  return server_from(request)->handle_command(request);
}

esp_err_t WebControlServer::get_settings_handler(httpd_req_t* request) {
  return server_from(request)->handle_get_settings(request);
}

esp_err_t WebControlServer::set_settings_handler(httpd_req_t* request) {
  return server_from(request)->handle_set_settings(request);
}

esp_err_t WebControlServer::status_handler(httpd_req_t* request) {
  return server_from(request)->handle_status(request);
}

esp_err_t WebControlServer::catalog_handler(httpd_req_t* request) {
  return server_from(request)->handle_catalog(request);
}

esp_err_t WebControlServer::api_command_handler(httpd_req_t* request) {
  return server_from(request)->handle_api_command(request);
}

esp_err_t WebControlServer::handle_command(httpd_req_t* request) {
  std::array<char, 32> action{};
  if (get_query_value(request, "stop", action.data(), action.size())) {
    stop_all();
    return send_result(request, true, "stopped");
  }
  if (get_query_value(request, "pose", action.data(), action.size()) ||
      get_query_value(request, "go", action.data(), action.size())) {
    const bool accepted = apply_action(action.data());
    return send_result(request, accepted,
                       accepted ? "movement started" : "movement unavailable",
                       accepted ? "200 OK" : "409 Conflict");
  }

  std::array<char, 16> servo{};
  std::array<char, 16> angle{};
  if (get_query_value(request, "motor", servo.data(), servo.size()) &&
      get_query_value(request, "value", angle.data(), angle.size())) {
    int servo_number = 0;
    int servo_angle = 0;
    const bool parsed = parse_integer(servo.data(), &servo_number) &&
                        parse_integer(angle.data(), &servo_angle);
    const bool accepted = parsed && apply_manual_servo(servo_number, servo_angle);
    return send_result(request, accepted,
                       accepted ? "servo updated" : "invalid servo command",
                       accepted ? "200 OK" : "400 Bad Request");
  }
  return send_result(request, false, "unknown command", "400 Bad Request");
}

esp_err_t WebControlServer::handle_get_settings(httpd_req_t* request) {
  cJSON* document = cJSON_CreateObject();
  if (document == nullptr) return ESP_ERR_NO_MEM;
  cJSON_AddNumberToObject(document, "frameDelay",
                           motion_runner_ == nullptr ? 100 : motion_runner_->frame_delay_ms());
  cJSON_AddNumberToObject(document, "walkCycles",
                           motion_runner_ == nullptr ? 10 : motion_runner_->walk_cycles());
  cJSON_AddNumberToObject(document, "motorCurrentDelay",
                           motion_runner_ == nullptr ? 20 :
                                                       motion_runner_->motor_current_delay_ms());
  cJSON_AddStringToObject(document, "motorSpeed", "medium");
  send_json(request, document);
  cJSON_Delete(document);
  return ESP_OK;
}

esp_err_t WebControlServer::handle_set_settings(httpd_req_t* request) {
  if (motion_runner_ == nullptr) {
    return send_result(request, false, "motion runner unavailable", "503 Service Unavailable");
  }
  int frame_delay = motion_runner_->frame_delay_ms();
  int walk_cycles = motion_runner_->walk_cycles();
  int motor_current_delay = motion_runner_->motor_current_delay_ms();
  std::array<char, 16> value{};
  if (get_query_value(request, "frameDelay", value.data(), value.size()) &&
      !parse_integer(value.data(), &frame_delay)) {
    return send_result(request, false, "invalid frameDelay", "400 Bad Request");
  }
  if (get_query_value(request, "walkCycles", value.data(), value.size()) &&
      !parse_integer(value.data(), &walk_cycles)) {
    return send_result(request, false, "invalid walkCycles", "400 Bad Request");
  }
  if (get_query_value(request, "motorCurrentDelay", value.data(), value.size()) &&
      !parse_integer(value.data(), &motor_current_delay)) {
    return send_result(request, false, "invalid motorCurrentDelay",
                       "400 Bad Request");
  }
  const bool accepted = motion_runner_->set_settings(frame_delay, walk_cycles,
                                                      motor_current_delay);
  return send_result(request, accepted,
                     accepted ? "settings updated" : "settings unavailable while moving",
                     accepted ? "200 OK" : "409 Conflict");
}

esp_err_t WebControlServer::handle_status(httpd_req_t* request) {
  cJSON* document = cJSON_CreateObject();
  if (document == nullptr) return ESP_ERR_NO_MEM;
  const bool moving = motion_runner_ != nullptr && motion_runner_->busy();
  cJSON_AddBoolToObject(document, "ok", true);
  cJSON_AddStringToObject(document, "runtime", "voice-and-web");
  cJSON_AddBoolToObject(document, "webMotionActive", moving);
  cJSON_AddStringToObject(document, "action",
                           moving ? motion_runner_->active_action().data() : "");
  cJSON_AddStringToObject(document, "expression",
                           display_ == nullptr ? "unavailable" : display_->current_expression().data());
  // Keep the field used by the verified Arduino page while retaining the
  // explicit expression field used by the voice control surface.
  cJSON_AddStringToObject(document, "currentFace",
                           display_ == nullptr ? "unavailable" : display_->current_expression().data());
  send_json(request, document);
  cJSON_Delete(document);
  return ESP_OK;
}

esp_err_t WebControlServer::handle_catalog(httpd_req_t* request) {
  cJSON* document = cJSON_CreateObject();
  if (document == nullptr) return ESP_ERR_NO_MEM;
  cJSON* actions = cJSON_AddArrayToObject(document, "actions");
  cJSON* faces = cJSON_AddArrayToObject(document, "faces");
  if (actions == nullptr || faces == nullptr) {
    cJSON_Delete(document);
    return send_result(request, false, "catalog unavailable",
                       "500 Internal Server Error");
  }

  for (std::string_view action : kLegacyActions) {
    cJSON_AddItemToArray(actions, cJSON_CreateString(action.data()));
  }
  if (display_ != nullptr) {
    for (size_t index = 0; index < display_->expression_count(); ++index) {
      const std::string_view expression = display_->expression_name(index);
      if (!expression.empty()) {
        cJSON_AddItemToArray(faces, cJSON_CreateString(expression.data()));
      }
    }
  }
  cJSON_AddBoolToObject(document, "ok", true);
  send_json(request, document);
  cJSON_Delete(document);
  return ESP_OK;
}

esp_err_t WebControlServer::handle_api_command(httpd_req_t* request) {
  if (request->content_len == 0 || request->content_len > kMaxBodyLength) {
    return send_result(request, false, "invalid request body", "400 Bad Request");
  }
  std::array<char, kMaxBodyLength + 1> body{};
  size_t received = 0;
  while (received < static_cast<size_t>(request->content_len)) {
    const int result = httpd_req_recv(request, body.data() + received,
                                      request->content_len - received);
    if (result <= 0) return send_result(request, false, "body receive failed", "400 Bad Request");
    received += static_cast<size_t>(result);
  }

  cJSON* document = cJSON_Parse(body.data());
  if (document == nullptr) return send_result(request, false, "invalid JSON", "400 Bad Request");
  const cJSON* action = cJSON_GetObjectItemCaseSensitive(document, "action");
  if (!cJSON_IsString(action)) action = cJSON_GetObjectItemCaseSensitive(document, "command");
  const cJSON* expression = cJSON_GetObjectItemCaseSensitive(document, "expression");
  if (!cJSON_IsString(expression)) expression = cJSON_GetObjectItemCaseSensitive(document, "face");
  const cJSON* servo = cJSON_GetObjectItemCaseSensitive(document, "servo");
  const cJSON* angle = cJSON_GetObjectItemCaseSensitive(document, "angle");

  bool accepted = false;
  if (cJSON_IsString(expression) && display_ != nullptr &&
      expression->valuestring[0] != '\0') {
    accepted = display_->show_expression(expression->valuestring);
  }
  if (cJSON_IsNumber(servo) || cJSON_IsNumber(angle)) {
    accepted = cJSON_IsNumber(servo) && cJSON_IsNumber(angle) &&
               apply_manual_servo(servo->valueint, angle->valueint);
  } else if (cJSON_IsString(action)) {
    if (std::strcmp(action->valuestring, "stop") == 0) {
      stop_all();
      accepted = true;
    } else {
      accepted = apply_action(action->valuestring);
    }
  }
  cJSON_Delete(document);
  return send_result(request, accepted,
                     accepted ? "command accepted" : "command rejected",
                     accepted ? "200 OK" : "400 Bad Request");
}

bool WebControlServer::apply_action(const char* action) {
  const WebCommand command = parse_web_command(action == nullptr ? "" : action,
                                                "", -1, -1);
  return command.kind == WebCommandKind::kLegacyAction && motion_runner_ != nullptr &&
         motion_runner_->start(command.action.data());
}

bool WebControlServer::apply_manual_servo(int servo_number, int angle) {
  const WebCommand command = parse_web_command("", "", servo_number, angle);
  if (command.kind != WebCommandKind::kManualServo || servos_ == nullptr ||
      (motion_runner_ != nullptr && motion_runner_->busy()) ||
      !servos_->begin_manual_control()) {
    return false;
  }
  return servos_->set_manual_angle(command.servo_index, command.servo_angle);
}

void WebControlServer::stop_all() {
  if (motion_runner_ != nullptr) motion_runner_->stop();
  if (servos_ != nullptr) servos_->end_web_control(true);
}

}  // namespace sesame::web
