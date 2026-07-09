# Changelog

Board-scoped changelog for the FREENOVE ESP32-S3-WROOM board. This file tracks changes specific to this board; platform-wide changes stay in the repository-root `CHANGELOG.md`. The format follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- New board `FREENOVE_ESP32_S3_WROOM` (ESP32-S3, 16 MiB flash, 8 MiB OPI PSRAM, no built-in display): GC2145 DVP camera through the esp32-camera backend at QQVGA/QVGA, SD card via SDMMC slot 0, and Flash MSC with the shared 16 MiB ESP-VISION partition layout.
- GRAYSCALE capture support in the board camera backend: RGB565 frames from the esp32-camera driver are converted to 8-bit luma when `sensor` is in grayscale mode.

### Fixed

- RGB565 capture validation now checks the delivered frame's format, dimensions (against the configured output) and byte length (width × height × 2) before copying, so padded or malformed driver frames are rejected with a diagnostic instead of copied silently.
- GRAYSCALE captures no longer trigger a Guru Meditation (LoadStoreError): the luma lookup tables were `IRAM_ATTR`, and on ESP32-S3 const data placed in the IRAM window is not data-bus-readable. The tables now live in flash rodata.
- Boot no longer hangs in the first camera frame wait (`sensor.skip_frames()` / `sensor.snapshot()`): with power management enabled (`CONFIG_PM_ENABLE=y`) the CPU light-sleeps while the MicroPython task blocks on the frame queue, the DVP frame-ready interrupt is not a light-sleep wake source, and the frozen FreeRTOS tick keeps the frame-wait timeout from ever firing. Power management is now disabled for this camera board in `sdkconfig.s3_freenove`.

### Changed

- Reworked `esp_vision_camera_capture` for the two supported output formats (RGB565 and GRAYSCALE): removed the unreachable JPEG decode path (the driver always delivers RGB565), and replaced the per-pixel grayscale shift/multiply math with lookup tables (`(r*616 + g*600 + b*232) >> 8`) in flash rodata so each pixel is three table loads and two adds. The RGB565 byte-swap path now processes two pixels per 32-bit word.
- Raised the FreeRTOS tick rate to 1000 Hz (`CONFIG_FREERTOS_HZ=1000`) to match the other ESP32-S3 boards and avoid MicroPython task starvation at boot.
