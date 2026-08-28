#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cli_config="$project_root/arduino-cli.yaml"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. On macOS: brew install arduino-cli" >&2
  exit 1
fi

cd "$project_root"
arduino-cli core update-index --config-file "$cli_config"
arduino-cli core install esp32:esp32@3.0.7 --config-file "$cli_config"
arduino-cli lib install "lvgl@8.3.10" --config-file "$cli_config"
arduino-cli lib install "ESP32_IO_Expander@0.0.4" --config-file "$cli_config"
arduino-cli lib install "ESP32_Display_Panel@0.1.8" --config-file "$cli_config"

echo "Project dependencies are installed under $project_root/.arduino"
