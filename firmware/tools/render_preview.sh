#!/bin/sh
# Build and run the OLED preview renderer, then turn the frames into an animated GIF.
#
#   sh firmware/tools/render_preview.sh [output-dir]
#
# Nothing here touches the board: it compiles the real PoomDisplay.cpp against the host
# stubs (fake I2C bus + panel memory + virtual clock), so the frames are what a 128x64
# SSD1306/SH1106 would actually be shown, including the ~20 fps pacing of the slide.
set -e

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fw=$(dirname -- "$here")
out=${1:-/tmp/oled-preview}
cxx=${CXX:-g++}

mkdir -p "$out"
rm -f "$out"/frame-*.ppm "$out/slide.gif"

"$cxx" -std=c++17 -O1 -Wall -Wextra \
  -I"$fw/host_tests/stubs" -I"$fw/upstream/esp32_marauder" \
  "$here/render_preview.cpp" "$fw/upstream/esp32_marauder/PoomDisplay.cpp" \
  "$fw/host_tests/stubs/stub_arduino.cpp" -o "$out/render_preview"

"$out/render_preview" "$out"

if command -v convert >/dev/null 2>&1; then
  # 50 ms per frame == the driver's own slide pacing, so the GIF runs at animation speed.
  convert -delay 5 -loop 0 "$out"/frame-*.ppm "$out/slide.gif"
  echo "gif: $out/slide.gif"
else
  echo "(ImageMagick not found - frames are in $out as PPM files)"
fi
