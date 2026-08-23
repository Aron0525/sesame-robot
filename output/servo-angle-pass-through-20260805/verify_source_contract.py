from pathlib import Path
repo = Path('/Users/mac/Desktop/sesame')
runner = (repo / 'firmware/esp32_voice_idf/components/sesame_web/legacy_motion_runner.cpp').read_text()
legacy = (repo / 'firmware/esp32_voice_idf/components/sesame_web/include/sesame_web/legacy_movement_sequences.h').read_text()
arduino = (repo / 'firmware/arduino/Sesame_Robot_WiFi_Controller/Sesame_Robot_WiFi_Controller.ino').read_text()
calibration = repo / 'firmware/esp32_voice_idf/components/sesame_web/include/sesame_web/legacy_motion_calibration.h'
assert 'g_runner->set_servo_angle(channel, static_cast<uint8_t>(angle));' in runner
assert 'physical_angle_for_legacy_motion' not in runner
assert not calibration.exists()
assert 'setServoAngle(R3, 180);' in legacy
assert 'setServoAngle(L4, 180);' in legacy
assert 'servoSubtrim' not in arduino
assert 'servos[channel].write(angle);' in arduino
assert 'adjustedAngle' not in arduino
print('PASS direct_angle_forwarding=true')
print('PASS legacy_calibration_deleted=true')
print('PASS stand_pose_R3_L4=180,180')
print('PASS arduino_subtrim_deleted=true')
