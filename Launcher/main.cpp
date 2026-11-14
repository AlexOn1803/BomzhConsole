// Лаунчер: список ігор з /games на SD-карті, встановлення гри в слот ota_0, Wi-Fi завантаження.
#include <vector>
#include <Arduino.h>
#include "BomzhCore.h"
#include <Preferences.h>
#include <SD.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_heap_caps_init.h>
#include "bz_display.h"
#include "launcher_ui.h"
#include "webpage.h"

extern LGFX tft;

static const char* AP_SSID = "BomzhConsole";
static const char* AP_PASS = "bomzh2026";
static const char* AP_URL = "http://192.168.4.1";

// Marker that tells a launcher image apart from a game: firmware updates are accepted only if it is inside.
extern "C" __attribute__((used)) const char BZ_LAUNCHER_MARK[] = "BZ-LAUNCHER-IMAGE-MARK-1";
static const char* FW_PATH = "/firmware/launcher.bin";
static const char* FW_VERSION = __DATE__ " " __TIME__;

// I/O buffers are taken from the heap only while copying and given back afterwards, so Wi-Fi has more RAM.
struct IoBuf {
  uint8_t* p;
  explicit IoBuf(size_t n) : p((uint8_t*)malloc(n)) {}
  ~IoBuf() { free(p); }
};

static LauncherState st;
static Preferences prefs;
static String curFile;          // game currently in the ota_0 slot
static uint32_t curSize = 0;

// ---------- installed game ----------
static const esp_partition_t* gameSlot() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
}
static bool slotValid() {
  const esp_partition_t* p = gameSlot();
  esp_app_desc_t d;
  return p && esp_ota_get_partition_description(p, &d) == ESP_OK;
}

static void scanGames() {
  st.games.clear();
  st.sd = sdReady();
  bool curSeen = false;
  if (st.sd) {
    st.sdTotal = SD.totalBytes();
    st.sdUsed = SD.usedBytes();
    if (!SD.exists("/games")) SD.mkdir("/games");
    st.fwPending = SD.exists(FW_PATH);
    File dir = SD.open("/games");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (f.isDirectory() || !name.endsWith(".bin") || name.startsWith(".")) continue;
      GameEntry g;
      g.file = name.c_str();
      g.size = f.size();
      g.title = titleFor(g.file, &g.desc);
      String txt = "/games/" + name.substring(0, name.length() - 4) + ".txt";
      if (SD.exists(txt)) {            // optional title, description and icon from a .txt next to the game
        File t = SD.open(txt);
        size_t n = std::min<size_t>(t.size(), 4096);
        std::string buf(n, '\0');
        n = t.read((uint8_t*)&buf[0], n);
        buf.resize(n);
        t.close();
        parseGameTxt(buf, g);
      }
      g.installed = slotValid() && name == curFile && g.size == curSize;
      curSeen |= g.installed;
      st.games.push_back(g);
    }
    dir.close();
    std::sort(st.games.begin(), st.games.end(), [](const GameEntry& a, const GameEntry& b) {
      return a.installed != b.installed ? a.installed : a.title < b.title;
    });
  }
  if (!curSeen && slotValid() && curFile.length()) {   // card missing or file deleted: still playable
    GameEntry g;
    g.file = curFile.c_str();
    g.title = titleFor(g.file, &g.desc);
    String t = prefs.getString("curtitle", ""), d = prefs.getString("curdesc", "");
    if (t.length()) g.title = t.c_str();
    if (d.length()) g.desc = d.c_str();
    g.size = curSize;
    g.installed = true;
    g.onSd = false;
    st.games.insert(st.games.begin(), g);
  }
  buildItems(st);
}

static void redraw() { drawMenu(st); present(); }

static void bootSlot() {
  SD.end();
  esp_ota_set_boot_partition(gameSlot());
  esp_restart();
}

static void showError(const std::string& t) {
  std::string o[1] = {"OK"};
  drawMenu(st);
  uiDialog("ПОМИЛКА", t, o, 1, 0);
  present();
  inputClear();
  for (;;) { consolePoll(); int k = inputGet(); if (k == K_OK || k == K_BACK) break; delay(10); }
  inputClear();
}

static void install(const GameEntry& g) {
  if (g.installed) bootSlot();                           // already in the slot: start instantly
  File f = SD.open(String("/games/") + g.file.c_str(), FILE_READ);
  if (!f) { showError("Не вдалося відкрити файл гри."); return; }
  uint32_t total = f.size();
  const esp_partition_t* slot = gameSlot();
  if (!slot || total > slot->size) { f.close(); showError("Гра завелика для слоту (максимум 2,4 МБ)."); return; }
  if (!Update.begin(total, U_FLASH)) { f.close(); showError("Не вдалося підготувати пам'ять для гри."); return; }
  IoBuf io(4096);
  uint8_t* buf = io.p;
  if (!buf) { f.close(); Update.abort(); showError("Замало пам'яті."); return; }
  uint32_t done = 0, shown = 0;
  drawInstall(g.title, 0, total); present();
  while (done < total) {
    int n = f.read(buf, 4096);
    if (n <= 0 || Update.write(buf, n) != (size_t)n) break;
    done += n;
    if (done - shown >= 48 * 1024 || done == total) { drawInstall(g.title, done, total); present(); shown = done; }
  }
  f.close();
  if (done != total || !Update.end(true)) {
    Update.abort();
    curFile = ""; curSize = 0;
    prefs.putString("cur", ""); prefs.putUInt("cursz", 0);
    showError("Файл гри пошкоджений або це не прошивка ESP32-C3.");
    scanGames();
    return;
  }
  prefs.putString("cur", g.file.c_str());
  prefs.putString("curtitle", g.title.c_str());
  prefs.putString("curdesc", g.desc.c_str());
  prefs.putUInt("cursz", total);
  bootSlot();                                            // Update.end() already selected the slot
}

// ---------- launcher self-update ----------
// Step 1 (factory launcher): copy /firmware/launcher.bin into the game slot and boot it.
// Step 2 (new launcher running from the slot): copy the same file over the factory partition, verify, boot it.
// If power is lost in step 2 the slot still holds a working launcher, which simply retries.
static const esp_partition_t* factoryPart() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
}

static bool checkFirmware(const char* path, std::string& err, uint32_t* sizeOut = nullptr) {
  File f = SD.open(path, FILE_READ);
  if (!f) { err = "Файл прошивки не знайдено."; return false; }
  uint32_t size = f.size();
  const esp_partition_t* fp = factoryPart();
  if (!fp || size < 100000 || size > fp->size) { f.close(); err = "Це не прошивка лаунчера (неправильний розмір). Потрібен Launcher.ino.bin, а не merged."; return false; }
  IoBuf io(4096 + 64);
  uint8_t* buf = io.p;
  if (!buf) { f.close(); err = "Замало пам'яті для перевірки."; return false; }
  const size_t ML = sizeof(BZ_LAUNCHER_MARK) - 1;
  size_t keep = 0;
  bool first = true, found = false;
  while (!found) {
    int n = f.read(buf + keep, 4096);
    if (n <= 0) break;
    if (first) { if (buf[0] != 0xE9) break; first = false; }
    size_t tot = keep + n;
    for (size_t i = 0; i + ML <= tot; i++) if (buf[i] == 'B' && memcmp(buf + i, BZ_LAUNCHER_MARK, ML) == 0) { found = true; break; }
    keep = tot < ML ? tot : ML - 1;
    memmove(buf, buf + tot - keep, keep);
  }
  f.close();
  if (!found) { err = "Цей файл — не прошивка лаунчера (можливо, гра). Ігри кидай у список ігор."; return false; }
  if (sizeOut) *sizeOut = size;
  return true;
}

static void firmwareStep1() {
  std::string err;
  uint32_t total;
  if (!checkFirmware(FW_PATH, err, &total)) { showError(err); return; }
  File f = SD.open(FW_PATH, FILE_READ);
  if (!Update.begin(total, U_FLASH)) { f.close(); showError("Не вдалося підготувати пам'ять."); return; }
  IoBuf io(4096);
  uint8_t* buf = io.p;
  if (!buf) { f.close(); Update.abort(); showError("Замало пам'яті."); return; }
  uint32_t done = 0, shown = 0;
  drawInstall("Прошивка консолі", 0, total, "Оновлення 1/2"); present();
  while (done < total) {
    int n = f.read(buf, 4096);
    if (n <= 0 || Update.write(buf, n) != (size_t)n) break;
    done += n;
    if (done - shown >= 64 * 1024 || done == total) { drawInstall("Прошивка консолі", done, total, "Оновлення 1/2"); present(); shown = done; }
  }
  f.close();
  // the game slot no longer holds a game
  curFile = ""; curSize = 0;
  prefs.putString("cur", ""); prefs.putUInt("cursz", 0);
  if (done != total || !Update.end(true)) { Update.abort(); showError("Не вдалося записати прошивку."); scanGames(); return; }
  prefs.putBool("fwstep2", true);
  SD.end();
  esp_restart();   // Update.end() selected the slot
}

// returns only if there is nothing to do (normal start from the factory partition)
static void firmwareStep2() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* fp = factoryPart();
  if (!run || !fp || run->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return;
  esp_ota_set_boot_partition(run);            // until the factory copy is verified, keep booting this one
  if (!prefs.getBool("fwstep2", false)) { esp_ota_set_boot_partition(fp); esp_restart(); }   // not an update: back to the menu
  std::string err;
  uint32_t total = 0;
  bool touched = false;   // set once the factory partition has been erased
  bool ok = checkFirmware(FW_PATH, err, &total);
  if (ok) {
    File f = SD.open(FW_PATH, FILE_READ);
    IoBuf io(8192 + 64);
    if (!io.p) { f.close(); showError("Замало пам'яті."); esp_restart(); }
    uint8_t *buf = io.p, *chk = io.p + 4096 + 32;
    uint32_t erase = (total + 4095) & ~4095u;
    drawInstall("Прошивка консолі", 0, total, "Оновлення 2/2"); present();
    touched = true;
    ok = esp_partition_erase_range(fp, 0, erase) == ESP_OK;
    uint32_t done = 0, shown = 0;
    while (ok && done < total) {
      int n = f.read(buf, 4096);
      if (n <= 0) { ok = false; break; }
      uint32_t wn = (n + 3) & ~3u;
      for (uint32_t i = n; i < wn; i++) buf[i] = 0xFF;
      ok = esp_partition_write(fp, done, buf, wn) == ESP_OK && esp_partition_read(fp, done, chk, n) == ESP_OK && memcmp(buf, chk, n) == 0;
      done += n;
      if (done - shown >= 64 * 1024 || done == total) { drawInstall("Прошивка консолі", done, total, "Оновлення 2/2"); present(); shown = done; }
    }
    f.close();
    esp_app_desc_t d;
    ok = ok && esp_ota_get_partition_description(fp, &d) == ESP_OK;
  }
  if (ok) {
    prefs.putBool("fwstep2", false);
    SD.remove(FW_PATH);
    esp_ota_set_boot_partition(fp);
    gfx::fill(0, 0, gfx::W, gfx::H, pal::BG);
    gfx::text("Прошивку оновлено!", gfx::W / 2, 150, pal::GREEN, F16B, gfx::CENTER);
    gfx::text("Перезапуск…", gfx::W / 2, 176, pal::SUB, F12, gfx::CENTER);
    present();
    delay(1200);
    SD.end();
    esp_restart();
  }
  if (touched) {
    // factory is half-written: keep booting this (working) launcher from the slot and retry on restart
    showError("Не вдалося записати прошивку. OK — спробувати ще раз.");
    SD.end();
    esp_restart();
  }
  // the file is gone or wrong, factory untouched: go back to the old launcher
  showError(err);
  prefs.putBool("fwstep2", false);
  esp_ota_set_boot_partition(fp);
  SD.end();
  esp_restart();
}

// ---------- Wi-Fi ----------
static WebServer server(80);
static std::string wifiStatus;
static File upFile;
static String upPath;
static bool wifiDirty = true;

static bool safePath(const String& p) {
  return (p.startsWith("/games/") || p.startsWith("/saves/")) && p.indexOf("..") < 0 && p.indexOf('/', 7) < 0;
}

static void listDir(String& json, const char* dir) {
  File d = SD.open(dir);
  bool first = true;
  if (d) for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory()) continue;
    String n = f.name();
    int s = n.lastIndexOf('/');
    if (s >= 0) n = n.substring(s + 1);
    if (n.startsWith(".") || n.endsWith(".tmp")) continue;
    n.replace("\"", "");
    json += String(first ? "" : ",") + "{\"name\":\"" + n + "\",\"size\":" + f.size() + "}";
    first = false;
  }
  d.close();
}

static void webSetup() {
  server.on("/", HTTP_GET, [] { server.send(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/api/list", HTTP_GET, [] {
    String j = "{\"fw\":\"" + String(FW_VERSION) + "\",\"fwPending\":" + (st.fwPending ? "true" : "false") + ",\"total\":" + String((double)SD.totalBytes(), 0) + ",\"used\":" + String((double)SD.usedBytes(), 0) + ",\"games\":[";
    listDir(j, "/games");
    j += "],\"saves\":[";
    listDir(j, "/saves");
    j += "]}";
    server.send(200, "application/json", j);
  });
  server.on("/dl", HTTP_GET, [] {
    String p = server.arg("path");
    if (!safePath(p) || !SD.exists(p)) { server.send(404, "text/plain", "немає файлу"); return; }
    File f = SD.open(p);
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + p.substring(7) + "\"");
    server.streamFile(f, "application/octet-stream");
    f.close();
  });
  server.on("/delete", HTTP_POST, [] {
    String p = server.arg("path");
    if (!safePath(p)) { server.send(400, "text/plain", "заборонено"); return; }
    bool ok = SD.remove(p);
    wifiStatus = (ok ? "Видалено: " : "Не вдалося видалити: ") + std::string(p.substring(7).c_str());
    wifiDirty = true;
    server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "fail");
  });
  server.on("/upload", HTTP_POST,
    [] {
      bool ok = upPath.length() && SD.exists(upPath);
      if (server.arg("dir") == "firmware" && !ok) { server.send(400, "text/plain", wifiStatus.c_str()); return; }
      server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "не вдалося записати на карту");
    },
    [] {
      HTTPUpload& u = server.upload();
      if (u.status == UPLOAD_FILE_START) {
        String name = u.filename;
        int s = name.lastIndexOf('/');
        if (s >= 0) name = name.substring(s + 1);
        bool fw = server.arg("dir") == "firmware";
        String dir = fw ? "/firmware/" : server.arg("dir") == "saves" ? "/saves/" : "/games/";
        upPath = "";
        if (!(name.endsWith(".bin") || name.endsWith(".txt") || name.endsWith(".sav")) || name.indexOf("..") >= 0) return;
        if (fw && !name.endsWith(".bin")) return;
        upPath = fw ? String(FW_PATH) : dir + name;
        if (!SD.exists(dir.substring(0, dir.length() - 1))) SD.mkdir(dir.substring(0, dir.length() - 1));
        upFile = SD.open(upPath + ".tmp", FILE_WRITE);
        wifiStatus = "Отримую " + std::string(name.c_str()) + "…";
        wifiDirty = true;
      } else if (u.status == UPLOAD_FILE_WRITE) {
        if (upFile) upFile.write(u.buf, u.currentSize);
      } else if (u.status == UPLOAD_FILE_END) {
        if (upFile) {
          upFile.close();
          SD.remove(upPath);
          SD.rename(upPath + ".tmp", upPath);
          if (upPath == FW_PATH) {
            std::string err;
            if (checkFirmware(FW_PATH, err)) { st.fwPending = true; wifiStatus = "Прошивка отримана — натисни OK"; }
            else { SD.remove(FW_PATH); st.fwPending = false; wifiStatus = err; upPath = ""; }
            wifiDirty = true;
            return;
          }
          char s[96];
          snprintf(s, sizeof s, "Отримано %s (%u КБ)", upPath.substring(7).c_str(), (unsigned)(u.totalSize / 1024));
          wifiStatus = s;
          wifiDirty = true;
        }
      } else if (u.status == UPLOAD_FILE_ABORTED) {
        if (upFile) { upFile.close(); SD.remove(upPath + ".tmp"); }
        upPath = "";
        wifiStatus = "Передачу перервано";
        wifiDirty = true;
      }
    });
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
}

static void presentRows(int y0, int h) {
  tft.startWrite();
  tft.setAddrWindow(0, y0, gfx::W, h);
  tft.writePixels(gfx::fb + y0 * gfx::W, gfx::W * h, true);
  tft.endWrite();
}

// Wi-Fi needs ~70 KB of RAM, more than is left next to the 150 KB frame buffer. So once the Wi-Fi screen is
// drawn, rows 42..277 of the frame buffer are handed to the heap for good, and leaving Wi-Fi restarts the console.
static const int LEND_Y0 = 42, LEND_Y1 = 278;

static void wifiMode() {
  if (!st.sd) { showError("Для завантаження ігор потрібна SD-карта."); return; }
  char qr[96];
  snprintf(qr, sizeof qr, "WIFI:T:WPA;S:%s;P:%s;;", AP_SSID, AP_PASS);
  drawWifi(AP_SSID, AP_PASS, AP_URL, "Запускаю Wi-Fi…", 0);
  present();
  tft.qrcode(qr, 59, 50, 122, 3);   // drawn straight onto the display, inside the white square
  intptr_t a = ((intptr_t)&gfx::fb[LEND_Y0 * gfx::W] + 15) & ~(intptr_t)15;
  intptr_t b = ((intptr_t)&gfx::fb[LEND_Y1 * gfx::W]) & ~(intptr_t)15;
  esp_err_t lent = heap_caps_add_region(a, b);
  Serial.printf("[wifi] lend %d KB: %d, free heap %u\n", (int)((b - a) / 1024), (int)lent, (unsigned)ESP.getFreeHeap());

  WiFi.mode(WIFI_AP);
  delay(100);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);
  // Force WPA2-PSK with AES only (older cores default to mixed WPA/WPA2, shown as "WPA" on phones).
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK) {
    conf.ap.authmode = WIFI_AUTH_WPA2_PSK;
    conf.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    apOk = esp_wifi_set_config(WIFI_IF_AP, &conf) == ESP_OK && apOk;
  }
  // ESP32-C3 SuperMini: the antenna is poorly matched, full TX power often breaks Wi-Fi completely.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  webSetup();
  server.begin();
  char hs[80];
  snprintf(hs, sizeof hs, "Мережа працює (WPA2) · вільно %u КБ", (unsigned)(ESP.getFreeHeap() / 1024));
  wifiStatus = apOk ? hs : "Не вдалося запустити Wi-Fi — перезавантаж консоль";
  wifiDirty = true;
  int clients = 0;
  bool headerDirty = false;
  inputClear();
  for (;;) {
    server.handleClient();
    consolePoll();
    int k = inputGet();
    if (k == K_BACK || k == K_BACK_LONG) break;
    if (k == K_OK && st.fwPending) { prefs.putBool("fwgo", true); break; }   // update right after the restart
    int c = WiFi.softAPgetStationNum();
    if (c != clients) { clients = c; headerDirty = true; }
    if (headerDirty) { drawWifiHeader(clients); presentRows(0, 40); headerDirty = false; }
    if (wifiDirty) { drawWifiStatus(wifiStatus); presentRows(280, 22); wifiDirty = false; }
    delay(2);
  }
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  SD.end();
  esp_restart();   // the lent part of the frame buffer cannot be taken back from the heap
}

// ---------- saves menu (hold Back in the game list) ----------
struct SaveFile { std::string id, label; uint32_t size; };

static std::vector<SaveFile> listSaves() {
  std::vector<SaveFile> v;
  if (!st.sd) return v;
  File d = SD.open("/saves");
  if (d) for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String n = f.name();
    int sl = n.lastIndexOf('/');
    if (sl >= 0) n = n.substring(sl + 1);
    if (f.isDirectory() || !n.endsWith(".sav")) continue;
    SaveFile s;
    s.id = n.substring(0, n.length() - 4).c_str();
    s.label = s.id;
    for (auto& g : st.games) if (g.file.substr(0, g.file.find('.')) == s.id) s.label = g.title;   // show the game's title
    s.size = f.size();
    v.push_back(s);
  }
  if (d) d.close();
  return v;
}

static void eraseSave(const std::string& id) {
  SD.remove((std::string("/saves/") + id + ".sav").c_str());
  SD.remove((std::string("/saves/") + id + ".tmp").c_str());
  Preferences p;                       // the backup copy in internal flash
  if (p.begin("bzsave", false)) { p.remove(id.c_str()); p.end(); }
}

static bool askYesNo(const std::string& title, const std::string& body) {
  std::string o[2] = {"Так", "Ні"};
  int sel = 1;
  inputClear();
  for (;;) {
    drawMenu(st);
    uiDialog(title, body, o, 2, sel);
    present();
    int k;
    while ((k = (consolePoll(), inputGet())) < 0) delay(10);
    if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) sel ^= 1;
    if (k == K_BACK || k == K_BACK_LONG) return false;
    if (k == K_OK) return sel == 0;
  }
}

static void savesMenu() {
  using namespace gfx;
  int sel = 0, top = 0;
  inputClear();
  for (;;) {
    std::vector<SaveFile> sv = listSaves();
    int n = (int)sv.size() + 2;          // saves + "all" + "close"
    if (sel >= n) sel = n - 1;
    drawMenu(st);
    blendRect(0, 0, W, H, pal::SHADE, 180);
    fill(10, 30, W - 20, 262, pal::PANEL);
    strokeRect(10, 30, W - 20, 262, pal::AMBER);
    text("ЗБЕРЕЖЕННЯ", W / 2, 48, pal::AMBER, F12B, CENTER);
    text(sv.empty() ? "Збережень немає" : "OK — видалити вибране", W / 2, 64, pal::SUB, F11, CENTER);
    const int VIS = 10;
    if (sel < top) top = sel;
    if (sel >= top + VIS) top = sel - VIS + 1;
    for (int i = top; i < n && i < top + VIS; i++) {
      int y = 86 + (i - top) * 20;
      bool s = i == sel;
      if (s) fill(16, y - 13, W - 32, 18, i == (int)sv.size() ? pal::RED : pal::AMBER);
      if (i < (int)sv.size()) {
        text(fit(sv[i].label, W - 90, F11B), 22, y, s ? pal::SELTXT : pal::TXT, F11B);
        char b[16];
        snprintf(b, sizeof b, "%u Б", (unsigned)sv[i].size);
        text(b, W - 22, y, s ? pal::SELTXT : pal::DIM, F10B, RIGHT);
      } else if (i == (int)sv.size()) text("Видалити ВСІ збереження", 22, y, s ? rgb(0xffffff) : pal::RED, F11B);
      else text("Закрити", 22, y, s ? pal::SELTXT : pal::TXT, F11B);
    }
    uiHint("▲▼ — вибір · OK — видалити · Назад — закрити");
    present();
    int k;
    while ((k = (consolePoll(), inputGet())) < 0) delay(10);
    if (k == K_UP) sel = (sel + n - 1) % n;
    if (k == K_DOWN) sel = (sel + 1) % n;
    if (k == K_BACK || k == K_BACK_LONG) break;
    if (k == K_OK) {
      if (sel == n - 1) break;
      if (sel == (int)sv.size()) {
        if (sv.empty()) continue;
        if (askYesNo("ВИДАЛИТИ ВСЕ?", "Усі збереження всіх ігор зникнуть назавжди.")) {
          for (auto& f : sv) eraseSave(f.id);
          Preferences p;
          if (p.begin("bzsave", false)) { p.clear(); p.end(); }
          st.msg = "Усі збереження видалено";
          break;
        }
      } else if (askYesNo("ВИДАЛИТИ?", "Збереження гри «" + sv[sel].label + "» зникне назавжди. Гра почнеться спочатку.")) {
        eraseSave(sv[sel].id);
        st.msg = "Видалено: " + sv[sel].label;
      }
    }
  }
  inputClear();
}

// ---------- menu ----------
void setup() {
  consoleBegin("launcher");
  inputSetRepeat(REPEAT_MENU);
  prefs.begin("launcher", false);
  curFile = prefs.getString("cur", "");
  curSize = prefs.getUInt("cursz", 0);
  st.autorun = prefs.getBool("autorun", false);
  st.sound = soundEnabled();
  firmwareStep2();
  scanGames();
  if (prefs.getBool("fwgo", false)) {   // OK was pressed on the Wi-Fi screen to install new firmware
    prefs.putBool("fwgo", false);
    if (st.fwPending) firmwareStep1();
  }
  // tell the player if the console restarted because of a crash or a power dip
  esp_reset_reason_t why = esp_reset_reason();
  if (why == ESP_RST_BROWNOUT) st.msg = "Перезапуск: просіло живлення";
  else if (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT)
    st.msg = "Перезапуск через помилку програми";
  Serial.printf("[boot] reset reason %d, free heap %u\n", (int)why, (unsigned)ESP.getFreeHeap());
  redraw();
}

void loop() {
  consolePoll();
  int k = inputGet();
  if (k < 0) { delay(5); return; }
  int n = (int)st.items.size();
  if (k == K_UP) { st.sel = (st.sel - 1 + n) % n; sfxClick(); }
  if (k == K_DOWN) { st.sel = (st.sel + 1) % n; sfxClick(); }
  if (k == K_OK) {
    const MenuItem& it = st.items[st.sel];
    if (it.kind == IT_GAME) {
      const GameEntry& g = st.games[it.game];
      if (!g.onSd) bootSlot(); else install(g);
    } else if (it.kind == IT_WIFI) {
      wifiMode();
    } else if (it.kind == IT_UPDATE) {
      firmwareStep1();
    } else if (it.kind == IT_SOUND) {
      st.sound = !st.sound;
      soundSetEnabled(st.sound);
      if (st.sound) sfxSelect();
      st.msg = st.sound ? "Звук увімкнено (п'єзо на GPIO 2)" : "Звук вимкнено в усіх іграх";
    } else if (it.kind == IT_AUTORUN) {
      st.autorun = !st.autorun;
      prefs.putBool("autorun", st.autorun);
      st.msg = st.autorun ? "Після увімкнення одразу стартує остання гра" : "Після увімкнення відкривається це меню";
    }
  }
  if (k == K_BACK) { scanGames(); st.msg = "Список оновлено"; }
  if (k == K_BACK_LONG) savesMenu();   // hold Back: manage saves
  redraw();
}
