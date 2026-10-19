#!/usr/bin/env bash

# ESP32-S3 一键编译与烧录快捷脚本
# 用法:
#   ./run.sh uart     - 编译、烧录并监视 00_hello_world_uart (CH343 硬件串口)
#   ./run.sh usb      - 编译、烧录并监视 01_hello_world_usb  (原生 USB 端口)
#   ./run.sh monitor  - 直接自动查找并打开串口监视器
#   ./run.sh list     - 列出当前 Mac 连接的所有串口设备

set -e

# 确保能找到 pio 命令
export PATH="$PATH:$HOME/.platformio/penv/bin"

TARGET=${1:-help}
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$TARGET" in
  uart|hello|00)
    echo "=========================================="
    echo "🚀 正在编译并烧录: 00_hello_world_uart (CH343 硬件串口)..."
    echo "=========================================="
    cd "$WORKSPACE_ROOT/00_hello_world_uart"
    pio run -t upload
    echo "💡 烧录完成，正在打开串口监视 (按 Ctrl+C 退出)..."
    pio device monitor
    ;;

  usb|01)
    echo "=========================================="
    echo "🚀 正在编译并烧录: 01_hello_world_usb (原生 USB 端口)..."
    echo "=========================================="
    cd "$WORKSPACE_ROOT/01_hello_world_usb"
    pio run -t upload
    echo "💡 烧录完成，正在打开串口监视 (按 Ctrl+C 退出)..."
    pio device monitor
    ;;

  monitor|log)
    echo "=========================================="
    echo "🔌 正在自动启动串口监视器 (115200)..."
    echo "=========================================="
    pio device monitor -b 115200
    ;;

  list|ports)
    echo "=========================================="
    echo "📋 当前 Mac 检测到的串口设备:"
    echo "=========================================="
    ls -l /dev/cu.usbmodem* /dev/cu.wch* 2>/dev/null || echo "未找到常见单片机串口设备，请检查 USB 连接。"
    ;;

  *)
    echo "ESP32-S3 便捷开发助手"
    echo "用法:"
    echo "  ./run.sh uart     - 编译、烧录并监听 00_hello_world_uart (CH343 串口)"
    echo "  ./run.sh usb      - 编译、烧录并监听 01_hello_world_usb  (原生 USB 端口)"
    echo "  ./run.sh monitor  - 快速打开 115200 串口监视器"
    echo "  ./run.sh list     - 检查当前连接的端口名称"
    ;;
esac
