// Тамагочі для кишенькової консолі.
// ◀▶ — вибрати дію, OK — виконати, Назад — закрити, довге Назад — меню.
// Годинника з батарейкою в консолі немає, тож поки вона вимкнена, улюбленець спить і час стоїть.
#include <Arduino.h>
#include "BomzhCore.h"
#include <math.h>

using namespace gfx;

// ---------- sprites: '#' outline, 'o' body, 'p' cheeks, 'w' white, '.' transparent ----------
#define SP(n, ...) static const char* const n[16] = {__VA_ARGS__}
SP(EGG, "......####......", "....##wwww##....", "...#wwwwwwww#...", "..#wwoowwwwww#..", "..#woooowwwww#..", ".#wwwoowwwwwww#.",
        ".#wwwwwwwwwwww#.", ".#wwwwwwwoowww#.", ".#wwwwwwoooow#..", ".#wwwwwwwoowww#.", "..#wwwwwwwwww#..", "..#wwwwwwwwww#..",
        "...#wwwwwwww#...", "....##wwww##....", "......####......", "................");
SP(BABY, "................", "................", "................", "................", "......####......", "....##oooo##....",
         "...#oooooooo#...", "..#oooooooooo#..", "..#oooooooooo#..", "..#oppooooppo#..", "..#oooooooooo#..", "..#oooooooooo#..",
         "...#oooooooo#...", "....########....", "................", "................");
SP(CHILD, "................", "...##......##...", "..#oo#....#oo#..", "..#ooo####ooo#..", ".#oooooooooooo#.", ".#oooooooooooo#.",
          ".#oooooooooooo#.", ".#oooooooooooo#.", ".#oppooooooppo#.", ".#oooooooooooo#.", "..#oooooooooo#..", "..#oooooooooo#..",
          "...#oo####oo#...", "...#oo#..#oo#...", "....##....##....", "................");
SP(ADULT, "..##........##..", ".#oo#......#oo#.", ".#ooo######ooo#.", "#oooooooooooooo#", "#oooooooooooooo#", "#oooooooooooooo#",
          "#oooooooooooooo#", "#oooooooooooooo#", "#opppooooooppo##", "#oooooooooooooo#", "#oooooooooooooo#", ".#oooooooooooo#.",
          "..#oooooooooo#..", "..#oo######oo#..", "..#oo#....#oo#..", "...##......##...");
SP(POOP, "................", "................", "................", "................", "................", "................",
         "................", "................", "......##........", ".....#oo#.......", "....#oooo#......", "...#oooooo#.....",
         "..#oooooooo#....", "..##########....", "................", "................");

enum Stage : uint8_t { S_EGG, S_BABY, S_CHILD, S_ADULT, S_GONE };
struct Pet {
  uint32_t magic;
  uint8_t stage, form, name, asleep, lightsOff, sick, poops;
  float hunger, happy, energy, clean, health;
  uint32_t ageSec, sinceMeal;
  uint16_t mistakes, weight, coins, gen;
  uint8_t candies;
};
static Pet pet;
static const uint32_t MAGIC = 0x54414d31;
static const char* NAMES[] = {"Бублик", "Пиріжок", "Ґудзик", "Квасолька", "Мурчик", "Тофу", "Пончик", "Шкварка"};
static const int NNAMES = sizeof NAMES / sizeof NAMES[0];
static const char* FORMS[3] = {"Зірочка", "Пухнастик", "Бурчало"};
static const uint32_t FORMC[3] = {0xffc83a, 0x6aa8ff, 0x8aa070};

// ---------- UI state ----------
enum Act { A_FOOD, A_PLAY, A_CLEAN, A_MED, A_LIGHT, A_INFO, NACT };
static const char* ACTNAME[NACT] = {"Їжа", "Гра", "Мити", "Ліки", "Світло", "Стан"};
static int act = 0;
enum Mode { M_MAIN, M_FOOD, M_INFO, M_GAME, M_ANIM, M_GONE };
static Mode mode = M_MAIN;
static int foodSel;
static int anim;          // 1 eat, 2 cheer, 3 refuse, 4 wash, 5 shot
static uint32_t animUntil, lastTick, lastSave, msgUntil;
static char msg[192];
// mini-game
static int gRound, gWins, gGuess, gDir;
static uint32_t gAt;
static int gPhase;        // 0 waiting for guess, 1 showing result

static int rnd(int n) { return (int)(randomU32() % (uint32_t)n); }
static float clamp(float v) { return v < 0 ? 0 : v > 100 ? 100 : v; }
static void say(const char* s) { strncpy(msg, s, sizeof msg - 1); msgUntil = nowMs() + 2200; }

static void newEgg() {
  uint16_t gen = pet.gen + 1;
  memset(&pet, 0, sizeof pet);
  pet.magic = MAGIC; pet.gen = gen;
  pet.stage = S_EGG; pet.name = rnd(NNAMES);
  pet.hunger = 70; pet.happy = 70; pet.energy = 90; pet.clean = 100; pet.health = 100; pet.weight = 5;
  say("З'явилося яйце! Скоро хтось вилупиться.");
}

static void save() { saveData(&pet, sizeof pet); lastSave = nowMs(); }

// ---------- simulation: one call per second of play ----------
static void tick() {
  if (pet.stage == S_GONE) return;
  pet.ageSec++;
  if (pet.stage == S_EGG) { if (pet.ageSec >= 40) { pet.stage = S_BABY; say("Вилупився! Привіт!"); sfxWin(); } return; }
  float m = 1.0f / 60;   // per-second share of the per-minute rates below
  bool sleep = pet.asleep;
  pet.hunger = clamp(pet.hunger - (sleep ? 2.5f : 6) * m);
  pet.happy = clamp(pet.happy - (sleep ? 1 : 4) * m);
  pet.energy = clamp(pet.energy + (sleep ? (pet.lightsOff ? 14 : 5) : -3.5f) * m);
  pet.clean = clamp(pet.clean - (1 + pet.poops * 3) * m);
  pet.sinceMeal++;
  if (!sleep && pet.sinceMeal > 150 && pet.poops < 4 && rnd(90) == 0) { pet.poops++; pet.sinceMeal = 0; }
  // falling asleep / waking up by itself
  if (!sleep && pet.energy < 8) { pet.asleep = 1; say("Заснув просто так… Вимкни світло."); }
  if (sleep && pet.energy >= 99) { pet.asleep = 0; say("Виспався!"); }
  // illness
  if (!pet.sick && ((pet.clean < 25 && rnd(240) == 0) || (pet.hunger < 5 && rnd(300) == 0) || (pet.candies > 4 && rnd(120) == 0))) {
    pet.sick = 1; say("Захворів! Дай ліки."); sfxHit();
  }
  float dmg = 0;
  if (pet.sick) dmg += 2.5f;
  if (pet.hunger < 1) dmg += 2;
  if (pet.happy < 1) dmg += 0.5f;
  if (dmg > 0) pet.health = clamp(pet.health - dmg * m);
  else pet.health = clamp(pet.health + 2 * m);
  if (pet.ageSec % 60 == 0) {   // once a minute: count care mistakes, digest candies
    if (pet.hunger < 5 || pet.happy < 5 || pet.poops >= 3 || pet.sick || (pet.asleep && !pet.lightsOff)) pet.mistakes++;
    if (pet.candies) pet.candies--;
  }
  // growing up (1 game day = 5 minutes of play)
  if (pet.stage == S_BABY && pet.ageSec >= 8 * 60) { pet.stage = S_CHILD; say("Підріс! Тепер він дитина."); }
  if (pet.stage == S_CHILD && pet.ageSec >= 30 * 60) {
    pet.stage = S_ADULT;
    pet.form = pet.mistakes <= 3 ? 0 : pet.mistakes <= 10 ? 1 : 2;
    char b[64];
    snprintf(b, sizeof b, "Виріс! Тепер це %s.", FORMS[pet.form]);
    say(b);
  }
  if (pet.health <= 0) { pet.stage = S_GONE; mode = M_GONE; save(); sfxLose(); }
}

// ---------- drawing ----------
static void sprite(const char* const* s, int x, int y, int px, uint16_t body, bool flip = false) {
  uint16_t line = rgb(0x1a1a24), cheek = rgb(0xff8aa8), white = rgb(0xffffff);
  for (int r = 0; r < 16; r++)
    for (int c = 0; c < 16; c++) {
      char ch = s[r][flip ? 15 - c : c];
      if (ch == '.') continue;
      uint16_t col = ch == '#' ? line : ch == 'p' ? cheek : ch == 'w' ? white : body;
      fill(x + c * px, y + r * px, px, px, col);
    }
}

static void face(int cx, int top, int px, bool flip, uint16_t body) {   // eyes and mouth over the body depending on mood
  uint16_t ink = rgb(0x1a1a24);
  int ey = top + (pet.stage == S_BABY ? 7 : 6) * px, ex1 = cx - 3 * px, ex2 = cx + 2 * px, my = ey + 3 * px;
  int look = flip ? -px / 2 : px / 2;
  bool sleep = pet.asleep, sad = pet.happy < 25 || pet.sick, happyNow = anim == 2 || (pet.happy > 75 && !pet.sick);
  if (sleep) {
    fill(ex1, ey + px / 2, px * 2, px / 2 + 1, ink); fill(ex2, ey + px / 2, px * 2, px / 2 + 1, ink);
  } else if (happyNow) {   // ^ ^
    for (int e : {ex1, ex2}) {
      tri(e, ey + px, e + px, ey - px / 2, e + 2 * px, ey + px, ink);
      tri(e + px / 2, ey + px + 1, e + px, ey + px / 3 + 1, e + px * 3 / 2, ey + px + 1, body);
    }
  } else {
    fill(ex1 + look, ey - px / 2, px * 3 / 2, px * 2, ink); fill(ex2 + look, ey - px / 2, px * 3 / 2, px * 2, ink);
    fill(ex1 + look + px / 4, ey - px / 4, px / 2, px / 2, rgb(0xffffff)); fill(ex2 + look + px / 4, ey - px / 4, px / 2, px / 2, rgb(0xffffff));
  }
  if (anim == 1 && (nowMs() / 180) % 2) fillRound(cx - px, my - px / 2, px * 2, px * 2, px / 2, ink);   // chewing
  else if (sad && !sleep) { fill(cx - px, my + px / 2, px * 2, px / 2 + 1, ink); fill(cx - px - px / 2, my + px, px / 2, px / 2, ink); fill(cx + px, my + px, px / 2, px / 2, ink); }
  else if (!sleep) { fill(cx - px, my, px * 2, px / 2 + 1, ink); fill(cx - px - px / 2, my - px / 2, px / 2, px / 2, ink); fill(cx + px, my - px / 2, px / 2, px / 2, ink); }
  if (pet.stage == S_ADULT && pet.form == 2 && !sleep) {   // grumpy brows
    tri(ex1 - px / 2, ey - px * 3 / 2, ex1 + px * 2, ey - px / 2, ex1 + px * 2, ey - px, ink);
    tri(ex2 + px * 2, ey - px * 3 / 2, ex2, ey - px / 2, ex2, ey - px, ink);
  }
}

static uint16_t bodyColor() {
  uint16_t c = pet.stage == S_ADULT ? rgb(FORMC[pet.form]) : pet.stage == S_CHILD ? rgb(0xff9ec0) : rgb(0xffd8a0);
  if (pet.sick) c = mix565(c, rgb(0x9ad06a), 110);
  return c;
}

static void bar(int x, int y, int w, const char* label, float v, uint16_t c) {
  const int L = 46;
  text(label, x, y + 8, pal::SUB, F9B);
  fillRound(x + L, y, w - L, 9, 4, pal::LINE);
  int f = (int)((w - L) * v / 100);
  if (f > 3) fillRound(x + L, y, f, 9, 4, v < 25 ? pal::RED : c);
}

static void icon(int a, int x, int y, uint16_t c) {   // tiny action icons
  switch (a) {
    case A_FOOD: fillCircle(x + 9, y + 10, 7, c); fill(x + 2, y + 9, 15, 2, pal::BG); fill(x + 7, y + 1, 4, 4, c); break;
    case A_PLAY: fillCircle(x + 9, y + 9, 8, c); fill(x + 1, y + 8, 17, 2, pal::BG); fill(x + 8, y + 1, 2, 17, pal::BG); break;
    case A_CLEAN: for (int i = 0; i < 3; i++) fillCircle(x + 4 + i * 5, y + 12 - (i % 2) * 5, 4 - (i % 2), c); fill(x + 2, y + 16, 15, 2, c); break;
    case A_MED: fillRound(x + 6, y + 1, 7, 17, 3, c); fill(x + 6, y + 9, 7, 2, pal::BG); break;
    case A_LIGHT: fillCircle(x + 9, y + 7, 6, c); fill(x + 6, y + 13, 7, 5, c); break;
    case A_INFO: fill(x + 3, y + 3, 13, 2, c); fill(x + 3, y + 8, 13, 2, c); fill(x + 3, y + 13, 9, 2, c); break;
  }
}

static void drawRoom() {
  bool dark = pet.lightsOff;
  uint16_t wall = dark ? rgb(0x141a2a) : rgb(0xbfe0f0), floorc = dark ? rgb(0x1e2232) : rgb(0xe8c89a);
  fill(0, 56, W, 190, wall);
  fill(0, 200, W, 46, floorc);
  fill(0, 200, W, 2, dark ? rgb(0x2a3048) : rgb(0xc8a070));
  // window
  fillRound(20, 76, 50, 44, 4, dark ? rgb(0x2a3048) : rgb(0xffffff));
  fill(23, 79, 44, 38, dark ? rgb(0x0a0e1e) : rgb(0x8ad0ff));
  fill(44, 79, 2, 38, dark ? rgb(0x2a3048) : rgb(0xffffff));
  if (dark) { fillCircle(34, 92, 5, rgb(0xf0f0c0)); fill(55, 86, 2, 2, rgb(0xffffff)); fill(60, 100, 2, 2, rgb(0xffffff)); }
  else fillCircle(58, 90, 6, rgb(0xffe060));
}

static void drawPet() {
  if (pet.stage == S_GONE) return;
  int px = pet.stage == S_EGG ? 5 : pet.stage == S_BABY ? 6 : pet.stage == S_CHILD ? 6 : 7;
  uint32_t t = nowMs();
  int bob = pet.asleep ? 0 : (int)(sinf(t / 260.0f) * 3);
  int wander = pet.asleep || pet.stage == S_EGG ? 0 : (int)(sinf(t / 2300.0f) * 30);
  bool flip = cosf(t / 2300.0f) < 0;
  if (anim == 2) bob = -(int)(fabsf(sinf(t / 120.0f)) * 14);
  int size = 16 * px, x = W / 2 - size / 2 + wander, y = 200 - size + 6 + bob;
  if (pet.stage == S_EGG) {
    int wob = (pet.ageSec > 25 && (t / 150) % 2) ? 2 : 0;
    sprite(EGG, x + wob, y, px, rgb(0x8ad06a));
    return;
  }
  const char* const* s = pet.stage == S_BABY ? BABY : pet.stage == S_CHILD ? CHILD : ADULT;
  sprite(s, x, y, px, bodyColor(), flip);
  face(x + size / 2, y, px, flip, bodyColor());
  if (pet.stage == S_ADULT && pet.form == 0) {   // little crown for the best-cared pet
    int cx = x + size / 2;
    tri(cx - 14, y + 4, cx - 14, y - 10, cx - 6, y, rgb(0xffcc00));
    tri(cx - 6, y + 4, cx, y - 14, cx + 6, y + 4, rgb(0xffcc00));
    tri(cx + 14, y + 4, cx + 14, y - 10, cx + 6, y, rgb(0xffcc00));
  }
  if (pet.asleep) {
    int zx = x + size - 4, zy = y - 6 - (int)((t / 40) % 30);
    text("z", zx, zy + 18, pal::BLUE, F12B);
    text("Z", zx + 10, zy + 4, pal::BLUE, F16B);
  }
  if (pet.sick) { fillRound(x + size - 6, y + 6, 18, 6, 3, rgb(0xffffff)); fill(x + size + 8, y + 7, 4, 4, rgb(0xff4d4f)); }
  if (anim == 1 && t < animUntil) {   // food flying in
    int k = (int)(animUntil - t) / 20;
    fillCircle(x + size / 2 + k, y + size / 2 + 4, 6, foodSel == 0 ? rgb(0xf0e0b0) : rgb(0xff5aa8));
  }
  if (anim == 4 && t < animUntil) for (int i = 0; i < 8; i++) fillCircle(x + rnd(size), y + rnd(size), 3 + rnd(4), rgb(0xffffff));
}

static void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    int x = 16 + (i % 2) * 180 + (i / 2) * 20, y = 214 + (i / 2) * 4;
    sprite(POOP, x - 8, y - 32, 2, rgb(0x8a5a2a));
    if ((nowMs() / 300 + i) % 2) fill(x + 4, y - 18 - i % 3, 2, 4, rgb(0x6a6a6a));
  }
}

static void draw() {
  fill(0, 0, W, H, pal::BG);
  char s[256];
  if (pet.stage == S_EGG) snprintf(s, sizeof s, "Яйце");
  else snprintf(s, sizeof s, "%s", NAMES[pet.name]);
  text(s, 10, 20, pal::AMBER, F16B);
  static const char* ST[5] = {"яйце", "малюк", "дитина", "дорослий", ""};
  snprintf(s, sizeof s, "%s · %u дн.", pet.stage == S_ADULT ? FORMS[pet.form] : ST[pet.stage], (unsigned)(pet.ageSec / 300));
  text(s, W - 10, 18, pal::SUB, F11, RIGHT);
  bar(6, 28, 112, "їжа", pet.hunger, rgb(0xffb21a));
  bar(126, 28, 108, "радість", pet.happy, rgb(0xff6aa8));
  bar(6, 41, 112, "енергія", pet.energy, rgb(0x4aa8ff));
  bar(126, 41, 108, "чистота", pet.clean, rgb(0x4cd964));
  drawRoom();
  drawPoops();
  drawPet();
  // message bubble
  if (nowMs() < msgUntil) {
    auto ls = wrap(msg, W - 40, F11);
    int h = 10 + 14 * (int)ls.size();
    fillRound(14, 60, W - 28, h, 6, rgb(0xffffff));
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], W / 2, 73 + 14 * (int)i, rgb(0x1a1a24), F11, CENTER);
  }
  // action bar
  fill(0, 248, W, 72, pal::PANEL);
  for (int i = 0; i < NACT; i++) {
    int x = 6 + i * 38, y = 254;
    bool sel = i == act && mode == M_MAIN;
    fillRound(x, y, 36, 30, 6, sel ? pal::AMBER : pal::BG);
    uint16_t c = sel ? pal::SELTXT : (i == A_MED && pet.sick) ? pal::RED : (i == A_CLEAN && pet.poops) ? pal::YELLOW : pal::SUB;
    icon(i, x + 8, y + 5, c);
  }
  text(ACTNAME[act], W / 2, 300, pal::TXT, F12B, CENTER);
  text(pet.stage == S_GONE ? "" : "◀▶ — дія · OK — зробити", W / 2, 315, pal::DIM, F10B, CENTER);

  if (mode == M_FOOD) {
    std::string o[2] = {"Каша", "Цукерка"};
    uiDialog("ЧИМ ПОГОДУВАТИ?", "Каша — ситно.\nЦукерка — весело, але багато шкідливо.", o, 2, foodSel);
  } else if (mode == M_INFO) {
    blendRect(0, 0, W, H, pal::SHADE, 180);
    fill(16, 60, 208, 200, pal::PANEL);
    strokeRect(16, 60, 208, 200, pal::AMBER);
    text("СТАН", W / 2, 80, pal::AMBER, F12B, CENTER);
    int y = 96;
    bar(28, y, 184, "їжа", pet.hunger, rgb(0xffb21a)); y += 18;
    bar(28, y, 184, "радість", pet.happy, rgb(0xff6aa8)); y += 18;
    bar(28, y, 184, "енергія", pet.energy, rgb(0x4aa8ff)); y += 18;
    bar(28, y, 184, "чистота", pet.clean, rgb(0x4cd964)); y += 18;
    bar(28, y, 184, "здоров'я", pet.health, rgb(0xff4d4f)); y += 26;
    snprintf(s, sizeof s, "Вага %u г · вік %u хв гри", pet.weight * 100, (unsigned)(pet.ageSec / 60));
    text(s, W / 2, y, pal::TXT, F11, CENTER); y += 16;
    snprintf(s, sizeof s, "Помилок догляду: %u", pet.mistakes);
    text(s, W / 2, y, pet.mistakes > 10 ? pal::RED : pal::SUB, F11, CENTER); y += 16;
    snprintf(s, sizeof s, "Монетки: %u · покоління %u", pet.coins, pet.gen);
    text(s, W / 2, y, pal::SUB, F11, CENTER);
    text("Назад — закрити", W / 2, 252, pal::DIM, F10B, CENTER);
  } else if (mode == M_GAME) {
    blendRect(0, 0, W, H, pal::SHADE, 150);
    fill(16, 70, 208, 170, pal::PANEL);
    strokeRect(16, 70, 208, 170, pal::AMBER);
    text("КУДИ ПОВЕРНЕТЬСЯ?", W / 2, 90, pal::AMBER, F12B, CENTER);
    snprintf(s, sizeof s, "Раунд %d з 5 · вгадано %d", gRound + 1 > 5 ? 5 : gRound + 1, gWins);
    text(s, W / 2, 108, pal::SUB, F11, CENTER);
    sprite(pet.stage == S_BABY ? BABY : pet.stage == S_CHILD ? CHILD : ADULT, W / 2 - 32, 118, 4, bodyColor(), gPhase == 1 && gDir < 0);
    if (gPhase == 0) text("◀ або ▶ — вгадай", W / 2, 206, pal::TXT, F12B, CENTER);
    else text(gGuess == gDir ? "Вгадав!" : "Не вгадав", W / 2, 206, gGuess == gDir ? pal::GREEN : pal::RED, F13B, CENTER);
    text("Назад — вийти з гри", W / 2, 228, pal::DIM, F10B, CENTER);
  } else if (mode == M_GONE) {
    std::string o[1] = {"Нове яйце"};
    snprintf(s, sizeof s, "%s утік від поганого догляду, щоб знайти кращий дім.\nПрожив %u дн.", NAMES[pet.name], (unsigned)(pet.ageSec / 300));
    uiDialog("ОЙ…", s, o, 1, 0);
  }
}

// ---------- actions ----------
static void doAction() {
  sfxSelect();
  if (pet.stage == S_EGG && act != A_INFO) { say("Це ще яйце. Почекай, поки вилупиться."); return; }
  if (pet.asleep && act != A_LIGHT && act != A_INFO) { say("Ш-ш-ш, він спить. Спершу ввімкни світло."); return; }
  switch (act) {
    case A_FOOD: mode = M_FOOD; foodSel = 0; break;
    case A_PLAY:
      if (pet.energy < 15) { say("Надто втомився, щоб гратися."); break; }
      mode = M_GAME; gRound = 0; gWins = 0; gPhase = 0; break;
    case A_CLEAN:
      if (!pet.poops && pet.clean > 90) { say("І так чистенький!"); break; }
      pet.poops = 0; pet.clean = 100; anim = 4; animUntil = nowMs() + 900; say("Блищить!"); break;
    case A_MED:
      if (!pet.sick) { say("Він здоровий, ліки не потрібні."); pet.happy = clamp(pet.happy - 5); break; }
      pet.sick = 0; pet.health = clamp(pet.health + 20); pet.happy = clamp(pet.happy - 10); anim = 5; animUntil = nowMs() + 600; say("Ліки подіяли. Одужує!"); break;
    case A_LIGHT:
      pet.lightsOff ^= 1;
      if (pet.lightsOff && !pet.asleep && pet.energy < 70) { pet.asleep = 1; say("На добраніч!"); }
      else if (!pet.lightsOff && pet.asleep) { pet.asleep = 0; say(pet.energy > 70 ? "Доброго ранку!" : "Ще сонний…"); }
      break;
    case A_INFO: mode = M_INFO; break;
  }
}

static void feed(int what) {
  mode = M_MAIN;
  if (pet.hunger > 92 && what == 0) { anim = 3; animUntil = nowMs() + 800; say("Не хочу! Я ситий."); return; }
  if (what == 0) { pet.hunger = clamp(pet.hunger + 30); pet.weight++; }
  else { pet.happy = clamp(pet.happy + 15); pet.hunger = clamp(pet.hunger + 5); pet.weight += 2; pet.candies++; }
  pet.sinceMeal = 0;
  anim = 1; animUntil = nowMs() + 1400;
  say(what == 0 ? "Ням-ням!" : pet.candies > 4 ? "Солодко… але живіт болить." : "Смакота!");
}

void setup() {
  consoleBegin("tamagotchi");
  inputSetRepeat(0);
  Pet l;
  if (loadData(&l, sizeof l) && l.magic == MAGIC) {
    pet = l;
    if (pet.stage == S_GONE) mode = M_GONE;
    else say(pet.asleep ? "Він ще спить." : "Привіт! Я скучив.");
  } else { memset(&pet, 0, sizeof pet); newEgg(); }
  lastTick = lastSave = nowMs();
  draw();
  present();
}

void loop() {
  consolePoll();
  uint32_t now = nowMs();
  for (int k; (k = inputGet()) >= 0;) {
    if (k == K_BACK_LONG) {
      save();
      draw();
      if (confirmExit()) returnToLauncher();
      continue;
    }
    if (mode == M_MAIN) {
      if (k == K_LEFT) { act = (act + NACT - 1) % NACT; sfxClick(); }
      if (k == K_RIGHT) { act = (act + 1) % NACT; sfxClick(); }
      if (k == K_OK) doAction();
    } else if (mode == M_FOOD) {
      if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) foodSel ^= 1;
      if (k == K_OK) feed(foodSel);
      if (k == K_BACK) mode = M_MAIN;
    } else if (mode == M_INFO) {
      if (k == K_BACK || k == K_OK) mode = M_MAIN;
    } else if (mode == M_GAME) {
      if (k == K_BACK) { mode = M_MAIN; continue; }
      if (gPhase == 0 && (k == K_LEFT || k == K_RIGHT)) {
        gGuess = k == K_LEFT ? -1 : 1; gDir = rnd(2) ? 1 : -1;
        if (gGuess == gDir) { gWins++; sfxCoin(); } else sfxClick();
        gPhase = 1; gAt = now;
      }
    } else if (mode == M_GONE) {
      if (k == K_OK) { newEgg(); mode = M_MAIN; save(); }
    }
  }
  if (mode == M_GAME && gPhase == 1 && now - gAt > 900) {
    gRound++; gPhase = 0;
    if (gRound >= 5) {
      mode = M_MAIN;
      pet.energy = clamp(pet.energy - 8); pet.weight = pet.weight > 3 ? pet.weight - 1 : pet.weight;
      if (gWins >= 3) { pet.happy = clamp(pet.happy + 25); pet.coins += gWins; anim = 2; animUntil = now + 1500; say("Перемога! Він дуже радий."); }
      else { pet.happy = clamp(pet.happy + 10); say("Не вийшло, але гратися було весело."); }
    }
  }
  if (anim && now > animUntil) anim = 0;
  while (now - lastTick >= 1000) { lastTick += 1000; tick(); }
  if (now - lastSave > 30000) save();
  draw();
  present();
  delay(20);
}
