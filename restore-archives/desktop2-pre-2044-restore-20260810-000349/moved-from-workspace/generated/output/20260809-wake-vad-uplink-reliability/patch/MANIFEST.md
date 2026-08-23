# Patch manifest

Textual source changes are in `wake-vad-uplink-reliability.patch`. The TFLite model and flash images are binary; their exact SHA-256 values are in `../modified/manifest-sha256.txt`.

Focused behavior changes:
- no speech returns the device to idle without TTS;
- initial speech requires 10 continuous 20-ms frames (200 ms);
- encoded uplink uses a bounded worker queue so socket writes do not block capture;
- listen telemetry carries the device trigger, stop reason, and sent-frame count.
