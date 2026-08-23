#line 1 "/Users/mac/Desktop/1/SesameV3_语音机器人项目/01-设备端/主固件/Sesame_Robot_V3/README-V3.md"
# Sesame Robot V3 固件工程

Arduino IDE 请打开 `Sesame_Robot_V3.ino`。依赖库位于上级目录的 `Arduino依赖库/`。

## V3 已启用的引脚

- OLED SDA：GPIO 8
- OLED SCL：GPIO 9
- Motor 0–7：GPIO 4、5、6、7、10、11、12、13

## Arduino IDE

- Board：ESP32S3 Dev Module
- USB CDC On Boot：Enabled
- Flash Mode：QIO 80MHz
- Partition Scheme：Default 4MB with spiffs
- Serial Monitor：115200 baud

## 所需库

- ESP32Servo 3.0.9
- Adafruit SSD1306
- Adafruit GFX Library

不要删除或移动同目录下的三个 `.h` 文件。
