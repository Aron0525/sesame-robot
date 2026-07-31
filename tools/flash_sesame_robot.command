#!/bin/zsh
# Double-click launcher for the Sesame Robot ESP32-S3 production firmware.
# It deliberately owns the serial port only for the build/flash interval.

emulate -L zsh
setopt err_return no_unset pipe_fail

SCRIPT_DIR="${0:A:h}"
PROJECT_ROOT="${SCRIPT_DIR:h}"
IDF_PROJECT="${PROJECT_ROOT}/firmware/esp32_voice_idf"
IDF_EXPORT="/Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh"
DEFAULT_PORT="/dev/cu.usbmodem101"
PORT="${SESAME_ESP32_PORT:-${DEFAULT_PORT}}"
BUILD_DIR="build-p1"
GATEWAY_LABEL="com.sesame.voice-gateway"
GATEWAY_DOMAIN="gui/$(id -u)"
GATEWAY_TARGET="${GATEWAY_DOMAIN}/${GATEWAY_LABEL}"
GATEWAY_PLIST="${HOME}/Library/LaunchAgents/${GATEWAY_LABEL}.plist"
DRY_RUN=false
GATEWAY_WAS_LOADED=false
GATEWAY_STOPPED=false

pause_before_exit() {
  if [[ -t 0 ]]; then
    echo
    read -r "?按 Enter 键关闭此窗口..."
  fi
}

restore_gateway() {
  if [[ "${GATEWAY_WAS_LOADED}" != true || "${GATEWAY_STOPPED}" != true ]]; then
    return
  fi
  echo "恢复电脑 Voice Gateway..."
  if launchctl bootstrap "${GATEWAY_DOMAIN}" "${GATEWAY_PLIST}" 2>/dev/null; then
    launchctl kickstart -k "${GATEWAY_TARGET}" 2>/dev/null || true
    echo "Gateway 已重新启动。"
  else
    echo "警告：Gateway 没有自动恢复。请执行："
    echo "launchctl bootstrap ${GATEWAY_DOMAIN} ${GATEWAY_PLIST}"
  fi
}

show_usage() {
  cat <<'EOF'
用法：双击本文件，或在终端执行：
  ./flash_sesame_robot.command

可选：
  SESAME_ESP32_PORT=/dev/cu.usbmodem101 ./flash_sesame_robot.command
  ./flash_sesame_robot.command --dry-run
EOF
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
if [[ ! -e "${PORT}" ]]; then
  # USB device names can change after reconnecting. When there is exactly one
  # plausible serial device, select it rather than making a one-click flash
  # fail because the trailing number changed.
  candidates=(/dev/cu.usbmodem*(N) /dev/cu.SLAB_USBtoUART*(N))
  if (( ${#candidates} == 1 )); then
    PORT="${candidates[1]}"
    echo "串口名称已变化，自动选择：${PORT}"
  else
    echo "错误：未找到 ESP32 串口 ${PORT}。请连接机器人后再运行。"
    echo "可用串口："
    print -l "${candidates[@]}"
    pause_before_exit
    exit 1
  fi
fi

echo "Sesame Robot ESP32-S3 一键烧录"
echo "项目：${IDF_PROJECT}"
echo "串口：${PORT}"

if [[ "${DRY_RUN}" == true ]]; then
  echo "检查通过：未执行构建、烧录或停止 Gateway。"
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

echo "加载 ESP-IDF 环境..."
source "${IDF_EXPORT}" >/dev/null

echo "开始构建并烧录。请不要拔掉 USB，也不要按住 BOOT。"
cd "${IDF_PROJECT}"
idf.py -p "${PORT}" -B "${BUILD_DIR}" build flash

echo "烧录完成：ESP32 已自动重启。"
echo "等待 Gateway 重连后，按 BOOT 一次开始录音、再按一次结束。"
pause_before_exit
