#!/bin/zsh
# Double-click launcher for the Sesame Robot ESP32-S3 production firmware.
# It deliberately owns the serial port only for the build/flash interval.

emulate -L zsh
setopt err_return no_unset pipe_fail

SCRIPT_DIR="${0:A:h}"
PROJECT_ROOT="${SCRIPT_DIR:h}"
IDF_PROJECT="${PROJECT_ROOT}/firmware/esp32_voice_idf"
IDF_EXPORT="/Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh"
PORT="${SESAME_ESP32_PORT:-}"
BUILD_DIR="build-p1"
GATEWAY_LABEL="com.sesame.voice-gateway"
GATEWAY_DOMAIN="gui/$(id -u)"
GATEWAY_TARGET="${GATEWAY_DOMAIN}/${GATEWAY_LABEL}"
GATEWAY_PLIST="${HOME}/Library/LaunchAgents/${GATEWAY_LABEL}.plist"
DRY_RUN=false
GATEWAY_WAS_LOADED=false
GATEWAY_STOPPED=false
SERIAL_CANDIDATES=()

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

show_usage() {
  cat <<'EOF'
用法：双击本文件，或在终端执行：
  ./flash_sesame_robot.command

可选：
  SESAME_ESP32_PORT=/dev/cu.usbmodemXXXX ./flash_sesame_robot.command
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

select_esp32_port() {
  local candidate
  local -a candidates matches

  if [[ -n "${PORT}" ]]; then
    if [[ ! -e "${PORT}" ]]; then
      echo "错误：指定的串口不存在：${PORT}" >&2
      return 1
    fi
    if ! is_esp32s3 "${PORT}"; then
      echo "错误：${PORT} 不是可连接的 ESP32-S3 下载端口。" >&2
      return 1
    fi
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

  echo "错误：发现了串口，但没有任何一个响应为 ESP32-S3 下载端口：" >&2
  print -l -- "${candidates[@]}" >&2
  echo "请确认 USB 线支持数据；如有需要，按住 BOOT 后短按 RESET/EN 再重试。" >&2
  return 1
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
  echo "检查完成：未执行芯片探测、构建、烧录或停止 Gateway。"
  pause_before_exit
  exit 0
fi

if launchctl print "${GATEWAY_TARGET}" >/dev/null 2>&1; then
  GATEWAY_WAS_LOADED=true
  echo "暂时停止 Gateway，释放 ESP32 串口..."
  launchctl bootout "${GATEWAY_TARGET}"
  GATEWAY_STOPPED=true
  trap restore_gateway EXIT INT TERM
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

echo "开始构建并以稳定的 115200 波特率烧录。请不要拔掉 USB，也不要按住 BOOT。"
cd "${IDF_PROJECT}"
# The ESP32-S3 USB-Serial/JTAG interface on this Mac disconnects during the
# 460800-baud switch, so retain the verified 115200-baud rate for flashing.
idf.py -p "${PORT}" -b 115200 -B "${BUILD_DIR}" build flash

echo "烧录完成：ESP32 已自动重启。"
echo "等待 Gateway 重连后，按 BOOT 一次开始录音、再按一次结束。"
# Restore before the terminal's optional Enter prompt, rather than holding the
# local web console offline while the user reads this success message.
restore_gateway
pause_before_exit
