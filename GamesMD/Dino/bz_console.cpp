// Device implementation of the console services (ESP32-C3 + Arduino).
#ifdef ARDUINO
#include "bz_console.h"
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include "bz_config.h"
#include "bz_display.h"
#include "bz_gfx.h"

LGFX tft;                       // shared with the launcher (QR code)
static bool sdOk = false;
static char gid[16] = "game";
static const uint8_t PINS[NBTN] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_OK, PIN_BTN_BACK};

uint32_t nowMs() { return millis(); }
void sleepMs(uint32_t ms) { delay(ms); }
uint32_t randomU32() { return esp_random(); }
bool sdReady() { return sdOk; }

static uint8_t readRaw() {
  uint8_t r = 0;
  for (int i = 0; i < NBTN; i++) if (digitalRead(PINS[i]) == LOW) r |= 1u << i;
  return r;
}
// Buttons are sampled every 4 ms by a background timer, independent of how long a game takes to draw a frame.
// (Before, they were read once per frame: a quick tap shorter than one slow frame could be missed.)
static esp_timer_handle_t btnTimer = nullptr;
static void btnTick(void*) { inputFeed(readRaw(), millis()); }
static void startButtonTimer() {
  if (btnTimer) return;
  esp_timer_create_args_t a = {};
  a.callback = btnTick;
  a.name = "buttons";
  if (esp_timer_create(&a, &btnTimer) == ESP_OK) esp_timer_start_periodic(btnTimer, 4000);
}
void consolePoll() { if (!btnTimer) inputFeed(readRaw(), millis()); }

void present() {
  tft.startWrite();
  tft.setAddrWindow(0, 0, gfx::W, gfx::H);
  tft.writePixels(gfx::fb, gfx::W * gfx::H, true);  // framebuffer holds native RGB565
  tft.endWrite();                                    // releases the bus for the SD card
}

static const esp_partition_t* factoryPart() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
}
bool hasLauncher() { return factoryPart() != nullptr; }
void returnToLauncher() {
  const esp_partition_t* f = factoryPart();
  if (!f) return;
  if (sdOk) SD.end();
  esp_ota_set_boot_partition(f);
  esp_restart();
}

void consoleBegin(const char* gameId) {
  snprintf(gid, sizeof gid, "%s", gameId);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);              // keep the card quiet while the display starts
  for (int i = 0; i < NBTN; i++) pinMode(PINS[i], INPUT_PULLUP);
  delay(30);
  startButtonTimer();
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (digitalRead(PIN_BTN_BACK) == LOW && hasLauncher() && run && run->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY)
    returnToLauncher();                       // safety exit: hold BACK while switching on
  if (hasLauncher() && run && run->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    // The launcher boots us once; unless "autorun" is on, the next power-on opens the menu again.
    Preferences lp;
    bool autorun = false;
    if (lp.begin("launcher", true)) { autorun = lp.getBool("autorun", false); lp.end(); }
    if (!autorun) esp_ota_set_boot_partition(factoryPart());
  }
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  // tft.init() already started SPI on our pins; SD joins the same bus with its own CS.
  sdOk = SD.begin(PIN_SD_CS, SPI, SD_SPI_FREQ);
  if (sdOk && !SD.exists("/saves")) SD.mkdir("/saves");
}

// ---------- saves ----------
struct SaveHdr { uint32_t magic, len, crc; };
static const uint32_t SAVE_MAGIC = 0x56535a42;  // "BZSV"

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1))); }
  return ~c;
}

bool saveData(const void* data, size_t len) {
  SaveHdr h = {SAVE_MAGIC, (uint32_t)len, crc32((const uint8_t*)data, len)};
  bool ok = false;
  if (sdOk) {
    String path = String("/saves/") + gid + ".sav", tmp = path + ".tmp";
    File f = SD.open(tmp, FILE_WRITE);
    if (f) {
      ok = f.write((const uint8_t*)&h, sizeof h) == sizeof h && f.write((const uint8_t*)data, len) == len;
      f.close();
      if (ok) { SD.remove(path); ok = SD.rename(tmp, path); }
    }
  }
  Preferences p;                                   // backup copy in internal flash
  if (p.begin("bzsave", false)) {
    size_t total = sizeof h + len;
    uint8_t* all = (uint8_t*)malloc(total);
    if (all) {
      memcpy(all, &h, sizeof h);
      memcpy(all + sizeof h, data, len);
      ok = p.putBytes(gid, all, total) == total || ok;
      free(all);
    }
    p.end();
  }
  return ok;
}

static bool valid(const SaveHdr& h, const uint8_t* d, size_t len) {
  return h.magic == SAVE_MAGIC && h.len == len && h.crc == crc32(d, len);
}

bool loadData(void* data, size_t len) {
  uint8_t* tmp = (uint8_t*)malloc(len);
  if (!tmp) return false;
  bool ok = false;
  if (sdOk) {
    File f = SD.open(String("/saves/") + gid + ".sav", FILE_READ);
    if (f) {
      SaveHdr h;
      if (f.read((uint8_t*)&h, sizeof h) == sizeof h && f.read(tmp, len) == (int)len && valid(h, tmp, len)) ok = true;
      f.close();
    }
  }
  if (!ok) {
    Preferences p;
    if (p.begin("bzsave", true)) {
      size_t total = sizeof(SaveHdr) + len;
      if (p.getBytesLength(gid) == total) {
        uint8_t* all = (uint8_t*)malloc(total);
        if (all) {
          p.getBytes(gid, all, total);
          SaveHdr h;
          memcpy(&h, all, sizeof h);
          if (valid(h, all + sizeof h, len)) { memcpy(tmp, all + sizeof h, len); ok = true; }
          free(all);
        }
      }
      p.end();
    }
  }
  if (ok) memcpy(data, tmp, len);
  free(tmp);
  return ok;
}
#endif
