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
    # No built-in display — frames are accessible via ESP-VISION host tools.
    time.sleep_ms(100)
"""

README_TXT = """\
ESP-VISION Freenove ESP32-S3-WROOM

Edit main.py to run your Python vision script.
Use the ESP-VISION VSCode extension to run scripts and preview frames.
The default main.py captures frames without a display — preview over USB.
"""
