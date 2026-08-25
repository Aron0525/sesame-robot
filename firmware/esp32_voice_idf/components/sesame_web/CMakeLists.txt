idf_component_register(
    SRCS "web_command.cpp" "legacy_motion_runner.cpp" "web_control_server.cpp"
    INCLUDE_DIRS "include"
    REQUIRES arduino-esp32 esp_http_server json freertos sesame_robot sesame_ui
)
