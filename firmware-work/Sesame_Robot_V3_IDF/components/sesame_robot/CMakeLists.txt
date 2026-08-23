idf_component_register(
    SRCS "action_policy.cpp" "robot_adapter.cpp" "motion_plan.cpp"
         "motion_executor.cpp" "esp32_servo_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer freertos sesame_ui
)
