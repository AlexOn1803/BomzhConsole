// Космошахтар — копай вглиб планети, збирай руди, повертайся на станцію.
// ◀▶▼ — їхати й бурити, ▲ — злітати (витрачає паливо), OK — предмети, Назад — меню, довге Назад — вийти.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>
#include <algorithm>

using namespace gfx;

static const int MW = 15, MH = 220, TS = 16, VY = 34, VH = 16;   // map 15 × 220 tiles, view 15 × 16
static const int SURF = 4;                                       // first diggable row
enum Tile : uint8_t { T_AIR, T_DIRT, T_ROCK, T_LAVA, T_GAS, T_ORE0, T_CORE = T_ORE0 + 9, T_BASE_FUEL, T_BASE_SHOP, T_BASE_SELL, T_BEDROCK };
struct Ore { const char* name; uint16_t price; uint8_t weight; uint8_t minDepth; uint32_t color; };
static const Ore ORES[9] = {
  {"Залізо", 25, 1, 0, 0xb87a5a},   {"Мідь", 45, 1, 8, 0xe0803a},    {"Срібло", 100, 1, 25, 0xd8e0e8},
  {"Золото", 250, 1, 45, 0xffd040},  {"Платина", 500, 1, 70, 0xa8c8ff}, {"Смарагд", 900, 1, 95, 0x40e070},
  {"Рубін", 1600, 1, 120, 0xff3a5a}, {"Діамант", 3000, 1, 150, 0xa0ffff}, {"Кристал Е", 6000, 1, 180, 0xff60ff},
};

struct Up { const char* name; uint16_t base[4]; };   // upgrade levels 0..4, prices to go to level 1..4
static const char* UPNAME[5] = {"Бур", "Корпус", "Бак", "Трюм", "Двигун"};
static const uint32_t UPPRICE[5][4] = {{750, 3000, 10000, 30000}, {600, 2500, 9000, 25000}, {500, 2000, 7000, 20000},
                                       {600, 2500, 8000, 22000}, {700, 2800, 9000, 26000}};

struct Game {
  uint32_t magic;
  uint8_t map[MH][MW];
  uint8_t px, face, up[5];     // up: drill, hull, tank, cargo, engine levels
  uint16_t py;
  int32_t money;
  float fuel, hull;
  uint8_t cargo[9];
  uint8_t dynamite, teleports, kits, cans;
  uint16_t maxDepth, trips;
  uint8_t won;
};
static Game g;
static const uint32_t MAGIC = 0x4d4e5231;

static float maxFuel() { return 10 + g.up[2] * 8; }      // litres
static float maxHull() { return 30 + g.up[1] * 25; }
static int maxCargo() { return 10 + g.up[3] * 8; }
static int cargoCount() { int n = 0; for (auto c : g.cargo) n += c; return n; }
static float drillTime(uint8_t t) {                        // seconds to dig one tile
  float base = t == T_ROCK ? 99 : 0.55f;
  if (t >= T_ORE0 && t < T_CORE) base = 0.65f + (t - T_ORE0) * 0.05f;
  if (t == T_CORE) base = 1.5f;
  float depthK = 1 + g.py / 120.0f;
  return base * depthK / (1 + g.up[0] * 0.6f);
}

enum Mode { M_PLAY, M_SHOP, M_MENU, M_ITEMS, M_DEAD, M_WIN, M_MSG };
static Mode mode = M_PLAY;
static int sel;
static char msg[200];
static uint32_t msgUntil;
// motion state: the pod moves smoothly between tiles
static float ox, oy;          // pixel offset of the animation (towards the target tile)
static int act = 0;           // 0 idle, 1 moving, 2 digging, 3 falling, 4 flying
static int adx, ady;          // direction of the current action
static float actT, actDur;
static int fallTiles;
static uint32_t lastT, saveAt;
static bool dirty;
static int camY = 0;

static int rnd(int n) { return (int)(randomU32() % (uint32_t)n); }
static void say(const char* s, uint32_t ms = 2600) { strncpy(msg, s, sizeof msg - 1); msgUntil = nowMs() + ms; }
static uint8_t& at(int x, int y) { return g.map[y][x]; }
static bool inMap(int x, int y) { return x >= 0 && y >= 0 && x < MW && y < MH; }

// ---------- world generation ----------
static void genWorld() {
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++) {
      uint8_t t;
      if (y < SURF) t = T_AIR;
      else if (y == MH - 1) t = T_BEDROCK;
      else {
        int d = y - SURF, r = rnd(1000);
        t = T_DIRT;
        int rockP = 40 + d / 2, lavaP = d > 60 ? 8 + d / 25 : 0, gasP = d > 30 ? 6 + d / 30 : 0, air = 50;
        if (r < rockP) t = T_ROCK;
        else if (r < rockP + lavaP) t = T_LAVA;
        else if (r < rockP + lavaP + gasP) t = T_GAS;
        else if (r < rockP + lavaP + gasP + air && d > 3) t = T_AIR;
        else if (r < rockP + lavaP + gasP + air + 110) {
          // ores: the deeper, the richer; pick among ores allowed at this depth, weighted to the newest
          int best = 0;
          for (int i = 0; i < 9; i++) if (d >= ORES[i].minDepth) best = i;
          int pick = best - std::min(best, rnd(4) == 0 ? rnd(best + 1) : rnd(3));
          t = T_ORE0 + pick;
        }
      }
      at(x, y) = t;
    }
  for (int x = 0; x < MW; x++) at(x, SURF) = (rnd(5) == 0) ? T_ORE0 : T_DIRT;   // first layer: easy
  // the station on the surface (row SURF-1 sits on the ground)
  at(2, SURF - 1) = T_BASE_FUEL;
  at(7, SURF - 1) = T_BASE_SELL;
  at(12, SURF - 1) = T_BASE_SHOP;
  // the goal: an alien core near the bottom
  int cx = 3 + rnd(9);
  at(cx, MH - 4) = T_CORE;
}

static void newGame() {
  memset(&g, 0, sizeof g);
  g.magic = MAGIC;
  genWorld();
  g.px = 5; g.py = SURF - 1; g.face = 1;
  g.money = 200; g.fuel = maxFuel(); g.hull = maxHull();
  g.dynamite = 1; g.teleports = 1;
  say("Копай вниз (▼), збирай руду, повертайся (▲) на станцію продати її й заправитись.", 4000);
}

// ---------- helpers ----------
static bool solidAt(int x, int y) {   // something you stand on / can't pass without digging
  if (!inMap(x, y)) return true;
  uint8_t t = at(x, y);
  return !(t == T_AIR || t == T_BASE_FUEL || t == T_BASE_SHOP || t == T_BASE_SELL);
}

static void damage(float d, const char* why) {
  g.hull -= d;
  char b[96];
  if (g.hull <= 0) {
    g.hull = 0;
    snprintf(b, sizeof b, "Машину знищено: %s", why);
    sfxLose();
    say(b, 6000);
    mode = M_DEAD;
    return;
  }
  snprintf(b, sizeof b, "%s: −%d міцності", why, (int)d);
  sfxHit();
  say(b);
}

static void collect(uint8_t t) {
  if (t >= T_ORE0 && t < T_CORE) {
    int i = t - T_ORE0;
    if (cargoCount() >= maxCargo()) { say("Трюм повний! Руду втрачено. Повертайся на станцію."); return; }
    g.cargo[i]++;
    sfxCoin();
    char b[64];
    snprintf(b, sizeof b, "+ %s (%d $)", ORES[i].name, ORES[i].price);
    say(b, 1200);
  } else if (t == T_CORE) {
    g.won = 1; mode = M_WIN; sfxWin();
    saveData(&g, sizeof g);
  }
}

// arrive at the target tile after an action
static void arrive() {
  g.px += adx; g.py += ady;
  ox = oy = 0;
  if (g.py > g.maxDepth) g.maxDepth = g.py;
  uint8_t& t = at(g.px, g.py);
  if (t == T_GAS) { t = T_AIR; damage(12 + g.py / 10.0f, "Вибух газу"); }
  if (t == T_LAVA) { damage(20 + g.py / 8.0f, "Лава"); t = T_AIR; }
  if (t >= T_ORE0 && t <= T_CORE) { collect(t); t = T_AIR; }
  if (t == T_DIRT) t = T_AIR;
  dirty = true;
}

static void startAction(int dx, int dy) {
  if (act || mode != M_PLAY) return;
  int nx = g.px + dx, ny = g.py + dy;
  if (!inMap(nx, ny)) return;
  if (dx) g.face = dx > 0 ? 3 : 2;
  if (dy > 0) g.face = 1;
  if (dy < 0) g.face = 0;
  uint8_t t = at(nx, ny);
  adx = dx; ady = dy; actT = 0;
  if (dy < 0) {   // flying up: only into open space
    if (solidAt(nx, ny)) { if (t != T_BASE_FUEL && t != T_BASE_SHOP && t != T_BASE_SELL) return; }
    if (g.fuel <= 0) { say("Пального немає! Скористайся каністрою або телепортом (OK)."); return; }
    act = 4; actDur = 0.32f / (1 + g.up[4] * 0.35f);
    return;
  }
  if (t == T_BEDROCK) { say("Тверда порода. Глибше не пробитися."); return; }
  if (t == T_ROCK) { say("Камінь не бурится — обійди або підірви динамітом (OK)."); return; }
  if (dx && !solidAt(g.px, g.py + 1) && !solidAt(nx, ny)) { act = 1; actDur = 0.18f; return; }   // drifting sideways in the air
  if (!solidAt(nx, ny)) { act = 1; actDur = 0.2f; return; }
  // dig
  act = 2; actDur = drillTime(t);
}

static void tick(float dt) {
  if (mode != M_PLAY) return;
  // fuel burns slowly while the engine runs, faster when digging/flying
  float burn = 0.012f;
  if (act == 2) burn = 0.05f; else if (act == 4) burn = 0.08f / (1 + g.up[4] * 0.2f); else if (act == 1) burn = 0.025f;
  if (g.py >= SURF) g.fuel -= burn * dt * 4;
  if (g.fuel < 0) g.fuel = 0;
  if (g.fuel <= 0 && g.py >= SURF && act == 0 && !g.cans && !g.teleports) { say("Пальне скінчилося. Тебе евакуювали за 30% грошей.", 5000); g.money = g.money * 7 / 10; g.px = 5; g.py = SURF - 1; g.fuel = maxFuel() / 3; memset(g.cargo, 0, sizeof g.cargo); }
  if (act) {
    actT += dt;
    float k = std::min(1.0f, actT / actDur);
    if (act == 2) { ox = adx * 3 * k; oy = ady * 3 * k; }   // nudging into the soil
    else { ox = adx * TS * k; oy = ady * TS * k; }
    if (actT >= actDur) {
      int a = act;
      act = 0;
      if (a == 2) { uint8_t& t = at(g.px + adx, g.py + ady); if (t == T_DIRT) t = T_AIR; else if (t >= T_ORE0 || t == T_GAS || t == T_LAVA) {} arrive(); }
      else if (a == 3) { g.py++; ox = oy = 0; fallTiles++; uint8_t& t = at(g.px, g.py); if (t == T_GAS || t == T_LAVA || (t >= T_ORE0 && t <= T_CORE)) { g.py--; adx = 0; ady = 1; arrive(); } }
      else arrive();
      if (a != 3 && a != 4) fallTiles = 0;
    }
  }
  if (!act && mode == M_PLAY) {
    // gravity: nothing under the pod → fall (unless holding ▲)
    // on the surface the pod stays on the ground level until ▼ is pressed, even above a dug shaft
    bool surfaceHold = g.py == SURF - 1 && !inputHeld(K_DOWN);
    if (!surfaceHold && !solidAt(g.px, g.py + 1) && !(inputHeld(K_UP) && g.fuel > 0)) {
      if (inMap(g.px, g.py + 1)) { act = 3; adx = 0; ady = 1; actT = 0; actDur = std::max(0.07f, 0.16f - fallTiles * 0.02f); }
    } else if (fallTiles >= 4) {
      damage((fallTiles - 3) * 4.0f, "Удар при падінні");
      fallTiles = 0;
    } else fallTiles = 0;
    // held directions keep moving
    if (!act && mode == M_PLAY) {
      int side = inputHeld(K_LEFT) ? -1 : inputHeld(K_RIGHT) ? 1 : 0;
      if (inputHeld(K_UP) && side) {
        // flying up while steering: go diagonally if there is room, otherwise up or sideways
        int nx = g.px + side, ny = g.py - 1;
        if (g.fuel > 0 && inMap(nx, ny) && !solidAt(nx, ny) && (!solidAt(nx, g.py) || !solidAt(g.px, ny))) {
          g.face = side > 0 ? 3 : 2;
          adx = side; ady = -1; actT = 0; act = 4; actDur = 0.36f / (1 + g.up[4] * 0.35f);
        } else if (inMap(g.px, ny) && !solidAt(g.px, ny)) startAction(0, -1);
        else startAction(side, 0);
      } else if (inputHeld(K_DOWN)) startAction(0, 1);
      else if (inputHeld(K_LEFT)) startAction(-1, 0);
      else if (inputHeld(K_RIGHT)) startAction(1, 0);
      else if (inputHeld(K_UP)) startAction(0, -1);
    }
  }
}

// ---------- shop ----------
static int32_t cargoValue() { int32_t v = 0; for (int i = 0; i < 9; i++) v += g.cargo[i] * ORES[i].price; return v; }

struct ShopRow { int kind, arg; };   // kind: 0 sell, 1 fuel, 2 repair, 3 upgrade, 4 dynamite, 5 teleport, 6 kit, 7 can
static ShopRow rows[16];
static int nRows;
static uint8_t station;   // which building we're in

static void buildShop() {
  nRows = 0;
  if (station == T_BASE_SELL) rows[nRows++] = {0, 0};
  if (station == T_BASE_FUEL) { rows[nRows++] = {1, 0}; rows[nRows++] = {2, 0}; rows[nRows++] = {7, 0}; rows[nRows++] = {6, 0}; }
  if (station == T_BASE_SHOP) { for (int i = 0; i < 5; i++) rows[nRows++] = {3, i}; rows[nRows++] = {4, 0}; rows[nRows++] = {5, 0}; }
}
static int32_t rowPrice(const ShopRow& r) {
  switch (r.kind) {
    case 1: return (int32_t)ceilf((maxFuel() - g.fuel) * 6);
    case 2: return (int32_t)ceilf((maxHull() - g.hull) * 5);
    case 3: return g.up[r.arg] >= 4 ? 0 : (int32_t)UPPRICE[r.arg][g.up[r.arg]];
    case 4: return 300;
    case 5: return 900;
    case 6: return 700;
    case 7: return 400;
  }
  return 0;
}
static void rowLabel(const ShopRow& r, char* b, size_t n) {
  switch (r.kind) {
    case 0: snprintf(b, n, "Продати руду (%d шт.)", cargoCount()); break;
    case 1: snprintf(b, n, "Заправити повний бак"); break;
    case 2: snprintf(b, n, "Відремонтувати корпус"); break;
    case 3: if (g.up[r.arg] >= 4) snprintf(b, n, "%s — максимум", UPNAME[r.arg]); else snprintf(b, n, "%s: рівень %d → %d", UPNAME[r.arg], g.up[r.arg] + 1, g.up[r.arg] + 2); break;
    case 4: snprintf(b, n, "Динаміт (є %d)", g.dynamite); break;
    case 5: snprintf(b, n, "Телепорт додому (є %d)", g.teleports); break;
    case 6: snprintf(b, n, "Ремкомплект +40%% (є %d)", g.kits); break;
    case 7: snprintf(b, n, "Каністра пального (є %d)", g.cans); break;
  }
}
static void buyRow(const ShopRow& r) {
  char b[96];
  if (r.kind == 0) {
    int32_t v = cargoValue();
    if (!v) { say("Трюм порожній."); return; }
    g.money += v; memset(g.cargo, 0, sizeof g.cargo); g.trips++; sfxWin();
    snprintf(b, sizeof b, "Продано на %ld $", (long)v); say(b);
    saveData(&g, sizeof g);
    return;
  }
  int32_t p = rowPrice(r);
  if (r.kind == 3 && g.up[r.arg] >= 4) return;
  if ((r.kind == 1 && g.fuel >= maxFuel() - 0.01f) || (r.kind == 2 && g.hull >= maxHull() - 0.01f)) { say("Уже повний."); return; }
  if (g.money < p) {
    if (r.kind == 1 || r.kind == 2) {   // partial service for what you can afford
      float k = (float)g.money / p;
      if (r.kind == 1) g.fuel += (maxFuel() - g.fuel) * k; else g.hull += (maxHull() - g.hull) * k;
      g.money = 0; say("Грошей вистачило лише частково.");
    } else say("Не вистачає грошей.");
    return;
  }
  g.money -= p;
  switch (r.kind) {
    case 1: g.fuel = maxFuel(); break;
    case 2: g.hull = maxHull(); break;
    case 3: g.up[r.arg]++; if (r.arg == 2) g.fuel = maxFuel(); if (r.arg == 1) g.hull = maxHull(); snprintf(b, sizeof b, "%s покращено!", UPNAME[r.arg]); say(b); break;
    case 4: g.dynamite++; break;
    case 5: g.teleports++; break;
    case 6: g.kits++; break;
    case 7: g.cans++; break;
  }
  saveData(&g, sizeof g);
}

// ---------- items (OK) ----------
static void useItem(int i) {
  mode = M_PLAY;
  if (i == 0) {
    if (!g.dynamite) { say("Динаміту немає."); return; }
    g.dynamite--;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
      int x = g.px + dx, y = g.py + dy;
      if (inMap(x, y) && y >= SURF && at(x, y) != T_BEDROCK && at(x, y) != T_CORE) at(x, y) = T_AIR;
    }
    damage(4, "Вибух поруч");
    if (mode == M_PLAY) say("БАХ! Каміння навколо розлетілося.");
  } else if (i == 1) {
    if (!g.teleports) { say("Телепортів немає."); return; }
    g.teleports--; g.px = 5; g.py = SURF - 1; act = 0; ox = oy = 0; fallTiles = 0;
    say("Телепорт на станцію!");
  } else if (i == 2) {
    if (!g.kits) { say("Ремкомплектів немає."); return; }
    g.kits--; g.hull = std::min(maxHull(), g.hull + maxHull() * 0.4f); say("Корпус підлатано.");
  } else if (i == 3) {
    if (!g.cans) { say("Каністр немає."); return; }
    g.cans--; g.fuel = std::min(maxFuel(), g.fuel + maxFuel() * 0.5f); say("Залито пів бака.");
  }
}

// ---------- drawing ----------
static uint16_t stratum(int y, int x) {   // soil colour by depth: purple → ochre → green → crimson
  static const uint32_t C[5] = {0x8a6a4a, 0x7a5ab0, 0xb08a3a, 0x6a9a3a, 0xa03a4a};
  int d = y - SURF, i = std::min(4, d / 45), j = std::min(4, i + 1);
  uint16_t a = rgb(C[i]), b = rgb(C[j]);
  uint16_t c = mix565(a, b, (uint8_t)std::min(255, (d % 45) * 255 / 45 * ((d % 45) > 35 ? 1 : 0)));
  return ((x * 7 + y * 13) % 5 == 0) ? mix565(c, rgb(0), 30) : c;
}

static void drawTile(int x, int y, int sx, int sy) {
  uint8_t t = at(x, y);
  if (y < SURF) {   // sky band
    uint16_t sky = mix565(rgb(0x1a1040), rgb(0x6a3a8a), (uint8_t)(y * 60));
    fill(sx, sy, TS, TS, sky);
    if ((x * 37 + y * 11) % 9 == 0) fill(sx + (x * 5) % 13, sy + (y * 7) % 13, 1, 1, rgb(0xffffff));
  } else if (t == T_AIR) {
    fill(sx, sy, TS, TS, mix565(stratum(y, x), rgb(0x05050a), 200));   // dug tunnel
    return;
  } else fill(sx, sy, TS, TS, stratum(y, x));
  if (y >= SURF && t != T_AIR) {   // soil texture
    uint16_t dk = mix565(stratum(y, x), rgb(0), 60), lt = mix565(stratum(y, x), rgb(0xffffff), 40);
    fill(sx + (x * 3 + y) % 11 + 2, sy + (y * 5 + x) % 10 + 3, 2, 2, dk);
    fill(sx + (x * 7 + y * 3) % 12 + 1, sy + (x + y * 9) % 11 + 2, 1, 1, lt);
  }
  switch (t) {
    case T_ROCK: fillRound(sx + 1, sy + 1, TS - 2, TS - 2, 4, rgb(0x6a6a78)); fillRound(sx + 3, sy + 3, 7, 5, 2, rgb(0x8a8a98)); fill(sx + 9, sy + 9, 4, 2, rgb(0x50505c)); break;
    case T_LAVA: {
      uint32_t p = (nowMs() / 200 + x + y) % 3;
      fill(sx, sy, TS, TS, p == 0 ? rgb(0xff5a1a) : p == 1 ? rgb(0xff7a20) : rgb(0xe04010));
      fill(sx + 3, sy + 4 + (int)p, 4, 2, rgb(0xffd040)); fill(sx + 10, sy + 9 - (int)p, 3, 2, rgb(0xffd040));
      break;
    }
    case T_GAS:   // looks like soil, with a faint hint when the engine is close
      if (abs(x - g.px) + abs(y - g.py) <= 1 + g.up[0] / 2) { fillCircle(sx + 8, sy + 8, 3, mix565(stratum(y, x), rgb(0x9aff5a), 90)); }
      break;
    case T_BEDROCK: fill(sx, sy, TS, TS, rgb(0x202028)); fill(sx + 2, sy + 3, 5, 3, rgb(0x34343e)); break;
    case T_CORE: {
      int r = 5 + (int)(sinf(nowMs() / 200.0f) * 2);
      fillCircle(sx + 8, sy + 8, r + 1, rgb(0x6020a0)); fillCircle(sx + 8, sy + 8, r - 1, rgb(0xff80ff)); fillCircle(sx + 6, sy + 6, 2, rgb(0xffffff));
      break;
    }
    case T_BASE_FUEL: case T_BASE_SHOP: case T_BASE_SELL: break;
    default:
      if (t >= T_ORE0 && t < T_CORE) {
        uint16_t c = rgb(ORES[t - T_ORE0].color), d = mix565(c, rgb(0), 110);
        int k = t - T_ORE0;
        if (k < 5) { fillCircle(sx + 5, sy + 6, 3, d); fillCircle(sx + 5, sy + 5, 2, c); fillCircle(sx + 11, sy + 10, 3, d); fillCircle(sx + 11, sy + 9, 2, c); fill(sx + 9, sy + 3, 2, 2, c); }
        else {   // gems: faceted
          tri(sx + 8, sy + 2, sx + 3, sy + 8, sx + 13, sy + 8, c);
          tri(sx + 3, sy + 8, sx + 13, sy + 8, sx + 8, sy + 14, d);
          fill(sx + 6, sy + 5, 2, 2, rgb(0xffffff));
        }
      }
  }
}

static void drawPod(int sx, int sy) {
  uint16_t body = rgb(0xe0a020), dark = rgb(0x8a5a10), glass = rgb(0x6ad0ff), drill = rgb(0xc8c8d0);
  int f = g.face;
  fillRound(sx + 2, sy + 4, 12, 9, 3, body);
  fill(sx + 2, sy + 11, 12, 2, dark);
  fillRound(sx + (f == 2 ? 3 : 8), sy + 5, 5, 4, 1, glass);
  fill(sx + 2, sy + 13, 12, 3, rgb(0x303038));
  for (int i = 0; i < 3; i++) fill(sx + 3 + i * 4, sy + 14, 2, 2, rgb(0x60606a));
  bool spin = act == 2 && (nowMs() / 60) % 2;
  uint16_t dc = spin ? rgb(0xffffff) : drill;
  if (f == 1) tri(sx + 4, sy + 13, sx + 12, sy + 13, sx + 8, sy + 18, dc);
  else if (f == 2) tri(sx + 2, sy + 5, sx + 2, sy + 12, sx - 3, sy + 8, dc);
  else if (f == 3) tri(sx + 14, sy + 5, sx + 14, sy + 12, sx + 19, sy + 8, dc);
  else { fill(sx + 7, sy + 1, 2, 3, drill); }
  if (act == 4 || (inputHeld(K_UP) && g.fuel > 0 && mode == M_PLAY)) {   // thruster flame
    int fl = 2 + (int)(nowMs() / 50) % 3;
    fill(sx + 4, sy + 16, 3, fl, rgb(0xff8a20)); fill(sx + 9, sy + 16, 3, fl, rgb(0xff8a20));
  }
}

static void bar(int x, int y, int w, float v, float mx, uint16_t c, const char* lab) {
  text(lab, x, y + 7, pal::SUB, F9B);
  int bx = x + 30;
  fillRound(bx, y, w - 30, 8, 4, pal::LINE);
  int f = (int)((w - 30) * std::max(0.0f, v) / mx);
  if (f > 3) fillRound(bx, y, f, 8, 4, v / mx < 0.25f ? pal::RED : c);
}

static void draw() {
  fill(0, 0, W, H, pal::BG);
  // camera follows the pod smoothly
  int target = std::max(0, std::min(MH - VH, (int)g.py - 6));
  camY = target;
  float fy = g.py + oy / TS;
  int camPix = (int)((fy - 6) * TS);
  camPix = std::max(0, std::min((MH - VH) * TS, camPix));
  int cy0 = camPix / TS, off = camPix % TS;
  for (int y = 0; y <= VH; y++) for (int x = 0; x < MW; x++) {
    int my = cy0 + y;
    if (my >= MH) continue;
    int sy = VY + y * TS - off;
    if (sy >= H - 30) continue;
    drawTile(x, my, x * TS, sy);
  }
  if (cy0 <= SURF) {   // surface buildings
    for (int b = 0; b < 3; b++) {
      static const int BX[3] = {2, 7, 12};
      static const char* BL[3] = {"ПАЛЬНЕ", "РУДА", "МАЙСТЕРНЯ"};
      static const uint32_t BC[3] = {0xffb21a, 0x4cd964, 0x4aa8ff};
      int sx = BX[b] * TS, sy = VY + (SURF - 1 - cy0) * TS - off;
      fill(sx - 10, sy - 18, 36, 34, rgb(0x2a3040));
      fill(sx - 10, sy - 18, 36, 3, rgb(BC[b]));
      fill(sx - 1, sy - 2, 18, 18, rgb(0x0a0e14));
      fill(sx + 2, sy + 2, 12, 2, rgb(BC[b]));
      text(BL[b], sx + 8, sy - 21, rgb(BC[b]), F9B, CENTER);
    }
  }
  int psx = g.px * TS + (int)ox, psy = VY + (int)((g.py - cy0) * TS + oy) - off;
  drawPod(psx, psy);
  // HUD
  fill(0, 0, W, VY, pal::PANEL);
  char s[256];
  bar(4, 4, 116, g.fuel, maxFuel(), rgb(0xffb21a), "пальне");
  bar(4, 16, 116, g.hull, maxHull(), rgb(0x4cd964), "корпус");
  snprintf(s, sizeof s, "%ld $", (long)g.money);
  text(s, W - 6, 12, pal::AMBER, F12B, RIGHT);
  snprintf(s, sizeof s, "%d м · трюм %d/%d", std::max(0, (int)g.py - SURF + 1) * 5, cargoCount(), maxCargo());
  text(s, W - 6, 27, cargoCount() >= maxCargo() ? pal::RED : pal::SUB, F10B, RIGHT);
  fill(0, VY - 1, W, 1, pal::LINE);
  // message / hint line
  int by = H - 30;
  fill(0, by, W, 30, pal::PANEL);
  if (nowMs() < msgUntil) {
    auto ls = wrap(msg, W - 12, F11);
    for (size_t i = 0; i < ls.size() && i < 2; i++) text(ls[i], 6, by + 12 + 13 * (int)i, pal::TXT, F11);
  } else {
    const char* h = "◀▶▼ — бурити · ▲ — злетіти · OK — предмети";
    uint8_t t = at(g.px, g.py);
    if (t == T_BASE_FUEL || t == T_BASE_SHOP || t == T_BASE_SELL) h = "OK — увійти на станцію";
    text(h, W / 2, by + 18, pal::SUB, F11, CENTER);
  }

  if (mode == M_SHOP) {
    blendRect(0, 0, W, H, pal::SHADE, 170);
    fill(10, 40, W - 20, 250, pal::PANEL);
    strokeRect(10, 40, W - 20, 250, pal::AMBER);
    text(station == T_BASE_FUEL ? "ЗАПРАВКА Й РЕМОНТ" : station == T_BASE_SELL ? "СКУПКА РУДИ" : "МАЙСТЕРНЯ", W / 2, 58, pal::AMBER, F12B, CENTER);
    snprintf(s, sizeof s, "у тебе %ld $", (long)g.money);
    text(s, W / 2, 74, pal::SUB, F11, CENTER);
    int y = 96;
    for (int i = 0; i < nRows; i++, y += 20) {
      char l[80];
      rowLabel(rows[i], l, sizeof l);
      bool sl = i == sel;
      if (sl) fill(16, y - 13, W - 32, 18, pal::AMBER);
      text(fit(l, W - 90, F11B), 22, y, sl ? pal::SELTXT : pal::TXT, F11B);
      int32_t p = rows[i].kind == 0 ? cargoValue() : rowPrice(rows[i]);
      if (!(rows[i].kind == 3 && g.up[rows[i].arg] >= 4)) { snprintf(s, sizeof s, "%ld", (long)p); text(s, W - 22, y, sl ? pal::SELTXT : pal::AMBER, F11B, RIGHT); }
    }
    if (station == T_BASE_SELL) {   // cargo list
      y += 6;
      for (int i = 0; i < 9; i++) if (g.cargo[i]) { snprintf(s, sizeof s, "%s ×%d = %d $", ORES[i].name, g.cargo[i], g.cargo[i] * ORES[i].price); text(s, 24, y, pal::SUB, F11); y += 14; }
    }
    if (station == T_BASE_SHOP) {
      static const char* UPD[5] = {"швидше бурить", "більше міцності", "більший бак", "більше руди", "швидше летить"};
      if (rows[sel].kind == 3) text(UPD[rows[sel].arg], W / 2, 270, pal::DIM, F10B, CENTER);
    }
    text("OK — купити · Назад — вийти", W / 2, 284, pal::DIM, F10B, CENTER);
  } else if (mode == M_ITEMS) {
    blendRect(0, 0, W, H, pal::SHADE, 170);
    fill(20, 90, W - 40, 130, pal::PANEL);
    strokeRect(20, 90, W - 40, 130, pal::AMBER);
    text("ПРЕДМЕТИ", W / 2, 108, pal::AMBER, F12B, CENTER);
    int cnt[4] = {g.dynamite, g.teleports, g.kits, g.cans};
    static const char* N[4] = {"Динаміт (вибух 3×3)", "Телепорт на станцію", "Ремкомплект", "Каністра пального"};
    for (int i = 0; i < 4; i++) {
      int y = 132 + i * 20;
      if (i == sel) fill(26, y - 13, W - 52, 18, pal::AMBER);
      snprintf(s, sizeof s, "%s ×%d", N[i], cnt[i]);
      text(s, 32, y, i == sel ? pal::SELTXT : (cnt[i] ? pal::TXT : pal::DIM), F11B);
    }
  } else if (mode == M_MENU) {
    std::string o[2] = {"Продовжити", "Нова гра"};
    snprintf(s, sizeof s, "Найглибше: %d м · рейсів: %u\nМета: знайти Кристал-Ядро біля дна (~%d м).", (g.maxDepth - SURF + 1) * 5, g.trips, (MH - 4 - SURF + 1) * 5);
    uiDialog("МЕНЮ", s, o, 2, sel);
  } else if (mode == M_DEAD) {
    std::string o[1] = {"Відновити"};
    uiDialog("АВАРІЯ", "Машину знищено. Страхова відновить її на станції, але вантаж і половина грошей пропадуть.", o, 1, 0);
  } else if (mode == M_WIN) {
    std::string o[1] = {"Грати далі"};
    snprintf(s, sizeof s, "Ти знайшов Кристал-Ядро на глибині %d м!\nПланета підкорена. Можна далі копати й заробляти.", (g.py - SURF + 1) * 5);
    uiDialog("ПЕРЕМОГА!", s, o, 1, 0);
  }
}

void setup() {
  consoleBegin("miner");
  inputSetRepeat(0);
  Game* l = new Game;
  if (loadData(l, sizeof *l) && l->magic == MAGIC) { g = *l; say("З поверненням, шахтарю!"); }
  else newGame();
  delete l;
  lastT = saveAt = nowMs();
  draw();
  present();
}

void loop() {
  consolePoll();
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      saveData(&g, sizeof g);
      draw();
      if (confirmExit()) returnToLauncher();
      lastT = nowMs();
      continue;
    }
    if (mode == M_PLAY) {
      if (k == K_OK) {
        uint8_t t = at(g.px, g.py);
        if (!act && (t == T_BASE_FUEL || t == T_BASE_SHOP || t == T_BASE_SELL)) { station = t; buildShop(); sel = 0; mode = M_SHOP; }
        else { mode = M_ITEMS; sel = 0; }
      }
      if (k == K_BACK) { mode = M_MENU; sel = 0; }
      if (k <= K_RIGHT && !act) startAction(k == K_LEFT ? -1 : k == K_RIGHT ? 1 : 0, k == K_UP ? -1 : k == K_DOWN ? 1 : 0);
    } else if (mode == M_SHOP) {
      if (k == K_UP) sel = (sel + nRows - 1) % nRows;
      if (k == K_DOWN) sel = (sel + 1) % nRows;
      if (k == K_OK) buyRow(rows[sel]);
      if (k == K_BACK) mode = M_PLAY;
    } else if (mode == M_ITEMS) {
      if (k == K_UP) sel = (sel + 3) % 4;
      if (k == K_DOWN) sel = (sel + 1) % 4;
      if (k == K_OK) useItem(sel);
      if (k == K_BACK) mode = M_PLAY;
    } else if (mode == M_MENU) {
      if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) sel ^= 1;
      if (k == K_BACK) mode = M_PLAY;
      if (k == K_OK) { if (sel == 1) newGame(); mode = M_PLAY; }
    } else if (mode == M_DEAD) {
      if (k == K_OK) {
        g.money /= 2; memset(g.cargo, 0, sizeof g.cargo);
        g.px = 5; g.py = SURF - 1; g.hull = maxHull(); g.fuel = maxFuel(); act = 0; ox = oy = 0; fallTiles = 0;
        mode = M_PLAY; saveData(&g, sizeof g);
      }
    } else if (mode == M_WIN) {
      if (k == K_OK) mode = M_PLAY;
    }
  }
  uint32_t t = nowMs();
  float dt = (t - lastT) / 1000.0f;
  lastT = t;
  if (dt > 0.05f) dt = 0.05f;
  tick(dt);
  if (t - saveAt > 20000) { saveData(&g, sizeof g); saveAt = t; }
  draw();
  present();
  delay(4);
}
