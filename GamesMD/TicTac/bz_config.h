#pragma once
// ===== Wiring: ESP32-C3 SuperMini + 2.0" ST7789V 240x320 + microSD, 6 buttons =====
// Display and SD card share SCK and MOSI; each has its own CS.
#define PIN_SPI_SCK    4    // display SCL + SD CLK (pin 5)
#define PIN_SPI_MOSI   6    // display SDA + SD CMD (pin 3)
#define PIN_SPI_MISO  21    // SD DAT0 (pin 7)
#define PIN_TFT_CS     7
#define PIN_TFT_DC    10
#define PIN_TFT_RST   -1    // display RST is wired to 3V3; reset is done in software
#define PIN_SD_CS      3    // SD CD/DAT3 (pin 2)

// Buttons: one leg to the GPIO, the other to GND (internal pull-ups).
#define PIN_BTN_UP     1
#define PIN_BTN_DOWN   0
#define PIN_BTN_LEFT  20    // UART RX pin: keep "USB CDC On Boot: Enabled"
#define PIN_BTN_RIGHT  5
#define PIN_BTN_OK     9    // = BOOT: hold while plugging USB to enter download mode
#define PIN_BTN_BACK   8    // on-board LED shares this pin: it lights up while pressed

// Sound: passive piezo between GPIO 2 (through ~100 Ω) and GND. Nothing connected = silent, no harm.
#define PIN_BUZZER     2

#define TFT_SPI_FREQ  40000000
#define SD_SPI_FREQ   20000000
#define TFT_INVERT    true
