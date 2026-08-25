idf_component_register(
    SRCS "audio_hal.cpp" "audio_buffers.cpp" "opus_codec.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2s esp_audio_codec
)
