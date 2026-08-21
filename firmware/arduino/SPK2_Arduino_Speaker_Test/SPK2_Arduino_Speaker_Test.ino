/*
 * Stand-alone M5Stack Hat SPK2 test for the Sesame ESP32-S3.
 *
 * This keeps the ESP32-audioI2S flow from M5Stack's SPEAKER2 example,
 * but uses this project's physical wiring instead of M5StickC Plus pins:
 *
 *   ESP32-S3 GPIO 1 -> SPK2 BCLK (Hat G26)
 *   ESP32-S3 GPIO 2 -> SPK2 LRCLK (Hat G0)
 *   ESP32-S3 GPIO 3 -> SPK2 SDATA (Hat G25/G36)
 *   ESP32-S3 3V3    -> SPK2 3V3
 *   ESP32-S3 GND    -> SPK2 GND
 *
 * Requirements:
 *   - ESP32 Arduino Core 3.x
 *   - ESP32-audioI2S-master 4.0.0
 *   - an ignored local-config.h beside this sketch containing the Wi-Fi data
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Audio.h>

#include "local-config.h"

#ifndef SPEAKER_TEST_WIFI_SSID
#error "Create local-config.h and define SPEAKER_TEST_WIFI_SSID."
#endif

#ifndef SPEAKER_TEST_WIFI_PASSWORD
#error "Create local-config.h and define SPEAKER_TEST_WIFI_PASSWORD."
#endif

namespace {

constexpr uint8_t kI2sBclk = 1;
constexpr uint8_t kI2sLrclk = 2;
constexpr uint8_t kI2sData = 3;
constexpr uint8_t kVolume = 21;  // ESP32-audioI2S range: 0...21.

// Continuous web radio from the M5Stack SPEAKER2 reference sketch.  It is
// used only to make the external SPK2 play independently of this project's
// TTS and normal voice-gateway firmware.
constexpr char kTestStream[] =
    "http://vis.media-ice.musicradio.com/CapitalMP3";

Audio audio;

void onAudioInfo(Audio::msg_t message) {
  Serial.printf("audio: %s: %s\n", message.s, message.msg);
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(SPEAKER_TEST_WIFI_SSID, SPEAKER_TEST_WIFI_PASSWORD);

  const uint32_t deadline = millis() + 30000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Wi-Fi connection failed; status=%d\n", WiFi.status());
    return false;
  }

  Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nSPK2 Arduino speaker test starting");

  if (!psramFound()) {
    Serial.println("ERROR: OPI PSRAM was not detected. This Audio library requires PSRAM.");
    return;
  }

  Audio::audio_info_callback = onAudioInfo;
  if (!audio.setPinout(kI2sBclk, kI2sLrclk, kI2sData)) {
    Serial.println("ERROR: I2S pin routing failed.");
    return;
  }
  audio.setVolume(kVolume);
  Serial.printf("I2S ready: BCLK=%u LRCLK=%u DATA=%u, volume=%u/21\n",
                kI2sBclk, kI2sLrclk, kI2sData, kVolume);

  if (!connectWiFi()) {
    return;
  }
  if (!audio.connecttohost(kTestStream)) {
    Serial.println("ERROR: unable to start the test stream.");
    return;
  }

  Serial.println("Streaming test audio. A correctly wired SPK2 should play music.");
}

void loop() {
  audio.loop();
  vTaskDelay(1);
}
