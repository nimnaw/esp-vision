# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

MAIN_PY = """\
import sensor
import time

print("ESP-VISION FREENOVE_ESP32_S3_WROOM ready")

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    # No built-in display; enable EV-MUX with sensor.evmux(True) to stream
    # frames to ESP-VISION host tools.
    time.sleep_ms(100)
"""

README_TXT = """\
ESP-VISION Freenove ESP32-S3-WROOM

Edit main.py to run your Python vision script.
This board exposes the CH343 UART bridge only; the native USB pins are not
usable. EV-MUX starts disabled, so use a serial terminal or mpremote over the
UART REPL. Run sensor.evmux(True) before connecting ESP-VISION host tools.
The default main.py captures frames without a display.
"""
