// Сокобан для кишенькової консолі.
// ▲▼◀▶ — ходити, Назад — хід назад, OK — меню рівня, довге Назад — вийти.
#include <Arduino.h>
#include "BomzhCore.h"
#include "levels.h"

using namespace gfx;

static const int MAXW = 20, MAXH = 20, HIST = 600;
static const uint16_t FLOOR = rgb(0x2a2f38), FLOOR2 = rgb(0x262b33), WALL = rgb(0x7a4a32), WALL_L = rgb(0x9a6244),
                      WALL_D = rgb(0x4e2e20), GOAL = rgb(0xff4d4f), BOX = rgb(0xd8a24a), BOX_D = rgb(0x9a6a2a),
                      BOX_OK = rgb(0x4cd964), BOX_OK_D = rgb(0x2a9a4a), MAN = rgb(0x4aa8ff);

struct Save { uint32_t magic; uint16_t cur; uint8_t solved[(NLEVELS + 7) / 8]; uint16_t bestMoves[NLEVELS]; };
static Save sv;

enum Mode { PLAY, MENU, SELECT, DONE };
static Mode mode = PLAY;
static int lw, lh, px, py, moves, pushes, face = K_DOWN, menuSel, selCur;
static char g[MAXH][MAXW];         // ' ' floor, '#' wall, '_' outside
static bool goal[MAXH][MAXW], box[MAXH][MAXW];
static uint8_t hist[HIST];         // bit 0-1 dir, bit 2 pushed
static int nh;
static bool newBest;

static bool solved(int i) { return sv.solved[i / 8] >> (i % 8) & 1; }
static int nSolved() { int n = 0; for (int i = 0; i < NLEVELS; i++) n += solved(i); return n; }

static void load(int i) {
  memset(g, '_', sizeof g); memset(goal, 0, sizeof goal); memset(box, 0, sizeof box);
  lw = lh = 0; moves = pushes = nh = 0; face = K_DOWN;
  int x = 0, y = 0;
  for (const char* p = LEVELS[i];; p++) {
    if (*p == '|' || !*p) { y++; x = 0; if (!*p) break; continue; }
    if (y >= MAXH || x >= MAXW) { x++; continue; }
    char c = *p;
    g[y][x] = c == '#' ? '#' : ' ';
    if (c == '.' || c == '*' || c == '+') goal[y][x] = true;
    if (c == '$' || c == '*') box[y][x] = true;
    if (c == '@' || c == '+') { px = x; py = y; }
    x++;
    if (x > lw) lw = x;
  }
  lh = y;
  // floor cells not reachable from the player are "outside" (drawn as background)
  static bool in[MAXH][MAXW];
  memset(in, 0, sizeof in);
  int qx[MAXW * MAXH], qy[MAXW * MAXH], h = 0, t = 0;
  qx[t] = px; qy[t++] = py; in[py][px] = true;
  while (h < t) {
    int cx = qx[h], cy = qy[h++];
    static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& d : D) {
      int nx = cx + d[0], ny = cy + d[1];
      if (nx < 0 || ny < 0 || nx >= MAXW || ny >= MAXH || in[ny][nx] || g[ny][nx] != ' ') continue;
      in[ny][nx] = true; qx[t] = nx; qy[t++] = ny;
    }
  }
  for (int yy = 0; yy < MAXH; yy++) for (int xx = 0; xx < MAXW; xx++) if (g[yy][xx] == ' ' && !in[yy][xx]) g[yy][xx] = '_';
}

static bool won() {
  for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) if (box[y][x] && !goal[y][x]) return false;
  return true;
}

static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};

static void step(int d) {
  face = d;
  int nx = px + DX[d], ny = py + DY[d];
  if (g[ny][nx] == '#') return;
  bool push = false;
  if (box[ny][nx]) {
    int bx = nx + DX[d], by = ny + DY[d];
    if (g[by][bx] != ' ' || box[by][bx]) return;
    box[ny][nx] = false; box[by][bx] = true; push = true; pushes++;
  }
  px = nx; py = ny; moves++;
  if (nh == HIST) { memmove(hist, hist + 1, HIST - 1); nh--; }
  hist[nh++] = d | (push ? 4 : 0);
  if (won()) {
    mode = DONE;
    int i = sv.cur;
    newBest = !sv.bestMoves[i] || moves < sv.bestMoves[i];
    if (newBest) sv.bestMoves[i] = moves;
    sv.solved[i / 8] |= 1 << (i % 8);
    saveData(&sv, sizeof sv);
  }
}

static void undo() {
  if (!nh) return;
  uint8_t h = hist[--nh];
  int d = h & 3;
  int bx = px + DX[d], by = py + DY[d];
  px -= DX[d]; py -= DY[d];
  if (h & 4) { box[by][bx] = false; box[py + DY[d]][px + DX[d]] = true; pushes--; }
  moves--;
  face = d;
}

static void drawLevel() {
  fill(0, 0, W, H, pal::BG);
  int t = 24;
  if (lw * t > 232) t = 232 / lw;
  if (lh * t > 268) t = 268 / lh;
  int ox = (W - lw * t) / 2, oy = 36 + (270 - lh * t) / 2;
  for (int y = 0; y < lh; y++)
    for (int x = 0; x < lw; x++) {
      int sx = ox + x * t, sy = oy + y * t;
      if (g[y][x] == '#') {
        fill(sx, sy, t, t, WALL);
        fill(sx, sy, t, 2, WALL_L);
        fill(sx, sy + t / 2, t, 1, WALL_D);
        fill(sx + ((y & 1) ? t / 3 : 2 * t / 3), sy + 2, 1, t / 2 - 2, WALL_D);
        fill(sx + ((y & 1) ? 2 * t / 3 : t / 3), sy + t / 2, 1, t / 2, WALL_D);
        fill(sx, sy + t - 1, t, 1, WALL_D);
      } else if (g[y][x] == ' ') {
        fill(sx, sy, t, t, (x + y) & 1 ? FLOOR : FLOOR2);
        if (goal[y][x]) {
          int c = t / 2, r = t / 5 + 1;
          tri(sx + c - r, sy + c, sx + c, sy + c - r, sx + c + r, sy + c, GOAL);
          tri(sx + c - r, sy + c, sx + c, sy + c + r, sx + c + r, sy + c, GOAL);
        }
      }
      if (box[y][x]) {
        bool ok = goal[y][x];
        int m = t / 10 + 1;
        fillRound(sx + m, sy + m, t - 2 * m, t - 2 * m, 2, ok ? BOX_OK_D : BOX_D);
        fillRound(sx + m + 2, sy + m + 2, t - 2 * m - 4, t - 2 * m - 4, 1, ok ? BOX_OK : BOX);
        uint16_t d = ok ? BOX_OK_D : BOX_D;
        for (int i = 3; i < t - 2 * m - 3; i++) {   // diagonal brace
          fill(sx + m + i, sy + m + i, 2, 1, d);
        }
      }
    }
  // player
  int sx = ox + px * t + t / 2, sy = oy + py * t + t / 2, r = t / 2 - 2;
  fillCircle(sx, sy, r, MAN);
  int ex = DX[face] * r / 3, ey = DY[face] * r / 3 - (face == K_DOWN ? 0 : 1);
  int e = r / 3 > 1 ? r / 3 : 1;
  fill(sx - r / 3 + ex - e / 2, sy - 1 + ey, e, e + 1, rgb(0xffffff));
  fill(sx + r / 3 + ex - e / 2, sy - 1 + ey, e, e + 1, rgb(0xffffff));

  char s[48];
  text("СОКОБАН", 10, 24, pal::AMBER, F16B);
  snprintf(s, sizeof s, "рівень %d / %d", sv.cur + 1, NLEVELS);
  text(s, W - 10, 16, pal::TXT, F11B, RIGHT);
  snprintf(s, sizeof s, "ходи %d · поштовхи %d", moves, pushes);
  text(s, W - 10, 30, pal::SUB, F10B, RIGHT);
  fill(0, 36, W, 1, pal::LINE);
  uiHint("Назад — хід назад · OK — меню");
}

static void drawSelect() {
  fill(0, 0, W, H, pal::BG);
  text("РІВНІ", 10, 26, pal::AMBER, F16B);
  char s[48];
  snprintf(s, sizeof s, "пройдено %d з %d", nSolved(), NLEVELS);
  text(s, W - 10, 24, pal::SUB, F11, RIGHT);
  fill(0, 36, W, 1, pal::LINE);
  const int C = 5, CW = 42, CH = 30, X0 = (W - C * CW - (C - 1) * 4) / 2;
  int rows = (NLEVELS + C - 1) / C, visRows = 7, top = selCur / C - visRows / 2;
  if (top > rows - visRows) top = rows - visRows;
  if (top < 0) top = 0;
  for (int i = top * C; i < NLEVELS && i < (top + visRows) * C; i++) {
    int r = i / C - top, c = i % C, x = X0 + c * (CW + 4), y = 46 + r * (CH + 4);
    bool sel = i == selCur;
    fillRound(x, y, CW, CH, 5, sel ? pal::AMBER : solved(i) ? rgb(0x1f3a28) : pal::PANEL);
    snprintf(s, sizeof s, "%d", i + 1);
    text(s, x + CW / 2, y + 20, sel ? pal::SELTXT : solved(i) ? pal::GREEN : pal::TXT, F13B, CENTER);
  }
  if (sv.bestMoves[selCur]) snprintf(s, sizeof s, "Рівень %d · найкраще %u ходів", selCur + 1, sv.bestMoves[selCur]);
  else snprintf(s, sizeof s, "Рівень %d · ще не пройдено", selCur + 1);
  text(s, W / 2, H - 26, pal::SUB, F11, CENTER);
  uiHint("OK — грати · Назад — повернутись");
}

static void draw() {
  if (mode == SELECT) { drawSelect(); return; }
  drawLevel();
  if (mode == MENU) {
    std::string o[2] = {"Заново", "Рівні"};
    uiDialog("МЕНЮ РІВНЯ", "Почати рівень заново або вибрати інший.\nНазад — повернутись до гри.", o, 2, menuSel);
  } else if (mode == DONE) {
    char b[96];
    snprintf(b, sizeof b, "Ходів: %d, поштовхів: %d\n%s", moves, pushes, newBest ? "Найкращий результат!" : "");
    std::string o[1] = {sv.cur + 1 < NLEVELS ? "Наступний" : "До рівнів"};
    uiDialog("ПРОЙДЕНО!", b, o, 1, 0);
  }
}

void setup() {
  consoleBegin("sokoban");
  inputSetRepeat(REPEAT_MENU, 450, 120);
  memset(&sv, 0, sizeof sv);
  sv.magic = 0x534f4b00 + NLEVELS;
  Save l;
  if (loadData(&l, sizeof l) && l.magic == sv.magic) sv = l;
  if (sv.cur >= NLEVELS) sv.cur = 0;
  load(sv.cur);
  draw();
  present();
}

void loop() {
  consolePoll();
  bool changed = false;
  for (int k; (k = inputGet()) >= 0;) {
    changed = true;
    if (k == K_BACK_LONG) {
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (mode == PLAY) {
      if (k <= K_RIGHT) step(k);
      else if (k == K_BACK) undo();
      else if (k == K_OK) { mode = MENU; menuSel = 0; }
    } else if (mode == MENU) {
      if (k <= K_RIGHT) menuSel ^= 1;
      else if (k == K_BACK) mode = PLAY;
      else if (k == K_OK) {
        if (menuSel == 0) { load(sv.cur); mode = PLAY; }
        else { mode = SELECT; selCur = sv.cur; }
      }
    } else if (mode == SELECT) {
      if (k == K_LEFT && selCur > 0) selCur--;
      if (k == K_RIGHT && selCur < NLEVELS - 1) selCur++;
      if (k == K_UP && selCur >= 5) selCur -= 5;
      if (k == K_DOWN && selCur + 5 < NLEVELS) selCur += 5;
      if (k == K_BACK) mode = PLAY;
      if (k == K_OK) { sv.cur = selCur; saveData(&sv, sizeof sv); load(sv.cur); mode = PLAY; }
    } else if (mode == DONE && k == K_OK) {
      if (sv.cur + 1 < NLEVELS) { sv.cur++; saveData(&sv, sizeof sv); load(sv.cur); mode = PLAY; }
      else { mode = SELECT; selCur = sv.cur; }
    }
  }
  if (changed) { draw(); present(); }
  delay(5);
}
