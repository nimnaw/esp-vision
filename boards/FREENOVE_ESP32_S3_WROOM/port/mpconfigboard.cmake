set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/FREENOVE_ESP32_S3_WROOM/sdkconfig.s3_freenove
    boards/FREENOVE_ESP32_S3_WROOM/sdkconfig.board
)

set(MICROPY_PY_BTREE OFF)
