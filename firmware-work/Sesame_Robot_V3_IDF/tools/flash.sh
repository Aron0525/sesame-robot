#!/usr/bin/env bash
# Stable ESP32-S3 update flow, based on the 2026-08-21 recovery procedure.
# It backs up NVS and updates only the application by default. Pass --nvs to
# deliberately update the device configuration as part of the same operation.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_export="${IDF_EXPORT:-$HOME/.espressif/frameworks/esp-idf-v5.5.4/export.sh}"
baud=115200
build_dir="${SESAME_BUILD_DIR:-build-codex-2}"
nvs_offset=0x9000
nvs_size=0x6000
app_offset=0x10000
nvs_image=""
port=""
flash_chunk_bytes="${SESAME_FLASH_CHUNK_BYTES:-16384}"
flash_max_retries="${SESAME_FLASH_MAX_RETRIES:-5}"
flash_chunk_file=""
flash_detail_log=""
flash_bootloader_ready=false

usage() {
    cat <<'EOF'
Usage: bash tools/flash.sh [--nvs /private/path/device-nvs.bin] [ESP32-S3 serial port]

Builds the application, backs up the current NVS, then writes and verifies only
the application at 115200 baud. The bootloader, partition table, and speech
model are preserved. --nvs additionally writes and verifies a 24 KiB NVS image
at 0x9000.
EOF
}

cleanup_flash_temp() {
    if [[ -n "$flash_chunk_file" && -f "$flash_chunk_file" ]]; then
        rm -f -- "$flash_chunk_file"
        flash_chunk_file=""
    fi
}

wait_for_download_port() {
    local attempt
    local -a candidates

    for ((attempt = 1; attempt <= 100; attempt++)); do
        if [[ -e "$port" ]]; then
            return 0
        fi

        shopt -s nullglob
        candidates=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*)
        shopt -u nullglob
        if (( ${#candidates[@]} == 1 )); then
            echo "USB serial re-enumerated: $port -> ${candidates[0]}"
            port="${candidates[0]}"
            return 0
        fi
        sleep 0.1
    done
    return 1
}

recover_download_mode() {
    local attempt

    sleep 1
    for ((attempt = 1; attempt <= 8; attempt++)); do
        if wait_for_download_port && esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
            --before usb_reset --after no_reset --no-stub chip_id >/dev/null 2>&1; then
            flash_bootloader_ready=true
            return 0
        fi
        sleep 0.5
    done

    echo "Unable to recover ESP32-S3 download mode. Hold BOOT, tap RESET/EN, then retry." >&2
    return 1
}

flash_verified_unit() {
    local address="$1"
    local image_file="$2"
    local after_reset="${3:-no_reset}"
    local attempt reset_mode rc=1

    if [[ ! -s "$image_file" ]]; then
        echo "Flash image does not exist or is empty: $image_file" >&2
        return 1
    fi

    for ((attempt = 1; attempt <= flash_max_retries; attempt++)); do
        if ! wait_for_download_port; then
            echo "Timed out waiting for the ESP32-S3 serial port." >&2
            return 1
        fi

        reset_mode=usb_reset
        if [[ "$flash_bootloader_ready" == true ]]; then
            reset_mode=no_reset
        fi

        if esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
            --before "$reset_mode" --after "$after_reset" --no-stub \
            write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB \
            --no-compress --verify "$address" "$image_file" >>"$flash_detail_log" 2>&1; then
            flash_bootloader_ready=true
            return 0
        else
            rc=$?
        fi

        flash_bootloader_ready=false
        if ((attempt < flash_max_retries)); then
            echo "USB disconnected while writing $address; recovering download mode (${attempt}/${flash_max_retries})..."
            if ! recover_download_mode; then
                return "$rc"
            fi
        fi
    done

    echo "Writing $address failed ${flash_max_retries} times. Recent esptool output:" >&2
    tail -n 35 "$flash_detail_log" >&2
    return "$rc"
}

flash_image_in_verified_chunks() {
    local image_file="$1"
    local base_address="$2"
    local image_size total_chunks index address address_hex after_reset

    image_size="$(stat -f %z "$image_file")"
    total_chunks=$(((image_size + flash_chunk_bytes - 1) / flash_chunk_bytes))
    flash_chunk_file="$(mktemp -t sesame-flash-chunk.XXXXXX)"

    echo "Writing application in $total_chunks verified chunks of $flash_chunk_bytes bytes."
    for ((index = 0; index < total_chunks; index++)); do
        dd if="$image_file" of="$flash_chunk_file" bs="$flash_chunk_bytes" skip="$index" count=1 status=none
        address=$((base_address + index * flash_chunk_bytes))
        printf -v address_hex '0x%x' "$address"
        after_reset=no_reset
        if ((index + 1 == total_chunks)); then
            after_reset=hard_reset
        fi
        flash_verified_unit "$address_hex" "$flash_chunk_file" "$after_reset"
        if ((index == 0 || (index + 1) % 5 == 0 || index + 1 == total_chunks)); then
            echo "  application verified: $((index + 1))/$total_chunks"
        fi
    done
    cleanup_flash_temp
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

if [[ ! "$flash_chunk_bytes" =~ ^[1-9][0-9]*$ || ! "$flash_max_retries" =~ ^[1-9][0-9]*$ ]]; then
    echo "SESAME_FLASH_CHUNK_BYTES and SESAME_FLASH_MAX_RETRIES must be positive integers." >&2
    exit 2
fi

cd "$project_dir"
idf.py -B "$build_dir" build

umask 077
backup_dir="${SESAME_FLASH_BACKUP_DIR:-$project_dir/flash-diagnostics/nvs-backups}"
mkdir -p "$backup_dir"
backup_file="$backup_dir/nvs-before-app-flash-$(date +%Y%m%d-%H%M%S).bin"

echo "Backing up NVS to $backup_file"
esptool.py --chip esp32s3 --port "$port" --baud "$baud" \
    --after no_reset --no-stub \
    read_flash "$nvs_offset" "$nvs_size" "$backup_file"
chmod 600 "$backup_file"
flash_bootloader_ready=true
flash_detail_log="$build_dir/segmented-app-flash.log"
: >"$flash_detail_log"
trap cleanup_flash_temp EXIT INT TERM

if [[ -n "$nvs_image" ]]; then
    echo "Writing supplied NVS image"
    flash_verified_unit "$nvs_offset" "$nvs_image"
fi

flash_image_in_verified_chunks "$build_dir/sesame_robot_v3.bin" "$app_offset"

echo "Flash verified. Preserved bootloader, partition table, and speech model. Details: $flash_detail_log"
