idf_component_register(
    SRCS "recording_button.cpp" "voice_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES sesame_audio sesame_protocol sesame_transport sesame_robot
             esp_driver_gpio esp_timer json freertos
)
