// Screen layout, 1:1 with the browser prototype.
#include "ui.h"
#include "game.h"
#include "bz_gfx.h"
#include "bz_fonts.h"
#include <algorithm>

using namespace gfx;

static const uint16_t C_BG = rgb(0x0e1218), C_PANEL = rgb(0x161c25), C_LINE = rgb(0x253041), C_TXT = rgb(0xe8eaee),
                      C_DIM = rgb(0x6b7687), C_SUB = rgb(0x9aa5b5), C_AMBER = rgb(0xffb21a), C_HP = rgb(0x4cd964),
                      C_FOOD = rgb(0xffcc00), C_MOOD = rgb(0x4aa8ff), C_RED = rgb(0xff4d4f), C_SELTXT = rgb(0x1a1200),
                      C_COLD = rgb(0x7fd3ff), C_ZERO = rgb(0x3a1416), C_SHADE = rgb(0x05070a);

#define BM(name, ...) static const char* const name[] = {__VA_ARGS__}
BM(IC0, "...####...", "..#....#..", "..#....#..", "...####...", "....##....", "..######..", ".#..##..#.", "....##....", "...#..#...", "..#....#..");
BM(IC1, "..........", "##########", "#........#", "#..####..#", "#..#..#..#", "#..####..#", "#........#", "##########", "..........", "..........");
BM(IC2, "..........", "..######..", ".########.", "##########", "..........", "##########", "..........", "##########", ".########.", "..........");
BM(IC3, "..........", ".##...##..", "####.####.", "#########.", "#########.", ".#######..", "..#####...", "...###....", "....#.....", "..........");
BM(IC4, "..######..", ".#......#.", "#..#..#..#", "#........#", "#.#....#.#", "#..####..#", ".#......#.", "..######..", "..........", "..........");
BM(IC5, "..##..##..", ".###..###.", "##########", "##.####.##", "...####...", "...####...", "...####...", "...####...", "..........", "..........");
BM(IC6, "..........", "....##....", "..######..", "##########", "..######..", "..######.#", "...####..#", ".........#", "..........", "..........");
BM(IC7, "....##....", "...####...", "..######..", ".########.", "##########", ".##....##.", ".##.##.##.", ".##.##.##.", ".##.##.##.", "..........");
BM(IC8, "..........", "...####...", "..#....#..", ".########.", "##########", "##########", ".##....##.", ".##....##.", "..........", "..........");
BM(IC9, "..........", "...####...", "...#..#...", "##########", "#........#", "##########", "#........#", "#........#", "##########", "..........");
BM(IC10, "##########", "#.#.#.#.#.", "##########", ".#......#.", ".#.####.#.", ".#.#..#.#.", ".#.#..#.#.", ".#.####.#.", "##########", "..........");
BM(IC11, "#.........", "#######...", "########..", "#######...", "#.........", "#.........", "#.........", "#.........", "##........", "##........");
BM(IC12, "##########", "#........#", "#...##...#", "#..####..#", "#...##...#", ".#......#.", ".#......#.", "..#....#..", "...#..#...", "....##....");
static const char* const* const IC[NTABS] = {IC0, IC1, IC2, IC3, IC4, IC5, IC6, IC7, IC8, IC9, IC10, IC11, IC12};
BM(STAR, "....#....", "....#....", "...###...", "#########", ".#######.", "..#####..", "..##.##..", ".#.....#.");
BM(BOTTLE, "..#..", "..#..", ".###.", "#####", "#####", "#####", "#####", "#####");

static std::string sgn(int64_t v) { return v > 0 ? "+" + fmt(v) : v < 0 ? "−" + fmt(-v) : "0"; }
static std::string dtxt(const int64_t d[2]) { return d[0] == d[1] ? sgn(d[0]) : sgn(d[0]) + "…" + sgn(d[1]); }
static uint16_t dcol(const int64_t d[2]) {
  if (d[0] >= 0 && d[1] > 0) return C_HP;
  if (d[1] <= 0 && d[0] < 0) return C_RED;
  return (d[0] || d[1]) ? C_AMBER : C_DIM;
}
static void lock(int x, int y, uint16_t c) { strokeRect(x + 1, y, 5, 5, c); fill(x, y + 4, 7, 5, c); }

static void box(const std::string& title, const std::string& body, const std::string* opts, int nopt, int sel) {
  blendRect(0, 0, W, H, C_SHADE, 184);
  std::vector<std::string> lines = wrap(body, 184, F12);
  if (lines.size() > 14) lines.resize(14);
  int h = 60 + (int)lines.size() * 15, y = std::max(4, (H - h) / 2);
  fill(16, y, 208, h, C_PANEL);
  strokeRect(16, y, 208, h, C_AMBER);
  text(title, W / 2, y + 18, C_AMBER, F12B, CENTER);
  for (size_t i = 0; i < lines.size(); i++) text(lines[i], W / 2, y + 38 + (int)i * 15, C_TXT, F12, CENTER);
  int bw = nopt > 1 ? 90 : 110, gap = 8, tot = nopt * bw + (nopt - 1) * gap;
  for (int i = 0; i < nopt; i++) {
    int x = (W - tot) / 2 + i * (bw + gap);
    bool s = i == sel;
    fill(x, y + h - 22, bw, 16, s ? C_AMBER : C_LINE);
    text(opts[i], x + bw / 2, y + h - 10, s ? C_SELTXT : C_TXT, F11B, CENTER);
  }
}

void draw() {
  fill(0, 0, W, H, C_BG);
  std::vector<Item> L = acts(ui.tab);
  int cur = std::min<int>(ui.cur[ui.tab], (int)L.size() - 1);
  ui.cur[ui.tab] = cur;
  int64_t pv[7][2];
  bool hasPv = !S.dead && popup.empty() && !ask.on && preview(L[cur], pv);

  // top-left: compact stat bars with the change the highlighted action would make
  const int BW = 62;
  int vals[3] = {S.hp, S.food, S.mood};
  uint16_t cols[3] = {C_HP, C_FOOD, C_MOOD};
  int icn[3] = {3, 2, 4};
  for (int i = 0; i < 3; i++) {
    int y = 4 + i * 13, v = vals[i];
    bitmap(IC[icn[i]], 10, 2, y - 1, 1, cols[i]);
    fill(15, y, BW, 8, v <= 0 ? C_ZERO : C_LINE);
    fill(15, y, (BW * v + 50) / 100, 8, cols[i]);
    if (hasPv) {
      int mid = (int)((pv[i][0] + pv[i][1]) / 2), t = std::max(0, std::min(100, v + mid));
      if (t > v) blendRect(15 + (BW * v + 50) / 100, y, (BW * (t - v) + 50) / 100, 8, cols[i], 89);
      if (t < v) blendRect(15 + (BW * t + 50) / 100, y, (BW * (v - t) + 50) / 100, 8, C_RED, 153);
    }
    text(std::to_string(v), 94, y + 8, v <= 20 ? C_RED : C_SUB, F10B, RIGHT);
    if (hasPv && (pv[i][0] || pv[i][1])) text(dtxt(pv[i]), 97, y + 8, dcol(pv[i]), F9B);
  }
  // top-right: money, bottles, reputation
  fill(129, 2, 1, 50, C_LINE);
  const int RX = 134;
  for (int i = 0; i < 4; i++) {
    int y = 12 + i * 13;
    std::string s;
    if (i == 0) { text("₴", RX, y, C_AMBER, F12B); s = fmt(S.uah); }
    if (i == 1) { text("$", RX, y, C_HP, F12B); s = fmt(S.usd); }
    if (i == 2) { bitmap(BOTTLE, 8, RX + 1, y - 8, 1, C_SUB); s = fmt(S.bot) + "/" + fmt(cap()); }
    if (i == 3) { bitmap(STAR, 8, RX - 1, y - 8, 1, C_FOOD); s = fmt(S.rep); }
    int w = text(s, RX + 10, y, C_TXT, F12B);
    int k = 3 + i;
    if (hasPv && (pv[k][0] || pv[k][1])) text(dtxt(pv[k]), RX + 13 + w, y - 1, dcol(pv[k]), F9B);
  }
  fill(0, 54, W, 1, C_LINE);

  // header: tab name, date, weather
  text(TABS[ui.tab], 4, 68, C_AMBER, F12B);
  uint16_t wcol = S.wxC < 0 ? C_COLD : S.wxT == 5 ? C_RED : C_SUB;
  std::string wt = std::string(WXN[S.wxT]) + " " + (S.wxC > 0 ? "+" : "") + std::to_string(S.wxC) + "°";
  int ww = textW(wt, F11);
  text(wt, 236, 68, wcol, F11B, RIGHT);
  text(std::string(WD[weekday(S.day)]) + " " + dd(S.day) + " · ", 232 - ww, 68, C_SUB, F11, RIGHT);

  // action list
  const int RH = 19, Y0 = 74, VIS = 10, n = (int)L.size();
  int top = std::max(0, std::min(cur - VIS + 1, n - VIS));
  std::string hint, desc;
  for (int i = 0; i < std::min(VIS, n); i++) {
    int idx = top + i, y = Y0 + i * RH;
    const Item& it = L[idx];
    bool sel = idx == cur;
    bool need = it.kind == K_LOCKED || (it.kind == K_ACT && !it.need.empty());
    bool poor = !it.owned && !need && it.kind == K_ACT && it.cost.any() && !canPay(it.cost);
    if (sel) fill(2, y, W - 4, RH - 2, C_AMBER);
    else if (it.kind != K_INFO) fill(2, y, W - 4, RH - 2, C_PANEL);
    uint16_t lc = sel ? C_SELTXT : need ? C_DIM : it.kind == K_INFO ? C_SUB : C_TXT;
    uint16_t pc = sel ? C_SELTXT : (it.owned || it.cur) ? C_HP : poor ? C_RED : need ? C_DIM : it.kind == K_INFO ? C_TXT : C_AMBER;
    int lx = 7;
    if (need) { lock(6, y + 4, sel ? C_SELTXT : C_DIM); lx = 17; }
    std::string price = it.owned ? (it.ownedPrice.empty() ? "✓ є" : it.ownedPrice) : it.price;
    int pw = textW(price, F11B);
    const Font& lf = it.kind == K_RESET ? F12B : F12;
    text(fit(it.label, W - lx - pw - 14, lf), lx, y + 13, it.kind == K_RESET && !sel ? C_RED : lc, lf);
    text(price, W - 7, y + 13, pc, F11B, RIGHT);
    if (sel && need && !(it.kind == K_LOCKED && !it.desc.empty())) hint = "Потрібно: " + (it.kind == K_LOCKED ? it.locked : it.need);
    if (sel && !it.desc.empty() && hint.empty()) desc = it.desc;
  }
  if (n > VIS) {
    int h = VIS * RH * VIS / n, yy = Y0 + (VIS * RH - h) * top / (n - VIS);
    fill(W - 2, yy, 2, h, C_LINE);
  }

  // message line
  fill(0, 266, W, 28, C_PANEL);
  const std::string& m = !hint.empty() ? hint : !desc.empty() ? desc : msg;
  uint16_t mc = !hint.empty() ? C_AMBER : !desc.empty() ? C_SUB : (msg.find("⚠") != std::string::npos ? C_RED : C_TXT);
  std::vector<std::string> ml = wrap(m, W - 8, F11);
  for (size_t i = 0; i < ml.size() && i < 2; i++) text(ml[i], 4, 278 + (int)i * 12, mc, F11);

  // scrolling tab bar
  const int TW = 32, TV = 7, TX = 8;
  ui.toff = std::max(0, std::min(ui.toff, NTABS - TV));
  if (ui.tab < ui.toff) ui.toff = ui.tab;
  if (ui.tab >= ui.toff + TV) ui.toff = ui.tab - TV + 1;
  for (int j = 0; j < TV; j++) {
    int i = ui.toff + j, x = TX + j * TW;
    bool s = i == ui.tab;
    fill(x + 1, 296, TW - 2, 24, s ? C_AMBER : C_BG);
    bitmap(IC[i], 10, x + (TW - 20) / 2, 298, 2, s ? C_SELTXT : C_DIM);
  }
  if (ui.toff > 0) tri(1, 308, 6, 303, 6, 313, C_SUB);
  if (ui.toff + TV < NTABS) tri(239, 308, 234, 303, 234, 313, C_SUB);
  fill(0, 295, W, 1, C_LINE);

  // dialogs
  if (ask.on) box("ДРУГ", ask.text, ask.opt, 2, ask.sel);
  else if (!popup.empty()) { std::string o[1] = {"OK"}; box("ПОДІЯ", popup, o, 1, 0); }
  if (S.dead) {
    std::string o[1] = {"OK — нова гра"};
    std::string b = std::string(S.deadWhy) + "\nПрожито днів: " + fmt(S.day) + "\nРекорд: " + fmt(S.best) + (S.won ? "\nПрезидент з дня " + fmt(S.won) : "");
    box("ГРУ ЗАВЕРШЕНО", b, o, 1, 0);
  }
}
