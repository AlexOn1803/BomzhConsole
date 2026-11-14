// Ферма для кишенькової консолі.
// ▲▼◀▶ — ходити, OK — дія з клітинкою попереду (зорати, посіяти, полити, зібрати, розчистити, двері),
// Назад — меню (насіння, інвентар, розбризкувач, спати, мета), довге Назад — вийти.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>
#include <algorithm>

using namespace gfx;

static const int MW = 24, MH = 22, TS = 16, VW = 15, VH = 14, VY = 30;
enum Tile : uint8_t { T_GRASS, T_SOIL, T_ROCK, T_WEED, T_TREE, T_WATER, T_PATH, T_WALL, T_HOUSE, T_SHOP, T_COOP, T_BARN, T_SPRINK, T_FENCE };
enum Season : uint8_t { SPRING, SUMMER, AUTUMN, WINTER };
static const char* SEASON[4] = {"весна", "літо", "осінь", "зима"};
static const int SEASON_DAYS = 14;
static const int32_t GOAL = 100000;

struct Crop { const char* name; uint8_t seasons; uint8_t days, regrow; uint16_t seed, sell; uint32_t color; };
static const Crop CROPS[] = {
  {"Редиска", 1 << SPRING, 3, 0, 10, 30, 0xff4a6a},   {"Картопля", 1 << SPRING, 5, 0, 20, 60, 0xc8a060},
  {"Полуниця", 1 << SPRING, 7, 3, 60, 55, 0xff3040},  {"Помідор", 1 << SUMMER, 6, 3, 40, 45, 0xff4020},
  {"Кукурудза", 1 << SUMMER, 8, 4, 50, 60, 0xffd040}, {"Кавун", 1 << SUMMER, 10, 0, 80, 300, 0x40a040},
  {"Соняшник", (1 << SUMMER) | (1 << AUTUMN), 6, 0, 30, 85, 0xffc020},
  {"Буряк", 1 << AUTUMN, 4, 0, 15, 45, 0xa02050},     {"Гарбуз", 1 << AUTUMN, 9, 0, 70, 280, 0xff8020},
};
static const int NCROP = sizeof CROPS / sizeof CROPS[0];
static const int EGG_PRICE = 50, MILK_PRICE = 120, STONE_PRICE = 5;

struct Cell { uint8_t t, crop, age, flags; };   // crop: 0 none, else CROPS[crop-1]; flags: 1 watered today
struct Game {
  uint32_t magic;
  Cell map[MH][MW];
  uint8_t px, py, face, season, rain, energyUp;
  uint16_t day, year;
  int32_t money, earned;
  uint8_t energy, seedSel, goalDone;
  uint8_t seeds[NCROP], crops[NCROP];
  uint8_t chickens, cows, chickFed, cowFed, eggsReady, milkReady, sprinklers;
  uint16_t eggs, milk, hay, stone;
};
static Game g;
static const uint32_t MAGIC = 0x46524d33;

enum Mode { M_PLAY, M_MENU, M_SEEDS, M_INV, M_SHOP, M_NIGHT };
static Mode mode = M_PLAY;
static int menuSel, shopSel;
static char msg[200];
static uint32_t msgUntil, lastChange;
static bool dirty;
static char nightText[300];

static int rnd(int n) { return (int)(randomU32() % (uint32_t)n); }
static void say(const char* s) { strncpy(msg, s, sizeof msg - 1); msg[sizeof msg - 1] = 0; msgUntil = nowMs() + 2600; }
static int maxEnergy() { return 100 + g.energyUp * 25; }
static Cell& at(int x, int y) { return g.map[y][x]; }
static bool inMap(int x, int y) { return x >= 0 && y >= 0 && x < MW && y < MH; }
static bool inField(int x, int y) { return x >= 5 && x <= 22 && y >= 7 && y <= 14; }
static bool cropInSeason(int ci, int season) { return CROPS[ci].seasons & (1 << season); }

// ---------- world ----------
static void building(int x0, int y0, int w, int h, uint8_t door, int dx) {
  for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) at(x, y) = {T_WALL, 0, door, 0};   // age = which building (for drawing)
  at(x0 + dx, y0 + h - 1) = {door, 0, door, 0};
}

static void newWorld() {
  memset(&g, 0, sizeof g);
  g.magic = MAGIC;
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
    at(x, y) = {T_GRASS, 0, 0, 0};
    if (x == 0 || y == 0 || x == MW - 1 || y == MH - 1) at(x, y).t = T_TREE;
  }
  building(2, 1, 4, 3, T_HOUSE, 1);
  building(17, 1, 4, 3, T_SHOP, 2);
  building(6, 16, 3, 2, T_COOP, 1);    // door (7,17), walk up to it from (7,18)
  building(11, 16, 4, 3, T_BARN, 1);   // door (12,18), walk up to it from (12,19)
  for (int y = 16; y < 20; y++) for (int x = 17; x < 22; x++) if (!((x == 17 || x == 21) && (y == 16 || y == 19))) at(x, y).t = T_WATER;
  for (int x = 1; x < MW - 1; x++) at(x, 5).t = T_PATH;
  at(3, 4).t = T_PATH; at(19, 4).t = T_PATH;
  for (int y = 6; y < 18; y++) at(1, y).t = T_GRASS;
  for (int y = 6; y < 15; y++) at(3, y).t = T_PATH;
  for (int y = 7; y < 15; y++) for (int x = 6; x < 22; x++) { int r = rnd(100); at(x, y).t = r < 12 ? T_ROCK : r < 42 ? T_WEED : T_GRASS; }
  for (int x = 5; x < 23; x++) at(x, 6).t = T_FENCE;
  at(12, 6).t = T_PATH; at(13, 6).t = T_PATH;
  for (int y = 8; y < 10; y++) for (int x = 8; x < 12; x++) at(x, y) = {T_SOIL, 0, 0, 0};
  g.px = 3; g.py = 5; g.face = 1;
  g.day = 1; g.year = 1; g.season = SPRING; g.money = 500; g.energy = 100;
  g.seeds[0] = 10; g.seeds[1] = 4;
  say("Ласкаво просимо на ферму! Іди вниз до поля, посій редиску (OK) і полий (ще раз OK).");
}

static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};   // K_UP K_DOWN K_LEFT K_RIGHT
static bool walkable(int x, int y) {
  if (!inMap(x, y)) return false;
  uint8_t t = at(x, y).t;
  return t == T_GRASS || t == T_SOIL || t == T_PATH;
}

static bool useEnergy(int e) {
  if (g.energy < e) { say("Сил немає. Час спати: Назад → Спати."); return false; }
  g.energy -= e;
  return true;
}

static bool ready(const Cell& c) { return c.crop && c.age >= CROPS[c.crop - 1].days; }

// ---------- one night: growth, animals, weather, seasons ----------
static void nextDay() {
  char extra[200] = "";
  // sprinklers water first
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) if (at(x, y).t == T_SPRINK)
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (inMap(x + dx, y + dy) && at(x + dx, y + dy).t == T_SOIL) at(x + dx, y + dy).flags |= 1;
  int grown = 0;
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
    Cell& c = at(x, y);
    if (c.t != T_SOIL) continue;
    bool wet = (c.flags & 1) || g.rain;
    if (c.crop && wet && !ready(c)) { c.age++; grown++; }
    if (!c.crop && !wet && rnd(100) < 6) c.t = T_GRASS;    // untended soil grows over
    c.flags = 0;
  }
  // animals: fed today → products tomorrow
  if (g.chickFed) g.eggsReady = (uint8_t)std::min(250, g.eggsReady + g.chickens);
  if (g.cowFed) g.milkReady = (uint8_t)std::min(250, g.milkReady + g.cows);
  g.chickFed = g.cowFed = 0;
  // calendar
  g.day++;
  int seasonBefore = g.season;
  if ((g.day - 1) % SEASON_DAYS == 0) {
    g.season = (g.season + 1) % 4;
    if (g.season == SPRING) g.year++;
    int died = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
      Cell& c = at(x, y);
      if (c.t == T_SOIL && c.crop && !cropInSeason(c.crop - 1, g.season)) { c.crop = 0; c.age = 0; died++; }
    }
    snprintf(extra, sizeof extra, "\nНастала %s!%s", SEASON[g.season], died ? " Рослини не свого сезону зів'яли." : "");
  }
  (void)seasonBefore;
  g.rain = g.season != WINTER && rnd(100) < 22;
  // new weeds pop up
  for (int i = 0; i < 4; i++) { int x = 6 + rnd(16), y = 7 + rnd(8); if (at(x, y).t == T_GRASS) at(x, y).t = T_WEED; }
  bool fainted = g.energy == 0;
  g.energy = fainted ? maxEnergy() / 2 : maxEnergy();
  snprintf(nightText, sizeof nightText, "День %u · %s · рік %u\n%s%s%s", g.day, SEASON[g.season], g.year,
           g.rain ? "Сьогодні дощ — поле поливати не треба." : g.season == WINTER ? "Зима: нічого не росте, але тварини дають продукти." : "Сонячно.",
           fainted ? "\nТи заснув від утоми: лише половина сил." : "", extra);
  mode = M_NIGHT; sfxSelect();
  saveData(&g, sizeof g);
  dirty = false;
}

// ---------- actions ----------
static void action() {
  int x = g.px + DX[g.face], y = g.py + DY[g.face];
  if (!inMap(x, y)) return;
  Cell& c = at(x, y);
  char b[160];
  switch (c.t) {
    case T_HOUSE: mode = M_MENU; menuSel = 3; return;
    case T_SHOP: mode = M_SHOP; shopSel = 0; return;
    case T_COOP:
    case T_BARN: {
      bool coop = c.t == T_COOP;
      int n = coop ? g.chickens : g.cows;
      if (!n) { say(coop ? "Курник порожній. Курей продають у крамниці." : "Корівник порожній. Корів продають у крамниці."); return; }
      int got = coop ? g.eggsReady : g.milkReady;
      if (coop) { g.eggs += got; g.eggsReady = 0; } else { g.milk += got; g.milkReady = 0; }
      uint8_t& fed = coop ? g.chickFed : g.cowFed;
      int need = coop ? n : n * 2;
      const char* fedTxt;
      if (fed) fedTxt = "Уже нагодовані.";
      else if (g.hay >= need) { g.hay -= need; fed = 1; fedTxt = "Нагодовано сіном."; }
      else fedTxt = "Сіна замало! Коси бур'ян або купи сіно.";
      snprintf(b, sizeof b, "%s: +%d. %s", coop ? "Яйця" : "Молоко", got, fedTxt);
      say(b);
      break;
    }
    case T_ROCK: if (useEnergy(4)) { c.t = T_GRASS; g.stone++; say("Камінь розбито (+1 камінь)."); sfxHit(); } break;
    case T_WEED: if (useEnergy(1)) { c.t = T_GRASS; if (g.hay < 999) g.hay++; say("Бур'ян скошено (+1 сіно)."); } break;
    case T_GRASS:
      if (!inField(x, y)) { say("Тут орати не можна — поле за парканом."); break; }
      if (useEnergy(2)) c = {T_SOIL, 0, 0, 0};
      break;
    case T_SOIL:
      if (ready(c)) { if (useEnergy(1)) {
        int ci = c.crop - 1;
        if (g.crops[ci] < 250) g.crops[ci]++;
        snprintf(b, sizeof b, "Зібрано: %s", CROPS[ci].name); say(b); sfxCoin();
        if (CROPS[ci].regrow) c.age = CROPS[ci].days - CROPS[ci].regrow; else { c.crop = 0; c.age = 0; }
      } }
      else if (!c.crop) {
        int s = g.seedSel;
        if (!g.seeds[s]) { snprintf(b, sizeof b, "Немає насіння «%s». Назад → Насіння, або купи в крамниці.", CROPS[s].name); say(b); break; }
        if (!cropInSeason(s, g.season)) { snprintf(b, sizeof b, "%s не росте в сезон «%s».", CROPS[s].name, SEASON[g.season]); say(b); break; }
        if (useEnergy(1)) { g.seeds[s]--; c.crop = s + 1; c.age = 0; }
      } else if (!(c.flags & 1) && !g.rain) { if (useEnergy(1)) c.flags |= 1; }
      else {
        snprintf(b, sizeof b, "%s: ще %d дн. (росте, коли полито)", CROPS[c.crop - 1].name, CROPS[c.crop - 1].days - c.age);
        say(b);
      }
      break;
    case T_WATER: say("Ставок. Води в лійці вистачає завжди."); break;
    case T_SPRINK: say("Розбризкувач щоранку поливає 8 клітинок навколо."); break;
    case T_FENCE: say("Паркан. Вхід на поле — посередині."); break;
    default: break;
  }
  dirty = true; lastChange = nowMs();
}

static void placeSprinkler() {
  int x = g.px + DX[g.face], y = g.py + DY[g.face];
  if (!g.sprinklers) { say("Розбризкувачів немає. Купи в крамниці."); return; }
  if (!inMap(x, y) || !inField(x, y) || (at(x, y).t != T_GRASS && !(at(x, y).t == T_SOIL && !at(x, y).crop))) { say("Стань перед порожньою клітинкою поля."); return; }
  at(x, y) = {T_SPRINK, 0, 0, 0};
  g.sprinklers--;
  say("Розбризкувач стоїть! Поливатиме все навколо.");
}

// ---------- shop ----------
enum ShopKind { SH_SELL, SH_SEED, SH_HAY, SH_CHICK, SH_COW, SH_SPRINK, SH_ENERGY };
struct ShopItem { ShopKind k; int crop; };
static ShopItem shopList[24];
static int shopN;
static int32_t sellValue() {
  int32_t v = g.eggs * EGG_PRICE + g.milk * MILK_PRICE + g.stone * STONE_PRICE;
  for (int i = 0; i < NCROP; i++) v += g.crops[i] * CROPS[i].sell;
  return v;
}
static void buildShop() {
  shopN = 0;
  shopList[shopN++] = {SH_SELL, 0};
  for (int i = 0; i < NCROP; i++) if (cropInSeason(i, g.season)) shopList[shopN++] = {SH_SEED, i};
  shopList[shopN++] = {SH_HAY, 0};
  shopList[shopN++] = {SH_CHICK, 0};
  shopList[shopN++] = {SH_COW, 0};
  shopList[shopN++] = {SH_SPRINK, 0};
  if (g.energyUp < 4) shopList[shopN++] = {SH_ENERGY, 0};
}
static int price(const ShopItem& it) {
  switch (it.k) {
    case SH_SEED: return CROPS[it.crop].seed;
    case SH_HAY: return 50;            // 10 hay
    case SH_CHICK: return 400;
    case SH_COW: return 1500;
    case SH_SPRINK: return 600;
    case SH_ENERGY: return 1000 * (g.energyUp + 1);
    default: return 0;
  }
}
static void shopLabel(const ShopItem& it, char* b, size_t n) {
  switch (it.k) {
    case SH_SELL: snprintf(b, n, "Продати все (%ld грн)", (long)sellValue()); break;
    case SH_SEED: snprintf(b, n, "%s, насіння (є %d)", CROPS[it.crop].name, g.seeds[it.crop]); break;
    case SH_HAY: snprintf(b, n, "Сіно ×10 (є %d)", g.hay); break;
    case SH_CHICK: snprintf(b, n, "Курка (є %d з 8)", g.chickens); break;
    case SH_COW: snprintf(b, n, "Корова (є %d з 4)", g.cows); break;
    case SH_SPRINK: snprintf(b, n, "Розбризкувач (є %d)", g.sprinklers); break;
    case SH_ENERGY: snprintf(b, n, "Чобітки: +25 сил"); break;
  }
}
static void buy(const ShopItem& it) {
  char b[120];
  if (it.k == SH_SELL) {
    int32_t v = sellValue();
    if (!v) { say("Нічого продавати. Збери урожай, яйця або молоко."); return; }
    g.money += v; g.earned += v;
    memset(g.crops, 0, sizeof g.crops); g.eggs = g.milk = g.stone = 0;
    snprintf(b, sizeof b, "Продано на %ld грн!", (long)v); say(b); sfxWin();
    if (!g.goalDone && g.earned >= GOAL) { g.goalDone = 1; say("Мету досягнуто: зароблено 100 000 грн! Ти справжній фермер."); }
    return;
  }
  int p = price(it);
  if (g.money < p) { say("Не вистачає грошей."); return; }
  switch (it.k) {
    case SH_SEED: if (g.seeds[it.crop] >= 250) return; g.seeds[it.crop]++; g.seedSel = it.crop; break;
    case SH_HAY: if (g.hay > 980) return; g.hay += 10; break;
    case SH_CHICK: if (g.chickens >= 8) { say("Курник повний."); return; } g.chickens++; break;
    case SH_COW: if (g.cows >= 4) { say("Корівник повний."); return; } g.cows++; break;
    case SH_SPRINK: g.sprinklers++; say("Розбризкувач у тебе. Постав: Назад → Поставити розбризкувач."); break;
    case SH_ENERGY: g.energyUp++; g.energy = std::min<int>(g.energy + 25, maxEnergy()); buildShop(); break;
    default: break;
  }
  g.money -= p;
}

// ---------- drawing ----------
static uint16_t shade(uint16_t c) { return mix565(c, rgb(0x000000), 60); }

static void drawCrop(int sx, int sy, const Cell& c) {
  int ci = c.crop - 1;
  const Crop& cr = CROPS[ci];
  int stage = ready(c) ? 3 : c.age * 3 / cr.days;
  uint16_t leaf = rgb(0x4cae3c), fruit = rgb(cr.color);
  int cx = sx + 8, by = sy + 13;
  if (stage == 0) { fill(cx - 1, by - 3, 2, 3, leaf); fill(cx - 3, by - 4, 2, 2, leaf); fill(cx + 1, by - 4, 2, 2, leaf); return; }
  if (stage == 1) { fill(cx, by - 7, 1, 7, leaf); fillCircle(cx - 3, by - 5, 2, leaf); fillCircle(cx + 3, by - 6, 2, leaf); return; }
  if (stage == 2) { fill(cx, by - 11, 2, 11, leaf); fillCircle(cx - 4, by - 7, 3, leaf); fillCircle(cx + 4, by - 9, 3, leaf); fillCircle(cx, by - 12, 2, leaf); return; }
  // ripe
  if (ci == 6) { fill(cx, by - 12, 2, 12, leaf); fillCircle(cx + 1, by - 12, 5, fruit); fillCircle(cx + 1, by - 12, 2, rgb(0x6a4a20)); return; }   // sunflower
  if (ci == 4) { fill(cx, by - 12, 2, 12, leaf); fillRound(cx + 2, by - 11, 4, 8, 2, fruit); fillCircle(cx - 3, by - 6, 3, leaf); return; }       // corn
  if (ci == 5 || ci == 8) { fillCircle(cx - 4, by - 7, 3, leaf); fillCircle(cx + 1, by - 5, 6, fruit); fill(cx - 2, by - 8, 6, 1, shade(fruit)); return; }   // melon, pumpkin
  fill(cx, by - 10, 2, 10, leaf); fillCircle(cx - 4, by - 7, 3, leaf); fillCircle(cx + 4, by - 8, 3, leaf);
  fillCircle(cx - 3, by - 4, 3, fruit); fillCircle(cx + 3, by - 3, 3, fruit);
}

static void drawBuilding(int sx, int sy, int x, int y, uint8_t kind, bool door) {
  uint16_t wall = kind == T_SHOP ? rgb(0xd8b878) : kind == T_HOUSE ? rgb(0xe0d0b0) : rgb(0xc06040);
  uint16_t roof = kind == T_SHOP ? rgb(0x3a7ad0) : kind == T_HOUSE ? rgb(0xb83a3a) : rgb(0x8a3a20);
  // roof on the top row of the building
  bool topRow = !inMap(x, y - 1) || at(x, y - 1).t != T_WALL || at(x, y - 1).age != kind;
  if (at(x, y).t != T_WALL && !door) topRow = false;
  fill(sx, sy, TS, TS, wall);
  if (topRow) { fill(sx, sy, TS, 9, roof); for (int i = 0; i < TS; i += 4) fill(sx + i, sy + 7, 2, 2, shade(roof)); }
  else if (!door) { fill(sx + 4, sy + 4, 8, 7, rgb(0x8ad0ff)); fill(sx + 7, sy + 4, 2, 7, wall); }
  if (door) {
    fill(sx + 3, sy + 2, 10, 14, rgb(0x6a4020));
    fill(sx + 10, sy + 9, 2, 2, rgb(0xffcc00));
    if (kind == T_SHOP) { fill(sx - 6, sy - 2, 28, 4, rgb(0xffffff)); }
  }
}

static void drawTile(int x, int y, int sx, int sy) {
  const Cell& c = at(x, y);
  uint16_t grass = g.season == WINTER ? rgb(0xe8f0f4) : g.season == AUTUMN ? rgb(0xa8b050) : rgb(0x6cbf4a);
  uint16_t grass2 = g.season == WINTER ? rgb(0xd0dce4) : g.season == AUTUMN ? rgb(0x98a040) : rgb(0x5cad3c);
  switch (c.t) {
    case T_GRASS: case T_WEED: case T_ROCK: case T_TREE: case T_FENCE: case T_SPRINK:
      fill(sx, sy, TS, TS, ((x * 7 + y * 13) % 5) ? grass : grass2);
      if (((x * 31 + y * 17) % 7) == 0) { fill(sx + 4, sy + 9, 1, 3, grass2); fill(sx + 6, sy + 8, 1, 4, grass2); }
      break;
    case T_SOIL: fill(sx, sy, TS, TS, (c.flags & 1) || g.rain ? rgb(0x5a3a22) : rgb(0x9a6a3e));
      for (int i = 0; i < 3; i++) fill(sx + 2, sy + 3 + i * 5, 12, 1, (c.flags & 1) || g.rain ? rgb(0x4a2e1a) : rgb(0x84582e)); break;
    case T_PATH: fill(sx, sy, TS, TS, rgb(0xd8c090)); if ((x + y) % 3 == 0) fill(sx + 5, sy + 6, 3, 2, rgb(0xc0a878)); break;
    case T_WATER: fill(sx, sy, TS, TS, rgb(0x3a8ad8)); if (((nowMs() / 500) + x + y) % 3 == 0) fill(sx + 3, sy + 6, 6, 1, rgb(0x8ac8ff)); break;
    default: break;
  }
  switch (c.t) {
    case T_WEED: fill(sx + 4, sy + 7, 2, 7, rgb(0x2e7a2a)); fill(sx + 8, sy + 5, 2, 9, rgb(0x2e7a2a)); fill(sx + 11, sy + 8, 2, 6, rgb(0x2e7a2a)); break;
    case T_ROCK: fillRound(sx + 2, sy + 5, 12, 10, 4, rgb(0x8a8a90)); fillRound(sx + 4, sy + 6, 6, 4, 2, rgb(0xb0b0b8)); break;
    case T_TREE: fill(sx + 7, sy + 10, 3, 6, rgb(0x6a4020)); fillCircle(sx + 8, sy + 7, 7, g.season == AUTUMN ? rgb(0xd07020) : g.season == WINTER ? rgb(0xc8d8d8) : rgb(0x2e8a3a)); break;
    case T_FENCE: fill(sx, sy + 6, TS, 2, rgb(0xb08050)); fill(sx, sy + 11, TS, 2, rgb(0xb08050)); fill(sx + 2, sy + 4, 3, 12, rgb(0x8a6038)); fill(sx + 11, sy + 4, 3, 12, rgb(0x8a6038)); break;
    case T_SPRINK: fillCircle(sx + 8, sy + 9, 5, rgb(0x9aa8b8)); fill(sx + 7, sy + 2, 2, 6, rgb(0x6a7888)); break;
    case T_SOIL: if (c.crop) drawCrop(sx, sy, c); break;
    case T_WALL: drawBuilding(sx, sy, x, y, c.age, false); break;
    case T_HOUSE: case T_SHOP: case T_COOP: case T_BARN: drawBuilding(sx, sy, x, y, c.t, true); break;
    default: break;
  }
}

static void drawFarmer(int sx, int sy) {
  uint16_t skin = rgb(0xf0c8a0), shirt = rgb(0x3a6ad0), hat = rgb(0xd8b040);
  fill(sx + 4, sy + 9, 8, 6, shirt);
  fill(sx + 4, sy + 14, 3, 2, rgb(0x5a3a20)); fill(sx + 9, sy + 14, 3, 2, rgb(0x5a3a20));
  fill(sx + 5, sy + 3, 6, 6, skin);
  fill(sx + 2, sy + 2, 12, 2, hat); fill(sx + 5, sy, 6, 3, hat);
  if (g.face == 1) { fill(sx + 6, sy + 5, 1, 2, rgb(0x222222)); fill(sx + 9, sy + 5, 1, 2, rgb(0x222222)); }
  if (g.face == 2) fill(sx + 5, sy + 5, 1, 2, rgb(0x222222));
  if (g.face == 3) fill(sx + 10, sy + 5, 1, 2, rgb(0x222222));
  // tool in the facing direction
  int tx = sx + 8 + DX[g.face] * 7, ty = sy + 10 + DY[g.face] * 6;
  fill(tx - 1, ty - 1, 2, 2, rgb(0x8a8a90));
}

static void drawWorld() {
  int camX = std::max(0, std::min(MW - VW, (int)g.px - VW / 2)), camY = std::max(0, std::min(MH - VH, (int)g.py - VH / 2));
  for (int y = 0; y < VH; y++) for (int x = 0; x < VW; x++) drawTile(camX + x, camY + y, x * TS, VY + y * TS);
  int fx = g.px + DX[g.face] - camX, fy = g.py + DY[g.face] - camY;
  if (fx >= 0 && fy >= 0 && fx < VW && fy < VH) strokeRect(fx * TS, VY + fy * TS, TS, TS, rgb(0xffffff));
  drawFarmer((g.px - camX) * TS, VY + (g.py - camY) * TS);
  if (g.rain) for (int i = 0; i < 40; i++) {   // rain streaks
    int rx = (i * 61 + (int)(nowMs() / 9)) % W, ry = VY + (i * 37 + (int)(nowMs() / 5)) % (VH * TS);
    fill(rx, ry, 1, 4, rgb(0xa8c8f0));
  }
  if (g.season == WINTER) for (int i = 0; i < 25; i++) {
    int rx = (i * 61 + (int)(nowMs() / 60)) % W, ry = VY + (i * 37 + (int)(nowMs() / 30)) % (VH * TS);
    fill(rx, ry, 2, 2, rgb(0xffffff));
  }
}

static void drawHud() {
  char s[96];
  fill(0, 0, W, VY, pal::PANEL);
  snprintf(s, sizeof s, "%ld грн", (long)g.money);
  text(s, 6, 13, pal::AMBER, F12B);
  snprintf(s, sizeof s, "день %u · %s%s", (unsigned)g.day, SEASON[g.season], g.rain ? " · дощ" : "");
  text(s, W - 6, 13, pal::TXT, F11, RIGHT);
  int ew = 90;
  fillRound(6, 18, ew, 8, 4, pal::LINE);
  int e = ew * g.energy / maxEnergy();
  if (e > 3) fillRound(6, 18, e, 8, 4, g.energy * 4 < maxEnergy() ? pal::RED : pal::GREEN);
  snprintf(s, sizeof s, "сили %d", g.energy);
  text(s, ew + 12, 26, pal::SUB, F9B);
  snprintf(s, sizeof s, "насіння: %s ×%d", CROPS[g.seedSel].name, g.seeds[g.seedSel]);
  text(s, W - 6, 26, pal::SUB, F9B, RIGHT);
  int by = VY + VH * TS;
  fill(0, by, W, H - by, pal::PANEL);
  if (nowMs() < msgUntil) {
    auto ls = wrap(msg, W - 12, F11);
    for (size_t i = 0; i < ls.size() && i < 3; i++) text(ls[i], 6, by + 13 + 13 * (int)i, pal::TXT, F11);
  } else {
    // what is in front of the farmer
    int x = g.px + DX[g.face], y = g.py + DY[g.face];
    const char* h = "OK — дія · Назад — меню";
    if (inMap(x, y)) {
      const Cell& c = at(x, y);
      if (c.t == T_GRASS && inField(x, y)) h = "OK — зорати (2 сили)";
      else if (c.t == T_WEED) h = "OK — скосити бур'ян (+сіно)";
      else if (c.t == T_ROCK) h = "OK — розбити камінь (4 сили)";
      else if (c.t == T_SOIL && ready(c)) h = "OK — зібрати урожай";
      else if (c.t == T_SOIL && !c.crop) h = "OK — посіяти вибране насіння";
      else if (c.t == T_SOIL && !(c.flags & 1) && !g.rain) h = "OK — полити";
      else if (c.t == T_SHOP) h = "OK — зайти в крамницю";
      else if (c.t == T_HOUSE) h = "OK — додому (спати)";
      else if (c.t == T_COOP) h = "OK — курник: зібрати яйця, нагодувати";
      else if (c.t == T_BARN) h = "OK — корівник: молоко, нагодувати";
    }
    text(h, W / 2, by + 15, pal::SUB, F11, CENTER);
    snprintf(s, sizeof s, "сіно %u · яйця %u · молоко %u", g.hay, g.eggs, g.milk);
    text(s, W / 2, by + 32, pal::DIM, F10B, CENTER);
  }
}

static void panel(int y, int h, const char* title) {
  blendRect(0, 0, W, H, pal::SHADE, 160);
  fill(10, y, W - 20, h, pal::PANEL);
  strokeRect(10, y, W - 20, h, pal::AMBER);
  text(title, W / 2, y + 18, pal::AMBER, F12B, CENTER);
}
static void row(int i, int y, bool sel, const char* s, uint16_t c = pal::TXT) {
  if (sel) fill(16, y - 13, W - 32, 18, pal::AMBER);
  text(fit(s, W - 44, F11B), 22, y, sel ? pal::SELTXT : c, F11B);
  (void)i;
}

static const char* MENU[] = {"Насіння", "Інвентар", "Поставити розбризкувач", "Спати (наступний день)", "Мета й поради", "Продовжити"};
static const int NMENU = 6;

static void draw() {
  fill(0, 0, W, H, pal::BG);
  drawWorld();
  drawHud();
  char s[120];
  if (mode == M_MENU) {
    panel(50, 30 + NMENU * 22, "МЕНЮ");
    for (int i = 0; i < NMENU; i++) row(i, 90 + i * 22, i == menuSel, MENU[i]);
  } else if (mode == M_SEEDS) {
    panel(30, 40 + NCROP * 22, "ВИБЕРИ НАСІННЯ");
    for (int i = 0; i < NCROP; i++) {
      snprintf(s, sizeof s, "%s ×%d%s", CROPS[i].name, g.seeds[i], cropInSeason(i, g.season) ? "" : " (не сезон)");
      row(i, 70 + i * 22, i == g.seedSel, s, cropInSeason(i, g.season) ? pal::TXT : pal::DIM);
    }
  } else if (mode == M_INV) {
    panel(30, 260, "ІНВЕНТАР");
    int y = 64;
    for (int i = 0; i < NCROP; i++) if (g.crops[i]) {
      snprintf(s, sizeof s, "%s ×%d — %d грн", CROPS[i].name, g.crops[i], g.crops[i] * CROPS[i].sell);
      text(s, 22, y, pal::TXT, F11); y += 15;
    }
    snprintf(s, sizeof s, "Яйця ×%u, молоко ×%u, камінь ×%u", g.eggs, g.milk, g.stone); text(s, 22, y, pal::TXT, F11); y += 15;
    snprintf(s, sizeof s, "Сіно ×%u, розбризкувачі ×%u", g.hay, g.sprinklers); text(s, 22, y, pal::TXT, F11); y += 15;
    snprintf(s, sizeof s, "Кури %u з 8, корови %u з 4", g.chickens, g.cows); text(s, 22, y, pal::TXT, F11); y += 22;
    snprintf(s, sizeof s, "Усе разом: %ld грн", (long)sellValue()); text(s, 22, y, pal::AMBER, F11B);
    text("Назад — закрити", W / 2, 282, pal::DIM, F10B, CENTER);
  } else if (mode == M_SHOP) {
    panel(24, 276, "КРАМНИЦЯ");
    snprintf(s, sizeof s, "у тебе %ld грн", (long)g.money);
    text(s, W / 2, 58, pal::SUB, F11, CENTER);
    int top = std::max(0, shopSel - 9);
    for (int i = top; i < shopN && i < top + 10; i++) {
      char l[80];
      shopLabel(shopList[i], l, sizeof l);
      int y = 80 + (i - top) * 20;
      row(i, y, i == shopSel, l);
      if (shopList[i].k != SH_SELL) { snprintf(s, sizeof s, "%d", price(shopList[i])); text(s, W - 22, y, i == shopSel ? pal::SELTXT : pal::AMBER, F11B, RIGHT); }
    }
    text("OK — купити/продати · Назад — вийти", W / 2, 292, pal::DIM, F10B, CENTER);
  } else if (mode == M_NIGHT) {
    std::string o[1] = {"Доброго ранку!"};
    uiDialog("НОВИЙ ДЕНЬ", nightText, o, 1, 0);
  }
  if (mode == M_MENU && menuSel == 4) {
    snprintf(s, sizeof s, "Мета: заробити %ld грн. Зароблено %ld.\nПоради: полий посіви щодня (дощ поливає сам). Кури й корови їдять сіно — коси бур'ян. Розбризкувач економить сили. У кожного насіння свій сезон.", (long)GOAL, (long)g.earned);
    auto ls = wrap(s, W - 40, F11);
    int h = 20 + 14 * (int)ls.size();
    fill(14, 228 - h + 70, W - 28, h, pal::BG);
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], 20, 228 - h + 86 + 14 * (int)i, pal::SUB, F11);
  }
}

void setup() {
  consoleBegin("farm");
  inputSetRepeat(REPEAT_MENU, 1000, 130);   // a tap (up to ~0.4 s) = one step; hold 1 s to keep walking
  Game* l = new Game;
  if (loadData(l, sizeof *l) && l->magic == MAGIC) { g = *l; say("З поверненням на ферму!"); }
  else newWorld();
  delete l;
  if (g.seedSel >= NCROP) g.seedSel = 0;
  draw();
  present();
}

void loop() {
  consolePoll();
  bool changed = false;
  for (int k; (k = inputGet()) >= 0;) {
    changed = true;
    if (k == K_BACK_LONG) {
      saveData(&g, sizeof g); dirty = false;
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (mode == M_PLAY) {
      if (k <= K_RIGHT) {
        // first press only turns the farmer; pressing (or holding) the same direction again walks
        if (g.face != k) g.face = k;
        else {
          int nx = g.px + DX[k], ny = g.py + DY[k];
          if (walkable(nx, ny)) { g.px = nx; g.py = ny; }
        }
        dirty = true; lastChange = nowMs();
      }
      if (k == K_OK) action();
      if (k == K_BACK) { mode = M_MENU; menuSel = 0; }
    } else if (mode == M_MENU) {
      if (k == K_UP) menuSel = (menuSel + NMENU - 1) % NMENU;
      if (k == K_DOWN) menuSel = (menuSel + 1) % NMENU;
      if (k == K_BACK) mode = M_PLAY;
      if (k == K_OK) {
        mode = M_PLAY;
        switch (menuSel) {
          case 0: mode = M_SEEDS; break;
          case 1: mode = M_INV; break;
          case 2: placeSprinkler(); break;
          case 3: nextDay(); break;
          default: break;
        }
      }
    } else if (mode == M_SEEDS) {
      if (k == K_UP) g.seedSel = (g.seedSel + NCROP - 1) % NCROP;
      if (k == K_DOWN) g.seedSel = (g.seedSel + 1) % NCROP;
      if (k == K_OK || k == K_BACK) mode = M_PLAY;
    } else if (mode == M_INV) {
      if (k == K_OK || k == K_BACK) mode = M_PLAY;
    } else if (mode == M_SHOP) {
      buildShop();
      if (shopSel >= shopN) shopSel = shopN - 1;
      if (k == K_UP) shopSel = (shopSel + shopN - 1) % shopN;
      if (k == K_DOWN) shopSel = (shopSel + 1) % shopN;
      if (k == K_OK) { buy(shopList[shopSel]); dirty = true; lastChange = nowMs(); }
      if (k == K_BACK) mode = M_PLAY;
    } else if (mode == M_NIGHT) {
      if (k == K_OK) mode = M_PLAY;
    }
  }
  if (mode == M_SHOP) buildShop();
  // animated water/rain/snow and messages need regular redraws
  static uint32_t lastAnim;
  if (nowMs() - lastAnim > 120) { lastAnim = nowMs(); changed = true; }
  if (changed) { draw(); present(); }
  if (dirty && nowMs() - lastChange > 3000) { saveData(&g, sizeof g); dirty = false; }
  delay(8);
}
