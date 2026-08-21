idf_component_register(
    SRCS "capture_session.cpp" "conversation_store.cpp" "pcm_preroll_buffer.cpp" "recording_button.cpp" "speaker_verification.cpp" "speaker_verification_store.cpp" "voice_controller.cpp" "voice_turn_detector.cpp" "wake_threshold_store.cpp"
         "wake_vad_engine.cpp" "wake_ack_audio.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES sesame_audio sesame_protocol sesame_transport sesame_robot
             esp_driver_gpio esp_timer json freertos espressif__esp-sr
)
