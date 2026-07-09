The following firmware is applicable to the Freenove ESP32-S3-WROOM development
board based on ESP32-S3.

This board must be equipped with 16 MiB external SPI Flash and 8 MiB PSRAM. The
default configuration enables the ESP32-S3 target, the ESP-VISION 16 MiB flash
layout, GC2145 camera support and the SD card.

The board reaches the host only through its CH343 UART bridge; the ESP32-S3
native USB pins are not usable, so the compiled TinyUSB CDC/MSC device cannot
be enumerated. EV-MUX is disabled at boot to keep the UART REPL available for
Thonny and mpremote. Run ``sensor.evmux(True)`` to enable EV-MUX over UART
before connecting ESP-VISION host tools such as Device Core.
