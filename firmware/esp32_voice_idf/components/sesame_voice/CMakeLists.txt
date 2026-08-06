idf_component_register(
    SRCS "recording_button.cpp" "voice_controller.cpp" "voice_turn_detector.cpp"
         "wake_vad_engine.cpp"
    INCLUDE_DIRS "include"
    EMBED_FILES "assets/wake_ack.pcm"
    REQUIRES sesame_audio sesame_protocol sesame_transport sesame_robot
             esp_driver_gpio esp_timer json freertos espressif__esp-sr
)
