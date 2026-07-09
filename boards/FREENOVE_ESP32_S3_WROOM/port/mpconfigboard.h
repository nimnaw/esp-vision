#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME "Freenove ESP32-S3-WROOM"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME "ESP32S3"
#endif

#define MICROPY_HW_USB_MANUFACTURER_STRING "ESP-VISION"
#define MICROPY_HW_USB_PRODUCT_FS_STRING "Freenove ESP32-S3-WROOM MicroPython"
#define MICROPY_HW_ENABLE_USBDEV (1)
#define MICROPY_HW_USB_CDC (1)
#define MICROPY_HW_ESP_USB_SERIAL_JTAG (0)
#define MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE (0)
#define MICROPY_HW_ENABLE_UART_REPL (1)

/* This board's only USB connector goes through the CH343 UART bridge; the
 * native USB pins are unusable (GPIO19 is wired to Freenove's external-power
 * ADC divider), so EV-MUX can never reach a host. Disable its boot default to
 * restore the classic UART REPL for Thonny/mpremote workflows.
 * (sensor.evmux() can still turn the mux on at runtime.) */
#ifndef ESP_VISION_EV_MUX_DEFAULT_ENABLED
#define ESP_VISION_EV_MUX_DEFAULT_ENABLED (0)
#endif

#define MICROPY_HW_USB_MSC (1)
#define MICROPY_HW_USB_MSC_INTERFACE_STRING "Freenove S3-WROOM Flash"
#define MICROPY_HW_USB_MSC_INQUIRY_VENDOR_STRING "ESPVIS"
#define MICROPY_HW_USB_MSC_INQUIRY_PRODUCT_STRING "Freenove Flash"
#define MICROPY_HW_USB_MSC_INQUIRY_REVISION_STRING "1.00"

#define MICROPY_PY_ESPNOW (0)
#define MICROPY_HW_ENABLE_SDCARD (1)
#define MICROPY_PY_BLUETOOTH (0)

#ifndef MICROPY_PY_NETWORK_WLAN
#define MICROPY_PY_NETWORK_WLAN (1)
#endif

#define MICROPY_HW_I2C0_SCL (5)
#define MICROPY_HW_I2C0_SDA (4)

#ifndef traceISR_EXIT_TO_SCHEDULER
#define traceISR_EXIT_TO_SCHEDULER()
#endif
