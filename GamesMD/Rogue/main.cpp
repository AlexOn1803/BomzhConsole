// Підземелля — roguelike для кишенькової консолі.
// ▲▼◀▶ — ходити й битися (врізайся в монстра), OK — дії, Назад — пропустити хід, довге Назад — меню.
// Гра зберігається сама; смерть — назавжди.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>
#include <stdarg.h>
#include <algorithm>

using namespace gfx;

static const int MW = 40, MH = 40, TS = 12, VW = 20, VH = 21, VY = 38, NMON = 28, NITEM = 24;
enum Tile : uint8_t { T_WALL, T_FLOOR, T_STAIRS };
static const uint8_t SEEN = 0x80;

struct MonType { char ch; const char* name; uint8_t hp, atk, def, xp, minD; uint32_t color; };
static const MonType MT[] = {
  {'r', "щур", 3, 2, 0, 1, 1, 0xb0906a}, {'b', "кажан", 4, 2, 0, 2, 1, 0x9a7ad0}, {'g', "гоблін", 7, 3, 1, 4, 2, 0x6cc04a},
  {'s', "скелет", 10, 4, 2, 6, 3, 0xe8e4d0}, {'o', "орк", 14, 5, 2, 9, 5, 0x4a9a3a}, {'T', "троль", 24, 7, 3, 16, 7, 0x6a8a6a},
  {'D', "дракон", 40, 10, 5, 40, 10, 0xff5a3a},
};
static const int NMT = sizeof MT / sizeof MT[0];
enum ItemKind : uint8_t { I_NONE, I_GOLD, I_POTION, I_SCROLL, I_WEAPON, I_ARMOR };

struct Mon { uint8_t type, x, y, hp, awake; };
struct Item { uint8_t kind, x, y, amount; };
struct Game {
  uint32_t magic;
  uint8_t map[MH][MW];
  Mon mon[NMON];
  Item item[NITEM];
  uint8_t px, py, hp, maxHp, atk, def, lvl, depth, potions, scrolls, alive;
  uint16_t xp, gold, turn, kills;
  uint32_t best, bestDepth, games;   // records survive death
};
static Game gm;
static const uint32_t MAGIC = 0x524f4731;

static char msg[2][128];
static uint32_t msgAt;
static bool vis[MH][MW];
static bool dirty;
static uint32_t lastChange;
enum Mode { M_TITLE, M_PLAY, M_MENU, M_DEAD };
static Mode mode = M_TITLE;
static int menuSel, menuN;
static const char* menuItems[5];
static int menuAct[5];

static int rnd(int n) { return n > 0 ? (int)(randomU32() % (uint32_t)n) : 0; }
static void say(const char* fmt, ...) {
  strcpy(msg[0], msg[1]);
  va_list a; va_start(a, fmt); vsnprintf(msg[1], sizeof msg[1], fmt, a); va_end(a);
  msgAt = nowMs();
}
static uint8_t tile(int x, int y) { return x < 0 || y < 0 || x >= MW || y >= MH ? T_WALL : gm.map[y][x] & 0x7f; }
static Mon* monAt(int x, int y) { for (auto& m : gm.mon) if (m.hp && m.x == x && m.y == y) return &m; return nullptr; }
static Item* itemAt(int x, int y) { for (auto& i : gm.item) if (i.kind && i.x == x && i.y == y) return &i; return nullptr; }
static bool freeCell(int x, int y) { return tile(x, y) != T_WALL && !monAt(x, y) && !(x == gm.px && y == gm.py); }

// ---------- level generation: rooms joined by L-shaped corridors ----------
struct Room { int x, y, w, h; };
static void carve(int x, int y) { if (x > 0 && y > 0 && x < MW - 1 && y < MH - 1 && (gm.map[y][x] & 0x7f) == T_WALL) gm.map[y][x] = T_FLOOR; }
static void randomFloor(const Room& r, int& x, int& y) { x = r.x + rnd(r.w); y = r.y + rnd(r.h); }

static void genLevel() {
  memset(gm.map, T_WALL, sizeof gm.map);
  memset(gm.mon, 0, sizeof gm.mon);
  memset(gm.item, 0, sizeof gm.item);
  Room rooms[10];
  int n = 0;
  for (int tries = 0; tries < 300 && n < 10; tries++) {
    Room r = {0, 0, 4 + rnd(6), 3 + rnd(5)};
    r.x = 1 + rnd(MW - r.w - 2); r.y = 1 + rnd(MH - r.h - 2);
    bool ok = true;
    for (int i = 0; i < n && ok; i++)
      if (r.x <= rooms[i].x + rooms[i].w + 1 && rooms[i].x <= r.x + r.w + 1 && r.y <= rooms[i].y + rooms[i].h + 1 && rooms[i].y <= r.y + r.h + 1) ok = false;
    if (!ok) continue;
    rooms[n++] = r;
    for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) carve(x, y);
  }
  for (int i = 1; i < n; i++) {
    int ax = rooms[i - 1].x + rooms[i - 1].w / 2, ay = rooms[i - 1].y + rooms[i - 1].h / 2;
    int bx = rooms[i].x + rooms[i].w / 2, by = rooms[i].y + rooms[i].h / 2;
    if (rnd(2)) { for (int x = std::min(ax, bx); x <= std::max(ax, bx); x++) carve(x, ay); for (int y = std::min(ay, by); y <= std::max(ay, by); y++) carve(bx, y); }
    else { for (int y = std::min(ay, by); y <= std::max(ay, by); y++) carve(ax, y); for (int x = std::min(ax, bx); x <= std::max(ax, bx); x++) carve(x, by); }
  }
  // a couple of extra loops so it is not a single chain
  for (int k = 0; k < 2 && n > 3; k++) {
    int a = rnd(n), b = rnd(n);
    int ax = rooms[a].x + rooms[a].w / 2, ay = rooms[a].y + rooms[a].h / 2, bx = rooms[b].x + rooms[b].w / 2, by = rooms[b].y + rooms[b].h / 2;
    for (int x = std::min(ax, bx); x <= std::max(ax, bx); x++) carve(x, ay);
    for (int y = std::min(ay, by); y <= std::max(ay, by); y++) carve(bx, y);
  }
  int x, y;
  randomFloor(rooms[0], x, y); gm.px = x; gm.py = y;
  randomFloor(rooms[n - 1], x, y); gm.map[y][x] = T_STAIRS;
  // monsters
  int d = gm.depth, nm = std::min(NMON, 3 + d + d / 2 + rnd(3));
  for (int i = 0; i < nm; i++) {
    int ri = 1 + rnd(n - 1);
    randomFloor(rooms[ri], x, y);
    if (!freeCell(x, y)) continue;
    int t;
    do { t = rnd(NMT); } while (MT[t].minD > d || (MT[t].minD + 6 < d && rnd(3)));   // deeper = tougher mix
    if (t == 6 && rnd(4)) t = 5;
    gm.mon[i] = {(uint8_t)t, (uint8_t)x, (uint8_t)y, (uint8_t)(MT[t].hp + d / 3), 0};
  }
  // items
  int ni = std::min(NITEM, 5 + rnd(4));
  for (int i = 0; i < ni; i++) {
    randomFloor(rooms[rnd(n)], x, y);
    if (itemAt(x, y) || tile(x, y) == T_STAIRS) continue;
    int r = rnd(100);
    uint8_t k = r < 40 ? I_GOLD : r < 68 ? I_POTION : r < 80 ? I_SCROLL : r < 90 ? I_WEAPON : I_ARMOR;
    gm.item[i] = {k, (uint8_t)x, (uint8_t)y, (uint8_t)(k == I_GOLD ? 5 + rnd(6) + d * 3 : 1)};
  }
}

static void newGame() {
  uint32_t best = gm.best, bestDepth = gm.bestDepth, games = gm.games;
  memset(&gm, 0, sizeof gm);
  gm.magic = MAGIC; gm.best = best; gm.bestDepth = bestDepth; gm.games = games;
  gm.hp = gm.maxHp = 20; gm.atk = 3; gm.def = 0; gm.lvl = 1; gm.depth = 1; gm.potions = 2; gm.alive = 1;
  genLevel();
  msg[0][0] = 0;
  say("Ти спускаєшся в підземелля. Шукай сходи >");
  dirty = true;
}

// ---------- field of view ----------
static void computeFov() {
  memset(vis, 0, sizeof vis);
  const int R = 7;
  for (int ty = gm.py - R; ty <= gm.py + R; ty++)
    for (int tx = gm.px - R; tx <= gm.px + R; tx++) {
      if (tx < 0 || ty < 0 || tx >= MW || ty >= MH) continue;
      int dx = tx - gm.px, dy = ty - gm.py;
      if (dx * dx + dy * dy > R * R + R) continue;
      // walk the line from the player; stop at the first wall (the wall itself is visible)
      int x = gm.px, y = gm.py, sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, ax = abs(dx), ay = abs(dy), err = ax - ay;
      for (;;) {
        vis[y][x] = true; gm.map[y][x] |= SEEN;
        if (x == tx && y == ty) break;
        if ((x != gm.px || y != gm.py) && tile(x, y) == T_WALL) break;
        int e2 = 2 * err;
        if (e2 > -ay) { err -= ay; x += sx; }
        if (e2 < ax) { err += ax; y += sy; }
      }
    }
}

// ---------- turns ----------
static void die(const char* by) {
  gm.alive = 0;
  uint32_t score = gm.gold + gm.depth * 50 + gm.kills * 5;
  if (score > gm.best) gm.best = score;
  if (gm.depth > gm.bestDepth) gm.bestDepth = gm.depth;
  gm.games++;
  say("Тебе вбив %s.", by);
  mode = M_DEAD; sfxLose();
  saveData(&gm, sizeof gm);
  dirty = false;
}

static void monstersAct() {
  for (auto& m : gm.mon) {
    if (!m.hp) continue;
    const MonType& t = MT[m.type];
    int dx = gm.px - m.x, dy = gm.py - m.y;
    if (vis[m.y][m.x]) m.awake = 1;
    if (!m.awake) continue;
    if (abs(dx) <= 1 && abs(dy) <= 1 && (dx == 0 || dy == 0)) {   // adjacent (no diagonals): attack
      int dmg = std::max(1, t.atk + rnd(3) - 1 - gm.def);
      if (gm.hp <= dmg) { gm.hp = 0; die(t.name); return; }
      gm.hp -= dmg; sfxHit();
      say("%s б'є тебе: −%d", t.name, dmg);
      continue;
    }
    int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    int opts[3][2];
    int no = 0;
    if (m.type == 1 && rnd(2)) { static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; int k = rnd(4); opts[no][0] = D[k][0]; opts[no++][1] = D[k][1]; }
    else if (abs(dx) > abs(dy)) { opts[no][0] = sx; opts[no++][1] = 0; if (sy) { opts[no][0] = 0; opts[no++][1] = sy; } }
    else { opts[no][0] = 0; opts[no++][1] = sy; if (sx) { opts[no][0] = sx; opts[no++][1] = 0; } }
    for (int i = 0; i < no; i++) {
      int nx = m.x + opts[i][0], ny = m.y + opts[i][1];
      if ((opts[i][0] || opts[i][1]) && freeCell(nx, ny)) { m.x = nx; m.y = ny; break; }
    }
  }
}

static void gainXp(int xp) {
  gm.xp += xp;
  while (gm.xp >= gm.lvl * 10) {
    gm.xp -= gm.lvl * 10;
    gm.lvl++;
    gm.maxHp += 5; gm.hp = std::min<int>(gm.hp + 8, gm.maxHp); gm.atk++;
    say("Новий рівень %d! Сила й здоров'я зросли.", gm.lvl);
  }
}

static void endTurn() {
  gm.turn++;
  if (gm.turn % 8 == 0 && gm.hp < gm.maxHp) gm.hp++;
  monstersAct();
  if (gm.alive) computeFov();
  dirty = true;
  lastChange = nowMs();
}

static void pickUp() {
  Item* it = itemAt(gm.px, gm.py);
  if (!it) return;
  sfxCoin();
  switch (it->kind) {
    case I_GOLD: gm.gold += it->amount; say("Золото: +%d", it->amount); break;
    case I_POTION: gm.potions++; say("Зілля здоров'я (маєш %d)", gm.potions); break;
    case I_SCROLL: gm.scrolls++; say("Сувій телепорту (маєш %d)", gm.scrolls); break;
    case I_WEAPON: gm.atk++; say("Кращий меч! Атака %d", gm.atk); break;
    case I_ARMOR: gm.def++; say("Обладунок! Захист %d", gm.def); break;
  }
  it->kind = I_NONE;
}

static void tryMove(int dx, int dy) {
  int nx = gm.px + dx, ny = gm.py + dy;
  if (Mon* m = monAt(nx, ny)) {
    const MonType& t = MT[m->type];
    int dmg = std::max(1, gm.atk + rnd(3) - 1 - t.def);
    m->awake = 1;
    if (m->hp <= dmg) {
      m->hp = 0; gm.kills++; sfxCoin();
      say("Ти вбив: %s (+%d досвіду)", t.name, t.xp);
      gainXp(t.xp);
      if (!rnd(4) && !itemAt(nx, ny)) for (auto& i : gm.item) if (!i.kind) { i = {I_GOLD, (uint8_t)nx, (uint8_t)ny, (uint8_t)(3 + gm.depth * 2)}; break; }
    } else { m->hp -= dmg; say("Ти б'єш: %s −%d", t.name, dmg); sfxClick(); }
    endTurn();
    return;
  }
  if (tile(nx, ny) == T_WALL) return;   // bumping a wall costs no turn
  gm.px = nx; gm.py = ny;
  pickUp();
  if (tile(nx, ny) == T_STAIRS) say("Сходи вниз. OK — спуститися.");
  endTurn();
}

static void descend() {
  gm.depth++; sfxWin();
  if (gm.depth > gm.bestDepth) gm.bestDepth = gm.depth;
  genLevel();
  computeFov();
  say("Глибина %d. Тут небезпечніше.", gm.depth);
  saveData(&gm, sizeof gm);
  dirty = false;
}

static void quaff() {
  if (!gm.potions) { say("Зілля немає."); return; }
  gm.potions--;
  int h = 12 + gm.depth * 2;
  gm.hp = std::min<int>(gm.hp + h, gm.maxHp);
  say("Ти випив зілля: +%d здоров'я", h);
  endTurn();
}
static void teleport() {
  if (!gm.scrolls) { say("Сувоїв немає."); return; }
  gm.scrolls--;
  for (int t = 0; t < 500; t++) {
    int x = rnd(MW), y = rnd(MH);
    if (tile(x, y) == T_FLOOR && freeCell(x, y) && abs(x - gm.px) + abs(y - gm.py) > 8) { gm.px = x; gm.py = y; break; }
  }
  say("Спалах — і ти деінде!");
  endTurn();
}

// ---------- drawing ----------
static const uint16_t WALL = rgb(0x5a5248), WALL_T = rgb(0x7a7064), FLOOR = rgb(0x1c1f26), DOT = rgb(0x2e333c);

static void drawTile(int x, int y, int sx, int sy) {
  uint8_t raw = gm.map[y][x];
  if (!(raw & SEEN)) return;
  bool v = vis[y][x];
  uint8_t t = raw & 0x7f;
  auto dimc = [&](uint16_t c) { return v ? c : mix565(c, pal::BG, 115); };
  if (t == T_WALL) {
    fill(sx, sy, TS, TS, dimc(WALL));
    if (tile(x, y + 1) != T_WALL) fill(sx, sy + TS - 3, TS, 3, dimc(mix565(WALL, rgb(0), 90)));
    fill(sx, sy, TS, 2, dimc(WALL_T));
    fill(sx + (y & 1 ? 3 : 8), sy + 2, 1, 4, dimc(mix565(WALL, rgb(0), 70)));
  } else {
    fill(sx, sy, TS, TS, dimc(FLOOR));
    fill(sx + 5, sy + 5, 2, 2, dimc(DOT));
    if (t == T_STAIRS) {
      for (int i = 0; i < 4; i++) fill(sx + 2 + i * 2, sy + 2 + i * 2, TS - 4 - i * 2, 2, dimc(rgb(0xc8b070)));
    }
  }
}

static void glyph(char c, int sx, int sy, uint16_t col) {
  char s[2] = {c, 0};
  text(s, sx + TS / 2, sy + 10, col, F12B, CENTER);
}

static void drawPlay() {
  fill(0, 0, W, H, pal::BG);
  int cx = std::max(0, std::min(MW - VW, gm.px - VW / 2)), cy = std::max(0, std::min(MH - VH, gm.py - VH / 2));
  for (int y = 0; y < VH; y++) for (int x = 0; x < VW; x++) drawTile(cx + x, cy + y, x * TS, VY + y * TS);
  for (auto& it : gm.item) {
    if (!it.kind || it.x < cx || it.y < cy || it.x >= cx + VW || it.y >= cy + VH || !(gm.map[it.y][it.x] & SEEN)) continue;
    int sx = (it.x - cx) * TS, sy = VY + (it.y - cy) * TS;
    static const char C[] = " $!?)[";
    static const uint32_t COL[] = {0, 0xffcc00, 0xff4d6a, 0xd8d0ff, 0x8ad0ff, 0xb0b8c8};
    uint16_t c = rgb(COL[it.kind]);
    glyph(C[it.kind], sx, sy, vis[it.y][it.x] ? c : mix565(c, pal::BG, 150));
  }
  for (auto& m : gm.mon) {
    if (!m.hp || !vis[m.y][m.x] || m.x < cx || m.y < cy || m.x >= cx + VW || m.y >= cy + VH) continue;
    int sx = (m.x - cx) * TS, sy = VY + (m.y - cy) * TS;
    glyph(MT[m.type].ch, sx, sy, rgb(MT[m.type].color));
    int hp = MT[m.type].hp + gm.depth / 3;
    if (m.hp < hp) fill(sx + 1, sy, (TS - 2) * m.hp / hp, 1, pal::RED);
  }
  int psx = (gm.px - cx) * TS, psy = VY + (gm.py - cy) * TS;
  fill(psx, psy, TS, TS, rgb(0x2a3448));
  glyph('@', psx, psy, pal::AMBER);

  // HUD
  char s[128];
  int bw = 90;
  fill(8, 8, bw, 8, rgb(0x3a1a1e));
  fill(8, 8, bw * gm.hp / std::max<int>(1, gm.maxHp), 8, gm.hp * 3 < gm.maxHp ? pal::RED : rgb(0xe0505a));
  snprintf(s, sizeof s, "%d/%d", gm.hp, gm.maxHp);
  text(s, 8 + bw + 6, 16, pal::TXT, F11B);
  snprintf(s, sizeof s, "глибина %d", gm.depth);
  text(s, W - 8, 16, pal::AMBER, F11B, RIGHT);
  snprintf(s, sizeof s, "рів %d  ат %d  зах %d  $%d  !%d  ?%d", gm.lvl, gm.atk, gm.def, gm.gold, gm.potions, gm.scrolls);
  text(s, 8, 31, pal::SUB, F10B);
  fill(0, VY - 2, W, 1, pal::LINE);
  // xp bar
  fill(0, VY - 2, W * gm.xp / (gm.lvl * 10), 1, pal::BLUE);
  // message log
  fill(0, H - 34, W, 34, pal::PANEL);
  text(fit(msg[0], W - 12, F11), 6, H - 21, pal::DIM, F11);
  text(fit(msg[1], W - 12, F11), 6, H - 6, nowMs() - msgAt < 1500 ? pal::TXT : pal::SUB, F11);
}

static void buildMenu() {
  menuN = 0;
  if (tile(gm.px, gm.py) == T_STAIRS) { menuItems[menuN] = "Спуститися сходами"; menuAct[menuN++] = 0; }
  menuItems[menuN] = "Випити зілля"; menuAct[menuN++] = 1;
  menuItems[menuN] = "Прочитати сувій телепорту"; menuAct[menuN++] = 2;
  menuItems[menuN] = "Відпочити 10 ходів"; menuAct[menuN++] = 3;
  menuItems[menuN] = "Закрити"; menuAct[menuN++] = 4;
  menuSel = 0;
}

static void drawMenu() {
  blendRect(0, 0, W, H, pal::SHADE, 170);
  int h = 36 + menuN * 26, y = (H - h) / 2;
  fill(20, y, 200, h, pal::PANEL);
  strokeRect(20, y, 200, h, pal::AMBER);
  text("ДІЇ", W / 2, y + 20, pal::AMBER, F12B, CENTER);
  for (int i = 0; i < menuN; i++) {
    int iy = y + 30 + i * 26;
    bool sel = i == menuSel;
    if (sel) fill(26, iy, 188, 22, pal::AMBER);
    char s[128];
    if (menuAct[i] == 1) snprintf(s, sizeof s, "%s (%d)", menuItems[i], gm.potions);
    else if (menuAct[i] == 2) snprintf(s, sizeof s, "Сувій телепорту (%d)", gm.scrolls);
    else snprintf(s, sizeof s, "%s", menuItems[i]);
    text(s, 34, iy + 16, sel ? pal::SELTXT : pal::TXT, F12B);
  }
}

static void draw() {
  drawPlay();
  if (mode == M_MENU) drawMenu();
  else if (mode == M_TITLE) {
    std::string o[1] = {gm.turn ? "OK — продовжити" : "OK — грати"};
    char b[400];
    snprintf(b, sizeof b, "▲▼◀▶ — ходити, врізайся в монстра, щоб битися.\nOK — дії (зілля, сувій, сходи). Назад — пропустити хід.\nРекорд: %u очок, глибина %u.", (unsigned)gm.best, (unsigned)gm.bestDepth);
    uiDialog("ПІДЗЕМЕЛЛЯ", b, o, 1, 0);
  } else if (mode == M_DEAD) {
    std::string o[1] = {"OK — нова гра"};
    char b[400];
    uint32_t score = gm.gold + gm.depth * 50 + gm.kills * 5;
    snprintf(b, sizeof b, "%s\nГлибина %d, рівень %d, вбито %d, золото %d.\nОчки: %u · рекорд %u", msg[1], gm.depth, gm.lvl, gm.kills, gm.gold, (unsigned)score, (unsigned)gm.best);
    uiDialog("ТИ ЗАГИНУВ", b, o, 1, 0);
  }
}

void setup() {
  consoleBegin("rogue");
  inputSetRepeat(REPEAT_MENU, 450, 140);
  Game* l = new Game;
  if (loadData(l, sizeof *l) && l->magic == MAGIC) gm = *l;
  else { memset(&gm, 0, sizeof gm); gm.magic = MAGIC; }
  delete l;
  if (gm.alive) say("З поверненням! Глибина %d.", gm.depth);
  else newGame();             // keeps the records
  computeFov();
  draw();
  present();
}

void loop() {
  consolePoll();
  bool changed = false;
  for (int k; (k = inputGet()) >= 0;) {
    changed = true;
    if (k == K_BACK_LONG) {
      if (dirty && gm.alive) { saveData(&gm, sizeof gm); dirty = false; }
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (mode == M_TITLE) {
      if (k == K_OK) mode = M_PLAY;
    } else if (mode == M_DEAD) {
      if (k == K_OK) { newGame(); computeFov(); mode = M_PLAY; }
    } else if (mode == M_MENU) {
      if (k == K_UP) menuSel = (menuSel + menuN - 1) % menuN;
      if (k == K_DOWN) menuSel = (menuSel + 1) % menuN;
      if (k == K_BACK) mode = M_PLAY;
      if (k == K_OK) {
        mode = M_PLAY;
        switch (menuAct[menuSel]) {
          case 0: descend(); break;
          case 1: quaff(); break;
          case 2: teleport(); break;
          case 3:
            for (int i = 0; i < 10 && gm.alive; i++) {
              bool seen = false;
              for (auto& m : gm.mon) if (m.hp && vis[m.y][m.x]) seen = true;
              if (seen) { say("Поруч ворог — не до відпочинку!"); break; }
              endTurn();
            }
            if (gm.alive && mode == M_PLAY) say("Ти відпочив. Здоров'я %d/%d", gm.hp, gm.maxHp);
            break;
        }
      }
    } else if (mode == M_PLAY) {
      if (k == K_UP) tryMove(0, -1);
      if (k == K_DOWN) tryMove(0, 1);
      if (k == K_LEFT) tryMove(-1, 0);
      if (k == K_RIGHT) tryMove(1, 0);
      if (k == K_BACK) { say("Ти чекаєш."); endTurn(); }
      if (k == K_OK) {
        if (tile(gm.px, gm.py) == T_STAIRS) descend();
        else { buildMenu(); mode = M_MENU; }
      }
    }
  }
  if (nowMs() - msgAt < 1600) changed = true;   // message fades from bright to normal
  if (changed) { draw(); present(); }
  if (dirty && gm.alive && nowMs() - lastChange > 2500) { saveData(&gm, sizeof gm); dirty = false; }
  delay(8);
}
