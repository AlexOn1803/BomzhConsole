// Хрестики-нолики без нічиїх: у кожного не більше трьох фігур. Коли ставиш четверту, твоя найстаріша зникає.
// Перед ходом фігура, що зникне, стає напівпрозорою — на її клітинку ставити не можна.
// ▲▼◀▶ — курсор, OK — поставити, Назад — меню, довге Назад — вийти.
#include <Arduino.h>
#include "BomzhCore.h"
#include <algorithm>

using namespace gfx;

static const int CS = 72, GAP = 6, BX = (240 - 3 * CS - 2 * GAP) / 2, BY = 70;
static const uint16_t XC = rgb(0x4aa8ff), OC = rgb(0xff6a5a), GRID = rgb(0x2a3444), CELL = rgb(0x161d28);

// a side's marks, oldest first
struct Q { int8_t c[3]; uint8_t n; };
struct Pos { Q q[2]; uint8_t turn; };   // turn 0 = X, 1 = O

static int8_t ownerAt(const Pos& p, int cell) {
  for (int s = 0; s < 2; s++) for (int i = 0; i < p.q[s].n; i++) if (p.q[s].c[i] == cell) return s;
  return -1;
}
static const int LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
static int winLine(const Pos& p, int side) {
  if (p.q[side].n < 3) return -1;
  for (int l = 0; l < 8; l++) {
    int hit = 0;
    for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) if (p.q[side].c[i] == LINES[l][k]) hit++;
    if (hit == 3) return l;
  }
  return -1;
}
static bool legal(const Pos& p, int cell) { return ownerAt(p, cell) < 0; }   // the fading mark still blocks its cell
static void play(Pos& p, int cell) {
  Q& q = p.q[p.turn];
  if (q.n == 3) { q.c[0] = q.c[1]; q.c[1] = q.c[2]; q.n = 2; }   // the oldest disappears
  q.c[q.n++] = cell;
  p.turn ^= 1;
}

// ---------- computer ----------
static int nodes;
static int search(const Pos& p, int depth, int alpha, int beta) {   // value for the side to move
  if ((++nodes & 4095) == 0) delay(1);   // keep the watchdog happy
  if (winLine(p, p.turn ^ 1) >= 0) return -1000 - depth;          // opponent just made a line
  if (depth == 0) {
    // small heuristic: lines still open for each side
    int s = 0;
    for (int side = 0; side < 2; side++)
      for (int l = 0; l < 8; l++) {
        int mine = 0, other = 0;
        for (int k = 0; k < 3; k++) { int o = ownerAt(p, LINES[l][k]); if (o == side) mine++; else if (o >= 0) other++; }
        if (!other) s += (side == p.turn ? 1 : -1) * mine * mine;
      }
    return s;
  }
  int best = -100000;
  static const int ORDER[9] = {4, 0, 2, 6, 8, 1, 3, 5, 7};
  for (int c : ORDER) {
    if (!legal(p, c)) continue;
    Pos n = p;
    play(n, c);
    int v = -search(n, depth - 1, -beta, -alpha);
    if (v > best) best = v;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;
  }
  return best;
}
static int aiMove(const Pos& p, int level) {
  int cand[9], nc = 0;
  for (int c = 0; c < 9; c++) if (legal(p, c)) cand[nc++] = c;
  if (!nc) return -1;
  if (level == 0 && randomU32() % 100 < 45) return cand[randomU32() % nc];   // easy: often just random
  static const int DEPTH[3] = {2, 5, 10};
  int best = -1000000, pick = cand[0];
  for (int i = nc - 1; i > 0; i--) std::swap(cand[i], cand[randomU32() % (i + 1)]);
  for (int i = 0; i < nc; i++) {
    Pos n = p;
    play(n, cand[i]);
    int v = -search(n, DEPTH[level] - 1, -1000000, 1000000);
    if (v > best) { best = v; pick = cand[i]; }
  }
  return pick;
}

// ---------- game ----------
struct Save { uint32_t magic; uint8_t mode, level; uint16_t wX, wO; uint16_t vsWin, vsLose; };
static Save sv;
static const uint32_t MAGIC = 0x54544331;
enum Screen { S_MENU, S_PLAY, S_THINK, S_OVER };
static Screen scr = S_MENU;
static Pos pos;
static int cur = 4, menuSel = 0, winner = -1, wline = -1, moves;
static uint32_t overAt, thinkAt;

static void newRound() { memset(&pos, 0, sizeof pos); pos.turn = (sv.wX + sv.wO) % 2 && sv.mode == 1 ? 1 : 0; winner = -1; wline = -1; moves = 0; cur = 4; scr = S_PLAY; if (sv.mode == 0) pos.turn = 0; }

static void afterMove() {
  moves++;
  int side = pos.turn ^ 1;
  int l = winLine(pos, side);
  if (l >= 0) {
    winner = side; wline = l; scr = S_OVER; overAt = nowMs();
    if (sv.mode == 0) { if (side == 0) { sv.vsWin++; sfxWin(); } else { sv.vsLose++; sfxLose(); } }
    else { if (side == 0) sv.wX++; else sv.wO++; sfxWin(); }
    saveData(&sv, sizeof sv);
    return;
  }
  if (sv.mode == 0 && pos.turn == 1) { scr = S_THINK; thinkAt = nowMs(); }
}

// ---------- drawing ----------
static void drawX(int cx, int cy, int r, uint16_t c) {
  for (int t = -4; t <= 4; t++) for (int i = -r; i <= r; i++) { fill(cx + i + t / 2, cy + i - t / 2, 2, 2, c); fill(cx + i + t / 2, cy - i + t / 2, 2, 2, c); }
}
static void drawO(int cx, int cy, int r, uint16_t c, uint16_t bg) { fillCircle(cx, cy, r, c); fillCircle(cx, cy, r - 8, bg); }

static void draw() {
  fill(0, 0, W, H, pal::BG);
  text("ХРЕСТИКИ-НОЛИКИ", W / 2, 22, pal::AMBER, F13B, CENTER);
  char s[96];
  static const char* LV[3] = {"легко", "нормально", "важко"};
  if (sv.mode == 0) snprintf(s, sizeof s, "Ти %u : %u комп'ютер · %s", sv.vsWin, sv.vsLose, LV[sv.level]);
  else snprintf(s, sizeof s, "Хрестики %u : %u Нолики", sv.wX, sv.wO);
  text(s, W / 2, 38, pal::SUB, F10B, CENTER);
  // the mark that will vanish on the current player's move
  int fading = -1;
  if ((scr == S_PLAY || scr == S_THINK) && pos.q[pos.turn].n == 3) fading = pos.q[pos.turn].c[0];
  for (int c = 0; c < 9; c++) {
    int x = BX + (c % 3) * (CS + GAP), y = BY + (c / 3) * (CS + GAP);
    bool inLine = wline >= 0 && (LINES[wline][0] == c || LINES[wline][1] == c || LINES[wline][2] == c);
    fillRound(x, y, CS, CS, 10, inLine ? mix565(CELL, winner == 0 ? XC : OC, 70) : CELL);
    int o = ownerAt(pos, c);
    if (o >= 0) {
      uint16_t col = o == 0 ? XC : OC;
      if (c == fading) col = mix565(col, CELL, 160);   // semi-transparent: disappears on this move
      if (o == 0) drawX(x + CS / 2, y + CS / 2, 20, col); else drawO(x + CS / 2, y + CS / 2, 24, col, inLine ? mix565(CELL, OC, 70) : CELL);
    }
    if (scr == S_PLAY && c == cur && !(sv.mode == 0 && pos.turn == 1)) {
      uint16_t cc = legal(pos, c) ? pal::AMBER : pal::RED;
      strokeRect(x, y, CS, CS, cc); strokeRect(x + 1, y + 1, CS - 2, CS - 2, cc); strokeRect(x + 2, y + 2, CS - 4, CS - 4, cc);
    }
  }
  // whose turn (under the title)
  int ty = 60;
  if (scr == S_PLAY || scr == S_THINK) {
    bool x = pos.turn == 0;
    const char* who = sv.mode == 0 ? (x ? "Твій хід" : "Комп'ютер думає…") : (x ? "Хід хрестиків" : "Хід ноликів");
    if (x) drawX(W / 2 - 60, ty - 5, 7, XC); else drawO(W / 2 - 60, ty - 5, 9, OC, pal::BG);
    text(who, W / 2 - 44, ty, pal::TXT, F12B);
  }
  uiHint(scr == S_PLAY ? "OK — поставити · Назад — меню" : "");
  if (scr == S_OVER && nowMs() - overAt > 500) {
    std::string o[1] = {"Ще раз"};
    const char* t = sv.mode == 0 ? (winner == 0 ? "ТИ ПЕРЕМІГ!" : "ПЕРЕМІГ КОМП'ЮТЕР") : (winner == 0 ? "ПЕРЕМОГЛИ ХРЕСТИКИ" : "ПЕРЕМОГЛИ НОЛИКИ");
    snprintf(s, sizeof s, "Три в ряд за %d ходів.", moves);
    uiDialog(t, s, o, 1, 0);
  }
  if (scr == S_MENU) {
    blendRect(0, 0, W, H, pal::SHADE, 170);
    fill(16, 70, 208, 190, pal::PANEL);
    strokeRect(16, 70, 208, 190, pal::AMBER);
    text("НОВА ГРА", W / 2, 90, pal::AMBER, F12B, CENTER);
    static const char* O[5] = {"Проти комп'ютера: легко", "Проти комп'ютера: нормально", "Проти комп'ютера: важко", "Удвох на одній консолі", "Продовжити"};
    for (int i = 0; i < 5; i++) {
      int y = 104 + i * 28;
      fill(28, y, 184, 22, i == menuSel ? pal::AMBER : pal::LINE);
      text(O[i], W / 2, y + 16, i == menuSel ? pal::SELTXT : pal::TXT, F11B, CENTER);
    }
    text("Лише 3 фігури в кожного — нічиїх немає", W / 2, 250, pal::DIM, F10B, CENTER);
  }
}

void setup() {
  consoleBegin("tictac");
  inputSetRepeat(REPEAT_MENU, 450, 120);
  memset(&sv, 0, sizeof sv);
  Save l;
  if (loadData(&l, sizeof l) && l.magic == MAGIC) sv = l;
  sv.magic = MAGIC;
  if (sv.level > 2) sv.level = 1;
  memset(&pos, 0, sizeof pos);
  scr = S_MENU; menuSel = sv.mode == 1 ? 3 : sv.level;
  draw();
  present();
}

void loop() {
  consolePoll();
  bool changed = false;
  for (int k; (k = inputGet()) >= 0;) {
    changed = true;
    if (k == K_BACK_LONG) { draw(); if (confirmExit()) { saveData(&sv, sizeof sv); returnToLauncher(); } continue; }
    if (scr == S_MENU) {
      if (k == K_UP) { menuSel = (menuSel + 4) % 5; sfxClick(); }
      if (k == K_DOWN) { menuSel = (menuSel + 1) % 5; sfxClick(); }
      if (k == K_BACK) { if (moves || pos.q[0].n) scr = S_PLAY; }
      if (k == K_OK) {
        sfxSelect();
        if (menuSel < 3) { sv.mode = 0; sv.level = menuSel; newRound(); }
        else if (menuSel == 3) { sv.mode = 1; newRound(); }
        else if (pos.q[0].n || moves) scr = S_PLAY; else { newRound(); }
        saveData(&sv, sizeof sv);
      }
    } else if (scr == S_PLAY) {
      if (sv.mode == 0 && pos.turn == 1) continue;
      if (k == K_UP && cur >= 3) { cur -= 3; sfxClick(); }
      if (k == K_DOWN && cur < 6) { cur += 3; sfxClick(); }
      if (k == K_LEFT && cur % 3) { cur--; sfxClick(); }
      if (k == K_RIGHT && cur % 3 < 2) { cur++; sfxClick(); }
      if (k == K_BACK) { scr = S_MENU; menuSel = 4; }
      if (k == K_OK) {
        if (legal(pos, cur)) { play(pos, cur); tone2(pos.turn == 1 ? 900 : 650, 50); afterMove(); }
        else sfxHit();   // occupied, including the fading mark
      }
    } else if (scr == S_OVER) {
      if (k == K_OK && nowMs() - overAt > 500) newRound();
      if (k == K_BACK) { scr = S_MENU; menuSel = 4; memset(&pos, 0, sizeof pos); moves = 0; }
    }
  }
  if (scr == S_THINK && nowMs() - thinkAt > 450) {   // a short pause so the move is visible
    int c = aiMove(pos, sv.level);
    if (c >= 0) { play(pos, c); tone2(650, 50); }
    scr = S_PLAY;
    afterMove();
    changed = true;
  }
  if (changed || scr == S_THINK || scr == S_OVER) { draw(); present(); }
  delay(10);
}
