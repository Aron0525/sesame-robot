#!/bin/zsh
# Double-click launcher for the Sesame Robot ESP32-S3 production firmware.
# It deliberately owns the serial port only for the build/flash interval.

emulate -L zsh
setopt err_return no_unset pipe_fail

SCRIPT_DIR="${0:A:h}"
PROJECT_ROOT="${SCRIPT_DIR:h}"
IDF_PROJECT="${SESAME_IDF_PROJECT:-${PROJECT_ROOT}/firmware/esp32_voice_idf}"
IDF_EXPORT="/Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh"
PORT="${SESAME_ESP32_PORT:-}"
BUILD_DIR="${SESAME_BUILD_DIR:-build-p1}"
APP_IMAGE="${SESAME_APP_IMAGE:-sesame_robot_v3.bin}"
GATEWAY_LABEL="com.sesame.voice-gateway"
GATEWAY_DOMAIN="gui/$(id -u)"
GATEWAY_TARGET="${GATEWAY_DOMAIN}/${GATEWAY_LABEL}"
GATEWAY_PLIST="${HOME}/Library/LaunchAgents/${GATEWAY_LABEL}.plist"
DRY_RUN=false
GATEWAY_WAS_LOADED=false
GATEWAY_STOPPED=false
SERIAL_CANDIDATES=()
FLASH_CHUNK_BYTES="${SESAME_FLASH_CHUNK_BYTES:-16384}"
FLASH_MAX_RETRIES="${SESAME_FLASH_MAX_RETRIES:-5}"
FLASH_CHUNK_FILE=""
FLASH_DETAIL_LOG=""
FLASH_VERIFIED_UNITS=0
FLASH_RETRY_COUNT=0

pause_before_exit() {
  # The app bundle keeps this terminal open after flashing.  Restoring only in
  # the EXIT trap meant the Gateway stayed offline until the user pressed
  # Enter, even though the serial port was already free.
  restore_gateway
  if [[ -t 0 ]]; then
    echo
    read -r "?按 Enter 键关闭此窗口..."
  fi
}

cleanup_orphaned_gateway_workers() {
  local worker_pid attempt
  local worker_pattern="${PROJECT_ROOT}/gateway/.venv/bin/sesame-voice-gateway"
  local worker_pids

  # launchctl manages the canonical instance. Detached workers can continue
  # advertising mDNS after losing TCP/8765, so an ESP32 may select a dead peer.
  worker_pids="$(pgrep -f -- "${worker_pattern}" 2>/dev/null || true)"
  if [[ -n "${worker_pids}" ]]; then
    while IFS= read -r worker_pid; do
      [[ -n "${worker_pid}" ]] || continue
      echo "停止遗留 Gateway worker（PID ${worker_pid}）..."
      kill -TERM "${worker_pid}" 2>/dev/null || true
    done <<< "${worker_pids}"
  fi
  for attempt in {1..10}; do
    if ! pgrep -f -- "${worker_pattern}" >/dev/null 2>&1; then
      return
    fi
    sleep 0.2
  done

  # A worker that ignores TERM can keep advertising mDNS even though the
  # replacement Gateway owns TCP/8765.  Force it down before bootstrapping the
  # one canonical LaunchAgent instance.
  worker_pids="$(pgrep -f -- "${worker_pattern}" 2>/dev/null || true)"
  if [[ -n "${worker_pids}" ]]; then
    while IFS= read -r worker_pid; do
      [[ -n "${worker_pid}" ]] || continue
      echo "强制停止遗留 Gateway worker（PID ${worker_pid}）..."
      kill -KILL "${worker_pid}" 2>/dev/null || true
    done <<< "${worker_pids}"
    sleep 0.2
  fi
}

restore_gateway() {
  local attempt

  if [[ "${GATEWAY_WAS_LOADED}" != true || "${GATEWAY_STOPPED}" != true ]]; then
    return
  fi
  echo "恢复电脑 Voice Gateway..."
  cleanup_orphaned_gateway_workers
  for attempt in {1..5}; do
    if launchctl bootstrap "${GATEWAY_DOMAIN}" "${GATEWAY_PLIST}" 2>/dev/null; then
      launchctl kickstart -k "${GATEWAY_TARGET}" 2>/dev/null || true
      GATEWAY_STOPPED=false
      echo "Gateway 已重新启动。"
      return
    fi
    sleep 1
  done
  echo "警告：Gateway 没有自动恢复。请执行："
  echo "launchctl bootstrap ${GATEWAY_DOMAIN} ${GATEWAY_PLIST}"
}

cleanup_flash_temp() {
  if [[ -n "${FLASH_CHUNK_FILE}" && -f "${FLASH_CHUNK_FILE}" ]]; then
    rm -f -- "${FLASH_CHUNK_FILE}"
    FLASH_CHUNK_FILE=""
  fi
}

cleanup() {
  cleanup_flash_temp
  restore_gateway
}

show_usage() {
  cat <<'EOF'
用法：双击本文件，或在终端执行：
  ./flash_sesame_robot.command

可选：
  SESAME_ESP32_PORT=/dev/cu.usbmodemXXXX ./flash_sesame_robot.command
  SESAME_IDF_PROJECT=/path/to/idf-project SESAME_BUILD_DIR=build \\
    SESAME_APP_IMAGE=app.bin ./flash_sesame_robot.command
  ./flash_sesame_robot.command --dry-run
EOF
}

refresh_serial_candidates() {
  local candidate port_name

  SERIAL_CANDIDATES=(/dev/cu.usbmodem*(N) /dev/cu.SLAB_USBtoUART*(N) \
    /dev/cu.wchusbserial*(N) /dev/cu.usbserial*(N))

  # Probe every other macOS serial endpoint too: USB bridge chip names and
  # trailing port numbers vary across boards and reconnects.
  for candidate in /dev/cu.*(N); do
    port_name="${candidate##*/}"
    case "${port_name}" in
      cu.Bluetooth-Incoming-Port|cu.debug-console)
        continue
        ;;
    esac
    if [[ " ${SERIAL_CANDIDATES[*]} " != *" ${candidate} "* ]]; then
      SERIAL_CANDIDATES+=("${candidate}")
    fi
  done
}

find_serial_candidates() {
  refresh_serial_candidates
  print -l -- "${SERIAL_CANDIDATES[@]}"
}

is_esp32s3() {
  local candidate="$1"
  local probe_output

  if ! probe_output="$(esptool.py --chip esp32s3 --port "${candidate}" --baud 115200 chip_id 2>&1)"; then
    return 1
  fi
  [[ "${probe_output}" == *"ESP32-S3"* ]]
}

retry_manual_download_mode() {
  local candidate="$1"

  if [[ ! -t 0 ]]; then
    return 1
  fi

  echo
  echo "检测到唯一的 USB 串口：${candidate}，但它没有自动进入下载模式。"
  echo "请按以下顺序操作："
  echo "  1. 按住开发板 BOOT；"
  echo "  2. 短按并松开 RESET/EN；"
  echo "  3. 松开 BOOT，等待约 1 秒；"
  read -r "?完成后按 Enter，让脚本重新探测下载模式..."
  is_esp32s3 "${candidate}"
}

select_esp32_port() {
  local candidate
  local -a candidates matches

  if [[ -n "${PORT}" ]]; then
    if [[ ! -e "${PORT}" ]]; then
      echo "错误：指定的串口不存在：${PORT}" >&2
      return 1
    fi
    if ! is_esp32s3 "${PORT}" && ! retry_manual_download_mode "${PORT}"; then
      echo "错误：${PORT} 没有响应 ESP32-S3 下载模式。" >&2
      return 1
    fi
    echo "已实时识别 ESP32-S3 串口：${PORT}"
    return 0
  fi

  refresh_serial_candidates
  candidates=("${SERIAL_CANDIDATES[@]}")
  if (( ${#candidates} == 0 )); then
    echo "错误：没有发现 USB 串口设备。请连接 ESP32-S3 后重试。" >&2
    return 1
  fi

  for candidate in "${candidates[@]}"; do
    if is_esp32s3 "${candidate}"; then
      matches+=("${candidate}")
    fi
  done

  if (( ${#matches} == 1 )); then
    PORT="${matches[1]}"
    echo "已实时识别 ESP32-S3 串口：${PORT}"
    return 0
  fi
  if (( ${#matches} > 1 )); then
    echo "错误：识别到多个 ESP32-S3，请指定要烧录的端口：" >&2
    print -l -- "${matches[@]}" >&2
    return 1
  fi

  if (( ${#candidates} == 1 )) && retry_manual_download_mode "${candidates[1]}"; then
    PORT="${candidates[1]}"
    echo "已通过手动下载模式识别 ESP32-S3 串口：${PORT}"
    return 0
  fi

  echo "错误：发现了串口，但没有任何一个响应为 ESP32-S3 下载模式：" >&2
  print -l -- "${candidates[@]}" >&2
  echo "请确认 USB 线支持数据，并按住 BOOT 后短按 RESET/EN 再重试。" >&2
  return 1
}

wait_for_download_port() {
  local attempt
  local -a available_ports

  for (( attempt = 1; attempt <= 100; attempt++ )); do
    if [[ -e "${PORT}" ]]; then
      return 0
    fi

    # Native ESP32-S3 USB-Serial/JTAG can disappear briefly and return under a
    # different suffix. Only switch automatically when exactly one USB serial
    # endpoint is present, so another board is never selected by accident.
    available_ports=(/dev/cu.usbmodem*(N) /dev/cu.SLAB_USBtoUART*(N) \
      /dev/cu.wchusbserial*(N) /dev/cu.usbserial*(N))
    if (( ${#available_ports} == 1 )); then
      if [[ "${PORT}" != "${available_ports[1]}" ]]; then
        echo "USB 串口重新枚举：${PORT} -> ${available_ports[1]}"
      fi
      PORT="${available_ports[1]}"
      return 0
    fi
    sleep 0.1
  done
  return 1
}

flash_verified_unit() {
  local address="$1"
  local file="$2"
  local after_reset="${3:-no_reset}"
  local attempt rc=1

  if [[ ! -s "${file}" ]]; then
    echo "错误：待烧录文件不存在或为空：${file}" >&2
    return 1
  fi

  for (( attempt = 1; attempt <= FLASH_MAX_RETRIES; attempt++ )); do
    if ! wait_for_download_port; then
      echo "错误：等待 ESP32-S3 USB 串口重新出现超时。" >&2
      return 1
    fi

    print -r -- "BEGIN address=${address} file=${file} attempt=${attempt}" \
      >> "${FLASH_DETAIL_LOG}"
    if esptool.py --chip esp32s3 --port "${PORT}" --baud 115200 \
      --before default_reset --after "${after_reset}" \
      write_flash --flash_mode keep --flash_freq keep --flash_size keep \
      --no-compress --verify "${address}" "${file}" \
      >> "${FLASH_DETAIL_LOG}" 2>&1; then
      print -r -- "PASS address=${address} attempt=${attempt}" \
        >> "${FLASH_DETAIL_LOG}"
      FLASH_VERIFIED_UNITS=$(( FLASH_VERIFIED_UNITS + 1 ))
      return 0
    else
      rc=$?
    fi

    FLASH_RETRY_COUNT=$(( FLASH_RETRY_COUNT + 1 ))
    print -r -- "RETRY address=${address} attempt=${attempt} rc=${rc}" \
      >> "${FLASH_DETAIL_LOG}"
    if (( attempt < FLASH_MAX_RETRIES )); then
      echo "USB 连接在 ${address} 处中断；重连后重试当前块（${attempt}/${FLASH_MAX_RETRIES}）..."
      sleep 0.4
    fi
  done

  echo "错误：地址 ${address} 连续 ${FLASH_MAX_RETRIES} 次写入失败。" >&2
  tail -n 35 "${FLASH_DETAIL_LOG}" >&2
  return "${rc}"
}

flash_app_in_verified_chunks() {
  local app_file="$1"
  local base_address="$2"
  local app_size total_chunks index address address_hex

  app_size="$(stat -f %z "${app_file}")"
  total_chunks=$(( (app_size + FLASH_CHUNK_BYTES - 1) / FLASH_CHUNK_BYTES ))
  FLASH_CHUNK_FILE="$(mktemp -t sesame-flash-chunk.XXXXXX)"

  echo "分块写入主程序：${total_chunks} 块，每块 ${FLASH_CHUNK_BYTES} bytes。"
  index=0
  while (( index < total_chunks )); do
    dd if="${app_file}" of="${FLASH_CHUNK_FILE}" bs="${FLASH_CHUNK_BYTES}" \
      skip="${index}" count=1 status=none
    address=$(( base_address + index * FLASH_CHUNK_BYTES ))
    printf -v address_hex '0x%x' "${address}"
    flash_verified_unit "${address_hex}" "${FLASH_CHUNK_FILE}"
    index=$(( index + 1 ))
    if (( index == 1 || index % 10 == 0 || index == total_chunks )); then
      echo "  主程序已写入并校验：${index}/${total_chunks}"
    fi
  done
  cleanup_flash_temp
}

flash_built_images_resiliently() {
  local build_root="${IDF_PROJECT}/${BUILD_DIR}"

  FLASH_DETAIL_LOG="${build_root}/segmented-flash.log"
  : > "${FLASH_DETAIL_LOG}"
  FLASH_VERIFIED_UNITS=0
  FLASH_RETRY_COUNT=0

  echo "[1/4] 写入并校验 Bootloader..."
  flash_verified_unit 0x0 "${build_root}/bootloader/bootloader.bin"
  echo "[2/4] 写入并校验分区表..."
  flash_verified_unit 0x8000 "${build_root}/partition_table/partition-table.bin"
  echo "[3/4] 写入并校验主程序..."
  flash_app_in_verified_chunks "${build_root}/${APP_IMAGE}" 0x10000
  echo "[4/4] 写入并校验模型分区，然后重启..."
  flash_verified_unit 0x410000 "${build_root}/srmodels/srmodels.bin" hard_reset

  echo "分块烧录校验完成：${FLASH_VERIFIED_UNITS} 个单元，USB 重试 ${FLASH_RETRY_COUNT} 次。"
  echo "详细日志：${FLASH_DETAIL_LOG}"
}

if (( $# > 1 )) || [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  show_usage
  pause_before_exit
  exit 0
fi
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=true
elif (( $# == 1 )); then
  echo "未知参数：$1"
  show_usage
  pause_before_exit
  exit 2
fi

if [[ ! -d "${IDF_PROJECT}" || ! -f "${IDF_EXPORT}" ]]; then
  echo "错误：未找到 ESP-IDF 项目或 ESP-IDF 5.5.4 环境。"
  pause_before_exit
  exit 1
fi
echo "Sesame Robot ESP32-S3 一键烧录"
echo "项目：${IDF_PROJECT}"

if [[ "${DRY_RUN}" == true ]]; then
  echo "发现的候选串口："
  find_serial_candidates
  echo "正式烧录模式：${FLASH_CHUNK_BYTES}-byte 分块校验，单块最多重试 ${FLASH_MAX_RETRIES} 次。"
  echo "检查完成：未执行芯片探测、构建、烧录或停止 Gateway。"
  pause_before_exit
  exit 0
fi

if launchctl print "${GATEWAY_TARGET}" >/dev/null 2>&1; then
  GATEWAY_WAS_LOADED=true
  echo "暂时停止 Gateway，释放 ESP32 串口..."
  launchctl bootout "${GATEWAY_TARGET}"
  GATEWAY_STOPPED=true
  trap cleanup EXIT INT TERM
fi

echo "加载 ESP-IDF 环境..."
source "${IDF_EXPORT}" >/dev/null

if ! select_esp32_port; then
  pause_before_exit
  exit 1
fi

for attempt in {1..10}; do
  if ! lsof "${PORT}" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
if lsof "${PORT}" >/dev/null 2>&1; then
  echo "错误：串口仍被其他程序占用："
  lsof "${PORT}" || true
  echo "请关闭 Arduino Serial Monitor、idf.py monitor 或其他串口工具后重试。"
  pause_before_exit
  exit 1
fi

if [[ "${FLASH_CHUNK_BYTES}" != <1-> || "${FLASH_MAX_RETRIES}" != <1-> ]]; then
  echo "错误：分块大小和重试次数必须是正整数。" >&2
  pause_before_exit
  exit 1
fi

echo "开始构建。请不要拔掉 USB，也不要按住 BOOT。"
cd "${IDF_PROJECT}"
# A single 1.5 MB transfer repeatedly loses this board's native USB connection.
# Build first, then use short verified writes so only the interrupted block is
# retried after USB-Serial/JTAG re-enumerates.
idf.py -B "${BUILD_DIR}" build
flash_built_images_resiliently

echo "烧录完成：ESP32 已自动重启。"
echo "等待 Gateway 重连。按 BOOT 一次开始录音，再按一次结束。"
# Restore before the terminal's optional Enter prompt, rather than holding the
# local web console offline while the user reads this success message.
restore_gateway
pause_before_exit
