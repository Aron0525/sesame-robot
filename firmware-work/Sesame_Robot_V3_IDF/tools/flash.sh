#!/usr/bin/env bash
# Stable ESP32-S3 update flow, based on the 2026-08-21 recovery procedure.
# It backs up NVS and updates only the application by default. Pass --nvs to
# deliberately update the device configuration as part of the same operation.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_export="${IDF_EXPORT:-$HOME/.espressif/frameworks/esp-idf-v5.5.4/export.sh}"
baud=115200
nvs_offset=0x9000
nvs_size=0x6000
app_offset=0x10000
nvs_image=""
port=""

usage() {
    cat <<'EOF'
Usage: bash tools/flash.sh [--nvs /private/path/device-nvs.bin] [ESP32-S3 serial port]

Builds the application, backs up the current NVS, then writes and verifies only
the application at 115200 baud. The bootloader, partition table, and speech
model are preserved. --nvs additionally writes and verifies a 24 KiB NVS image
at 0x9000.
EOF
}

if [[ ! -f "$idf_export" ]]; then
    echo "ESP-IDF export script was not found: $idf_export" >&2
    echo "Set IDF_EXPORT to the ESP-IDF export.sh path, then retry." >&2
    exit 1
fi

source "$idf_export" >/dev/null

while (( $# > 0 )); do
    case "$1" in
        --nvs)
            if (( $# < 2 )); then
                echo "--nvs requires a path to a 24 KiB NVS image." >&2
                exit 2
            fi
            nvs_image="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            if [[ -n "$port" ]]; then
                echo "Only one serial port may be supplied." >&2
                usage >&2
                exit 2
            fi
            port="$1"
            shift
            ;;
    esac
done

if [[ -z "$port" ]]; then
    shopt -s nullglob
    candidates=(
        /dev/cu.usbmodem*
        /dev/cu.usbserial*
        /dev/cu.SLAB_USBtoUART*
        /dev/cu.wchusbserial*
        /dev/ttyACM*
        /dev/ttyUSB*
    )
    shopt -u nullglob

    if (( ${#candidates[@]} == 0 )); then
        echo "No ESP32 serial port found." >&2
        echo "Connect the board with a data-capable USB cable and retry, or pass the port explicitly." >&2
        exit 1
    fi
    if (( ${#candidates[@]} > 1 )); then
        echo "More than one serial port was found; pass the ESP32 port explicitly:" >&2
        printf '  %s <port>\n' "$0" >&2
        printf '  %s\n' "${candidates[@]}" >&2
        exit 1
    fi
    port="${candidates[0]}"
fi

if [[ ! -e "$port" ]]; then
    echo "Serial port does not exist: $port" >&2
    exit 1
fi

if [[ -n "$nvs_image" ]]; then
    if [[ ! -f "$nvs_image" ]]; then
        echo "NVS image does not exist: $nvs_image" >&2
        exit 1
    fi
    nvs_image_size="$(wc -c < "$nvs_image" | tr -d '[:space:]')"
    if [[ "$nvs_image_size" != "24576" ]]; then
        echo "NVS image must be exactly 24576 bytes, got ${nvs_image_size}: $nvs_image" >&2
        exit 1
    fi
fi

cd "$project_dir"
idf.py build

umask 077
backup_dir="$project_dir/flash-diagnostics/nvs-backups"
mkdir -p "$backup_dir"
backup_file="$backup_dir/nvs-before-app-flash-$(date +%Y%m%d-%H%M%S).bin"

echo "Backing up NVS to $backup_file"
esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
    --no-stub \
    read_flash "$nvs_offset" "$nvs_size" "$backup_file"
chmod 600 "$backup_file"

echo "Writing application only via $port at ${baud} baud"
esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
    --no-stub \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_freq 80m --flash_size 4MB \
    "$app_offset" build/sesame_robot_v3.bin
esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
    --no-stub \
    verify_flash "$app_offset" build/sesame_robot_v3.bin

if [[ -n "$nvs_image" ]]; then
    echo "Writing supplied NVS image"
    esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
        --no-stub \
        --before default_reset --after hard_reset write_flash \
        "$nvs_offset" "$nvs_image"
    esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
        --no-stub \
        verify_flash "$nvs_offset" "$nvs_image"
fi

echo "Flash verified. Preserved bootloader, partition table, and speech model."
