#!/bin/sh
# Everything that can be verified about the OLED port without an ESP32 compiler.
#
#   ./firmware/host_tests/run_tests.sh
#
# Runs from any directory. Needs only g++ (>= 7) and python3. This does NOT replace a real
# firmware build - see firmware/BUILD_ARDUINO_IDE.md for that - it is the pre-flight check
# that makes the first flash on the bench worth doing.
set -e

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fw=$(dirname -- "$here")
cd "$fw"

src=upstream/esp32_marauder
inc="-Ihost_tests/stubs -I$src"
cxx=${CXX:-g++}
cxxflags="-std=c++17 -Wall -Wextra -g"
out=$(mktemp -d "${TMPDIR:-/tmp}/marauder-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT INT TERM

fail=0
note() { printf '%s\n' "$*"; }

# ---------------------------------------------------------------- pure logic tests
# These need no Arduino stubs: they test the hardware-free math and the log core.
for t in test_core test_logs test_slide; do
  note "--- $t"
  $cxx $cxxflags -I"$src" "$here/$t.cpp" -o "$out/$t"
  "$out/$t" || fail=1
done

# ---------------------------------------------------------------- tests that run the firmware sources
# Compiled against the Arduino stubs in host_tests/stubs, so they execute the same .cpp
# files the ESP32 build compiles (PoomDisplay.cpp, LogStores.cpp), not a copy.
note "--- test_display (real PoomDisplay.cpp + fake I2C bus/panel)"
$cxx $cxxflags $inc "$here/test_display.cpp" "$src/PoomDisplay.cpp" host_tests/stubs/stub_arduino.cpp -o "$out/test_display"
"$out/test_display" || fail=1

note "--- test_logstores (real LogStores.cpp + in-memory SPIFFS/SD)"
$cxx $cxxflags $inc "$here/test_logstores.cpp" "$src/LogStores.cpp" host_tests/stubs/stub_arduino.cpp -o "$out/test_logstores"
"$out/test_logstores" || fail=1

note "--- test_board_config (configs.h + partitions.csv)"
$cxx $cxxflags $inc -DPARTITIONS_CSV='"'"$src/partitions.csv"'"' \
  "$here/test_board_config.cpp" "$src/LogStores.cpp" host_tests/stubs/stub_arduino.cpp -o "$out/test_board_config"
"$out/test_board_config" || fail=1

# ---------------------------------------------------------------- compiler-warning gate
# Our two driver files must build warning-free; on target they are compiled with the same
# strictness plus the ESP32 core headers, which are only additive.
note "--- warning check on the OLED driver and log store"
for f in "$src/PoomDisplay.cpp" "$src/LogStores.cpp"; do
  if $cxx $cxxflags $inc -c "$f" -o "$out/$(basename "$f").o" 2>"$out/warn.txt"; then
    if [ -s "$out/warn.txt" ]; then
      note "WARNINGS in $f:"; sed 's/^/  /' "$out/warn.txt"; fail=1
    fi
  else
    note "COMPILE FAILED for $f"; sed 's/^/  /' "$out/warn.txt"; fail=1
  fi
done

# ---------------------------------------------------------------- static lint
note "--- tools/check_sources.py"
python3 tools/check_sources.py || fail=1

note ""
if [ "$fail" -ne 0 ]; then
  note "RESULT: failures above"
  exit 1
fi
note "RESULT: all host checks passed. Next step is the real build:"
note "  Arduino IDE (esp32 core 3.3.4) + firmware/BUILD_ARDUINO_IDE.md"
exit 0
