idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES arduino-esp32 sesame_audio sesame_protocol sesame_robot
             sesame_ui sesame_voice sesame_web
)
