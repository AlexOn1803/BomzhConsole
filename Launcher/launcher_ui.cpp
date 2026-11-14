#include "launcher_ui.h"
#include <algorithm>
#include <stdio.h>
#include "bz_fonts.h"
#include "bz_gfx.h"
#include "bz_ui.h"

using namespace gfx;

static const uint16_t CARD = rgb(0x141a22), CARD_SEL = rgb(0x2a2210), TAG = rgb(0x1f3a28);

#define BM(name, ...) static const char* const name[] = {__VA_ARGS__}
BM(IC_CART, "..######..", ".#......#.", ".#.####.#.", ".#.#..#.#.", ".#.####.#.", ".#......#.", ".#......#.", ".#.#..#.#.", ".########.", "..........");
BM(IC_WIFI, "..........", "..######..", ".#......#.", "#..####..#", "..#....#..", ".#..##..#.", "...#..#...", "....##....", "....##....", "..........");
BM(IC_UPD, "....##....", "....##....", "....##....", "....##....", ".########.", "..######..", "...####...", "....##....", "..........", "##########");
BM(IC_AUTO, "....##....", "...####...", "..##..##..", ".##....##.", ".#......#.", ".#..#...#.", ".##..#.##.", "..##..##..", "...####...", "..........");

// Without a .txt the game is shown by its file name; title, description and icon come from the SD card.
std::string titleFor(const std::string& file, std::string* desc) {
  if (desc) *desc = "Гра з SD-карти";
  return file.substr(0, file.find('.'));
}

static std::string sizeStr(uint64_t b) {
  char s[24];
  if (b >= 1000000000ull) snprintf(s, sizeof s, "%.1f ГБ", b / 1e9);
  else if (b >= 1000000) snprintf(s, sizeof s, "%.1f МБ", b / 1e6);
  else snprintf(s, sizeof s, "%u КБ", (unsigned)((b + 999) / 1000));
  return s;
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n\xEF\xBB\xBF"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static bool isRow(const std::string& r) {
  if (r.empty() || r.size() > 16) return false;
  for (char c : r) if (c != '#' && c != '.') return false;
  return true;
}

void parseGameTxt(const std::string& txt, GameEntry& g) {
  std::vector<std::string> lines;
  size_t p = 0;
  while (p <= txt.size()) {
    size_t e = txt.find('\n', p);
    if (e == std::string::npos) e = txt.size();
    lines.push_back(trim(txt.substr(p, e - p)));
    p = e + 1;
  }
  if (lines.size() > 0 && !lines[0].empty()) g.title = lines[0];
  if (lines.size() > 1 && !lines[1].empty()) g.desc = lines[1];
  std::vector<std::string> rows;
  for (size_t i = 2; i < lines.size(); i++) {
    const std::string& l = lines[i];
    size_t eq = l.find('=');
    std::string hex;
    if (eq != std::string::npos && l.find('"') == std::string::npos) hex = trim(l.substr(eq + 1));
    else if (l.size() == 7 && l[0] == '#' && !isRow(l)) hex = l;
    if (!hex.empty()) {
      if (hex[0] == '#') hex = hex.substr(1);
      if (hex.size() == 6 && hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
        g.iconColor = rgb((uint32_t)strtoul(hex.c_str(), nullptr, 16));
        g.hasColor = true;
      }
      continue;
    }
    if (l.find('"') != std::string::npos) {          // BM(IC_X, "....", "....");
      size_t q = 0;
      while ((q = l.find('"', q)) != std::string::npos) {
        size_t e = l.find('"', q + 1);
        if (e == std::string::npos) break;
        std::string r = l.substr(q + 1, e - q - 1);
        if (isRow(r)) rows.push_back(r);
        q = e + 1;
      }
    } else if (isRow(l)) rows.push_back(l);
  }
  if (rows.empty()) return;
  g.iconH = (uint8_t)std::min<size_t>(rows.size(), 16);
  g.iconW = 0;
  for (int y = 0; y < g.iconH; y++) {
    g.iconRows[y] = 0;
    for (size_t x = 0; x < rows[y].size(); x++) if (rows[y][x] == '#') g.iconRows[y] |= 1u << x;
    g.iconW = std::max<uint8_t>(g.iconW, (uint8_t)rows[y].size());
  }
}

void buildItems(LauncherState& st) {
  st.items.clear();
  for (int i = 0; i < (int)st.games.size(); i++) st.items.push_back({IT_GAME, i});
  if (st.fwPending) st.items.push_back({IT_UPDATE, -1});
  st.items.push_back({IT_WIFI, -1});
  st.items.push_back({IT_AUTORUN, -1});
  if (st.sel >= (int)st.items.size()) st.sel = (int)st.items.size() - 1;
  if (st.sel < 0) st.sel = 0;
}

static int itemH(const MenuItem& it) { return it.kind == IT_GAME ? 50 : 34; }

void drawMenu(LauncherState& st) {
  fill(0, 0, W, H, pal::BG);
  // header
  text("ІГРИ", 10, 28, pal::AMBER, F20B);
  std::string sd = st.sd ? "SD " + sizeStr(st.sdTotal - st.sdUsed) + " вільно" : "SD-карти немає";
  text(sd, W - 10, 26, st.sd ? pal::SUB : pal::RED, F11, RIGHT);
  fill(0, 38, W, 1, pal::LINE);

  const int Y0 = 46, YMAX = H - 22;
  // keep the selection visible
  auto yOf = [&](int idx) { int y = 0; for (int i = st.top; i < idx; i++) y += itemH(st.items[i]) + 6; return y; };
  if (st.sel < st.top) st.top = st.sel;
  while (Y0 + yOf(st.sel) + itemH(st.items[st.sel]) > YMAX && st.top < st.sel) st.top++;

  int y = Y0;
  if (st.games.empty()) {
    std::string m = st.sd ? "У папці /games на SD-карті немає ігор. Завантаж їх по Wi-Fi або скопіюй .bin з комп'ютера."
                          : "Встав SD-карту з папкою /games.";
    auto ls = wrap(m, W - 28, F12);
    fill(8, y, W - 16, 16 + 15 * (int)ls.size(), CARD);
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], 16, y + 18 + 15 * (int)i, pal::SUB, F12);
    y += 22 + 15 * (int)ls.size();
  }
  for (int i = st.top; i < (int)st.items.size(); i++) {
    const MenuItem& it = st.items[i];
    int h = itemH(it);
    if (y + h > YMAX) break;
    bool sel = i == st.sel;
    fillRound(8, y, W - 16, h, 6, sel ? CARD_SEL : CARD);
    if (sel) { fill(8, y + 6, 3, h - 12, pal::AMBER); }
    if (it.kind == IT_GAME) {
      const GameEntry& g = st.games[it.game];
      uint16_t ic = pal::SUB;
      fillRound(18, y + 9, 32, 32, 6, rgb(0x0b0f14));
      if (g.iconH) {
        uint16_t c = g.hasColor ? g.iconColor : ic;
        int ox = 18 + (32 - g.iconW * 2) / 2, oy = y + 9 + (32 - g.iconH * 2) / 2;
        for (int iy = 0; iy < g.iconH; iy++)
          for (int ix = 0; ix < g.iconW; ix++)
            if (g.iconRows[iy] >> ix & 1) fill(ox + ix * 2, oy + iy * 2, 2, 2, c);
      } else bitmap(IC_CART, 10, 24, y + 15, 2, ic);
      text(fit(g.title, 120, F16B), 60, y + 22, sel ? pal::AMBER : pal::TXT, F16B);
      text(fit(g.desc, W - 76, F11), 60, y + 39, pal::SUB, F11);
      std::string tag = !g.onSd ? "лише в пам'яті" : g.installed ? "встановлена" : sizeStr(g.size);
      int tw = textW(tag, F10B) + 10;
      if (g.installed || !g.onSd) fillRound(W - 18 - tw, y + 8, tw, 15, 4, TAG);
      text(tag, W - 23, y + 19, g.installed || !g.onSd ? pal::GREEN : pal::DIM, F10B, RIGHT);
    } else {
      const char* const* icon = it.kind == IT_WIFI ? IC_WIFI : it.kind == IT_UPDATE ? IC_UPD : IC_AUTO;
      bitmap(icon, 10, 22, y + 7, 2, it.kind == IT_WIFI ? pal::BLUE : it.kind == IT_UPDATE ? pal::GREEN : pal::SUB);
      std::string label = it.kind == IT_WIFI ? "Завантажити ігри по Wi-Fi" : it.kind == IT_UPDATE ? "Оновити прошивку консолі" : "Автозапуск останньої гри";
      text(label, 52, y + 22, sel ? pal::AMBER : pal::TXT, F12B);
      if (it.kind == IT_AUTORUN) text(st.autorun ? "увімк" : "вимк", W - 20, y + 22, st.autorun ? pal::GREEN : pal::DIM, F11B, RIGHT);
    }
    y += h + 6;
  }
  // scroll marks
  if (st.top > 0) tri(W / 2 - 5, 44, W / 2 + 5, 44, W / 2, 40, pal::SUB);
  if (y > YMAX - 10 && st.items.size() > 0 && st.sel < (int)st.items.size() - 1) tri(W / 2 - 5, YMAX - 4, W / 2 + 5, YMAX - 4, W / 2, YMAX, pal::SUB);
  uiHint(st.msg.empty() ? "OK — грати · утримай Назад — збереження" : st.msg);
}

void drawInstall(const std::string& title, uint32_t done, uint32_t total, const char* head) {
  fill(0, 0, W, H, pal::BG);
  text(head, W / 2, 120, pal::SUB, F12, CENTER);
  text(title, W / 2, 150, pal::AMBER, F20B, CENTER);
  int pw = W - 40, p = total ? (int)((uint64_t)pw * done / total) : 0;
  fillRound(20, 172, pw, 12, 6, pal::LINE);
  if (p > 0) fillRound(20, 172, std::max(p, 12), 12, 6, pal::AMBER);
  char s[48];
  snprintf(s, sizeof s, "%u / %u КБ", (unsigned)(done / 1024), (unsigned)(total / 1024));
  text(s, W / 2, 204, pal::SUB, F11, CENTER);
  text("Не вимикай консоль", W / 2, 290, pal::DIM, F11, CENTER);
}

// Screen layout for Wi-Fi mode: rows 0..39 (header) and 280..319 (status + hint) can be redrawn later;
// rows in between are drawn once, because the launcher lends that part of the frame buffer to Wi-Fi.
void drawWifiHeader(int clients) {
  fill(0, 0, W, 40, pal::BG);
  text("Wi-Fi", 10, 28, pal::BLUE, F20B);
  text(clients ? "телефон підключено" : "чекаю на телефон", W - 10, 26, clients ? pal::GREEN : pal::SUB, F11, RIGHT);
  fill(0, 38, W, 1, pal::LINE);
}
void drawWifiStatus(const std::string& status) {
  fill(0, 280, W, 22, pal::BG);
  if (!status.empty()) {
    fill(0, 281, W, 18, pal::PANEL);
    text(fit(status, W - 16, F11), 8, 294, pal::GREEN, F11);
  }
}
void drawWifi(const char* ssid, const char* pass, const char* url, const std::string& status, int clients) {
  fill(0, 0, W, H, pal::BG);
  drawWifiHeader(clients);
  fillRound(55, 46, 130, 130, 8, rgb(0xffffff));   // QR code goes here (drawn by the display driver)
  int y = 194;
  text("1. Скануй QR камерою телефона", 12, y, pal::TXT, F12); y += 18;
  text(std::string("   або мережа ") + ssid + " (WPA2)", 12, y, pal::SUB, F11); y += 15;
  text(std::string("   пароль ") + pass, 12, y, pal::SUB, F11); y += 18;
  text("2. Відкрий у браузері:", 12, y, pal::TXT, F12); y += 18;
  text(url, 24, y, pal::AMBER, F13B);
  drawWifiStatus(status);
  uiHint("Назад — вийти (консоль перезапуститься)");
}
