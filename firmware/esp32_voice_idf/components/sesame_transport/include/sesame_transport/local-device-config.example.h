#pragma once

// Copy this file to local-device-config.h in the same directory. That file is
// ignored by Git and is compiled into a device-specific firmware image.
// Do not put real credentials into this example file.

#define SESAME_LOCAL_WIFI_SSID "YOUR_WIFI_SSID"
#define SESAME_LOCAL_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define SESAME_LOCAL_DEVICE_ID "dev_001"
#define SESAME_LOCAL_GATEWAY_ID "gw_local_dev"
#define SESAME_LOCAL_DEVICE_TOKEN "YOUR_DEVICE_TOKEN"

// Optional compatibility URL for the ESP32's HTTP control page. Keep this
// empty in multi-device deployments to avoid a shared Wi-Fi name collision.
// A unique DeviceID-based page is always available as http://<device-id>.local/.
// The local hostname converts '_' in the Device ID to '-'. Keep IDs to 1-63
// ASCII letters, digits, hyphens, or underscores, and do not use both forms.
#define SESAME_LOCAL_WEB_CONTROL_HOSTNAME ""

// Paste the PEM exactly, including the BEGIN/END lines.
#define SESAME_LOCAL_ROOT_CA R"PEM(-----BEGIN CERTIFICATE-----
YOUR_GATEWAY_ROOT_CA
-----END CERTIFICATE-----
)PEM"
