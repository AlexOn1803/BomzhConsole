// Шашки проти комп'ютера для кишенькової консолі (правила російських/українських шашок:
// бити обов'язково, прості б'ють і назад, дамка ходить і б'є на будь-яку відстань).
// ▲▼◀▶ — курсор, OK — вибрати шашку / куди ходити, Назад — скасувати або хід назад, довге Назад — меню.
#include <Arduino.h>
#include "BomzhCore.h"
#include <algorithm>

using namespace gfx;

#ifdef ARDUINO
// the AI search is recursive and keeps move lists on the stack: give loop() a bigger stack than the default 8 KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

static const int CS = 28, BX = (240 - 8 * CS) / 2, BY = 52, MAXM = 64, MAXP = 12;
enum : int8_t { E = 0, WM = 1, WK = 2, BM_ = -1, BK = -2 };   // white = player (bottom), black = computer
static const uint16_t LIGHT = rgb(0xe8d5b0), DARK = rgb(0x8a5a36), DARK_SEL = rgb(0x6a7a3a), WHITE_P = rgb(0xf4efe6),
                      WHITE_D = rgb(0xb8b0a0), BLACK_P = rgb(0x2a2420), BLACK_D = rgb(0x5a4e44), CROWN = rgb(0xffcc00);

struct Move { uint8_t path[MAXP]; uint8_t n; uint8_t cap[MAXP]; uint8_t nc; bool crown; };   // squares 0..63
struct Board { int8_t s[64]; };
struct Save { uint32_t magic; Board b; uint8_t turn, level, over, nh; Board hist[24]; uint16_t wins, losses, draws; uint16_t quiet; };
static Save sv;
static const uint32_t MAGIC = 0x43484b31;

static Move legal[MAXM];
static int nLegal;
static int cx = 1, cy = 6, sel = -1;
enum Mode { M_SETUP, M_PLAY, M_THINK, M_OVER, M_MENU };
static Mode mode = M_SETUP;
static int setupSel = 1, menuSel = 0;
static Move lastAi;
static bool hasLastAi;
static const char* overText = "";

static inline bool dark(int sq) { return ((sq / 8) + (sq % 8)) % 2 == 1; }
static inline bool mine(int8_t p, int side) { return side > 0 ? p > 0 : p < 0; }

// ---------- move generation ----------
static void addCapture(Board& b, int side, int from, int8_t piece, Move& cur, Move* out, int& n, bool* taken) {
  bool king = piece == WK || piece == BK;
  int fx = from % 8, fy = from / 8;
  bool more = false;
  static const int D[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
  for (auto& d : D) {
    int x = fx + d[0], y = fy + d[1];
    if (king) while (x >= 0 && y >= 0 && x < 8 && y < 8 && b.s[y * 8 + x] == E) { x += d[0]; y += d[1]; }
    if (x < 0 || y < 0 || x >= 8 || y >= 8) continue;
    int mid = y * 8 + x;
    if (!(side > 0 ? b.s[mid] < 0 : b.s[mid] > 0) || taken[mid]) continue;
    int lx = x + d[0], ly = y + d[1];
    while (lx >= 0 && ly >= 0 && lx < 8 && ly < 8 && b.s[ly * 8 + lx] == E) {
      int land = ly * 8 + lx;
      Move nx = cur;
      if (nx.n >= MAXP || nx.nc >= MAXP) return;
      nx.path[nx.n++] = land;
      nx.cap[nx.nc++] = mid;
      int8_t np = piece;
      bool promo = !king && (side > 0 ? ly == 0 : ly == 7);
      if (promo) { np = side > 0 ? WK : BK; nx.crown = true; }
      taken[mid] = true;
      int8_t save = b.s[from];
      b.s[from] = E;
      b.s[land] = np;
      int before = n;
      addCapture(b, side, land, np, nx, out, n, taken);
      b.s[land] = E;
      b.s[from] = save;
      taken[mid] = false;
      if (n == before && n < MAXM) out[n++] = nx;   // sequence ends here
      more = true;
      if (!king) break;
      lx += d[0]; ly += d[1];
    }
  }
  (void)more;
}

static int genMoves(Board& b, int side, Move* out) {
  int n = 0;
  bool taken[64] = {};
  for (int sq = 0; sq < 64; sq++) {
    if (!mine(b.s[sq], side)) continue;
    Move m = {};
    m.path[0] = sq; m.n = 1;
    addCapture(b, side, sq, b.s[sq], m, out, n, taken);
  }
  if (n) return n;   // capturing is compulsory
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = b.s[sq];
    if (!mine(p, side)) continue;
    bool king = p == WK || p == BK;
    int fx = sq % 8, fy = sq / 8;
    static const int D[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (auto& d : D) {
      if (!king && (side > 0 ? d[1] > 0 : d[1] < 0)) continue;   // men move forward only
      int x = fx + d[0], y = fy + d[1];
      while (x >= 0 && y >= 0 && x < 8 && y < 8 && b.s[y * 8 + x] == E && n < MAXM) {
        Move m = {};
        m.path[0] = sq; m.path[1] = y * 8 + x; m.n = 2;
        m.crown = !king && (side > 0 ? y == 0 : y == 7);
        out[n++] = m;
        if (!king) break;
        x += d[0]; y += d[1];
      }
    }
  }
  return n;
}

static void apply(Board& b, const Move& m) {
  int8_t p = b.s[m.path[0]];
  b.s[m.path[0]] = E;
  for (int i = 0; i < m.nc; i++) b.s[m.cap[i]] = E;
  if (m.crown) p = p > 0 ? WK : BK;
  b.s[m.path[m.n - 1]] = p;
}

// ---------- AI: alpha-beta ----------
static int evaluate(const Board& b) {   // from black's (computer's) point of view
  int s = 0;
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = b.s[sq];
    if (!p) continue;
    int x = sq % 8, y = sq / 8, v;
    if (p == WM) v = 100 + (7 - y) * 4 + (x > 1 && x < 6 ? 4 : 0) + (y == 7 ? 6 : 0);
    else if (p == BM_) v = 100 + y * 4 + (x > 1 && x < 6 ? 4 : 0) + (y == 0 ? 6 : 0);
    else v = 320 + ((x == y || x + y == 7) ? 12 : 0);    // kings like the long diagonals
    s += p > 0 ? -v : v;
  }
  return s;
}

static uint32_t nodes;
// move lists live in a static pool (one list per search depth) instead of on the stack:
// deep searches with long capture chains would otherwise overflow the task stack and reboot the board
static const int MAXPLY = 20;
static Move pool[MAXPLY][MAXM];
static int maxPlySeen;

static int search(Board& b, int side, int depth, int alpha, int beta, int ply) {
  if ((++nodes & 2047) == 0) delay(1);   // let the system breathe (watchdog)
  if (ply > maxPlySeen) maxPlySeen = ply;
  if (ply >= MAXPLY) { int e = evaluate(b); return side < 0 ? e : -e; }
  Move* ms = pool[ply];
  int n = genMoves(b, side, ms);
  if (!n) return -100000 - depth;   // side to move has lost (sooner = worse)
  if (depth <= 0 && ms[0].nc == 0) { int e = evaluate(b); return side < 0 ? e : -e; }   // quiet: stop (captures are searched on)
  if (depth < -6) { int e = evaluate(b); return side < 0 ? e : -e; }
  // captures first, bigger captures first
  std::sort(ms, ms + n, [](const Move& a, const Move& c) { return a.nc > c.nc; });
  for (int i = 0; i < n; i++) {
    Board nb = b;
    apply(nb, ms[i]);
    int v = -search(nb, -side, depth - 1, -beta, -alpha, ply + 1);
    if (v > alpha) alpha = v;
    if (alpha >= beta) break;
  }
  return alpha;
}

static bool aiMove(Move& best) {
  Move* ms = pool[0];
  int n = genMoves(sv.b, -1, ms);
  if (!n) return false;
  static const int DEPTH[3] = {2, 4, 6};
  int depth = DEPTH[sv.level], bestV = -1000000;
  nodes = 0;
  // small shuffle so equal moves vary from game to game
  for (int i = n - 1; i > 0; i--) std::swap(ms[i], ms[randomU32() % (i + 1)]);
  for (int i = 0; i < n; i++) {
    Board nb = sv.b;
    apply(nb, ms[i]);
    int v = -search(nb, 1, depth - 1, -1000000, -bestV, 1);
    if (sv.level == 0) v += (int)(randomU32() % 60);        // the easy computer makes mistakes
    if (v > bestV || i == 0) { bestV = v; best = ms[i]; }
  }
  return true;
}

// ---------- game flow ----------
static void newGame() {
  memset(&sv.b, 0, sizeof sv.b);
  for (int sq = 0; sq < 64; sq++) if (dark(sq)) { if (sq < 24) sv.b.s[sq] = BM_; else if (sq >= 40) sv.b.s[sq] = WM; }
  sv.turn = 1; sv.over = 0; sv.nh = 0; sv.quiet = 0;
  hasLastAi = false; sel = -1; cx = 1; cy = 6;
}

static int count(int side) { int n = 0; for (auto p : sv.b.s) if (mine(p, side)) n++; return n; }

static void refreshLegal() { nLegal = genMoves(sv.b, sv.turn > 0 ? 1 : -1, legal); }

static void finish(int result) {   // 1 player won, -1 computer won, 0 draw
  sv.over = 1; mode = M_OVER;
  if (result > 0) { sv.wins++; overText = "Ти переміг!"; sfxWin(); }
  else if (result < 0) { sv.losses++; overText = "Переміг комп'ютер."; sfxLose(); }
  else { sv.draws++; overText = "Нічия."; }
  saveData(&sv, sizeof sv);
}

static void afterMove(const Move& m) {
  bool quiet = m.nc == 0 && sv.b.s[m.path[m.n - 1]] != WM && sv.b.s[m.path[m.n - 1]] != BM_;
  sv.quiet = quiet ? sv.quiet + 1 : 0;
  sv.turn = sv.turn > 0 ? 0 : 1;
  refreshLegal();
  if (!nLegal) { finish(sv.turn ? -1 : 1); return; }
  if (sv.quiet >= 30) { finish(0); return; }   // 15 king moves each without captures
}

static void playerMove(const Move& m) {
  if (sv.nh == 24) { memmove(sv.hist, sv.hist + 1, sizeof(Board) * 23); sv.nh--; }
  sv.hist[sv.nh++] = sv.b;
  apply(sv.b, m);
  if (m.nc) sfxHit(); else sfxClick();
  sel = -1;
  afterMove(m);
  if (!sv.over) mode = M_THINK;
  saveData(&sv, sizeof sv);
}

static void undo() {
  if (!sv.nh) return;
  sv.b = sv.hist[--sv.nh];
  sv.turn = 1; sv.over = 0; hasLastAi = false; sel = -1;
  refreshLegal();
  saveData(&sv, sizeof sv);
}

// ---------- drawing ----------
static void piece(int sq, int8_t p, int dy = 0) {
  int x = BX + (sq % 8) * CS + CS / 2, y = BY + (sq / 8) * CS + CS / 2 + dy;
  bool w = p > 0;
  uint16_t c = w ? WHITE_P : BLACK_P, d = w ? WHITE_D : BLACK_D;
  fillCircle(x, y + 2, 11, rgb(0x2a1a10));
  fillCircle(x, y, 11, d);
  fillCircle(x, y - 1, 10, c);
  fillCircle(x, y - 1, 6, d);
  fillCircle(x, y - 1, 5, c);
  if (p == WK || p == BK) {   // crown
    tri(x - 6, y + 2, x - 6, y - 6, x - 2, y - 1, CROWN);
    tri(x - 3, y + 2, x, y - 8, x + 3, y + 2, CROWN);
    tri(x + 6, y + 2, x + 6, y - 6, x + 2, y - 1, CROWN);
    fill(x - 6, y + 1, 13, 3, CROWN);
  }
}

static bool isTarget(int sq) {
  if (sel < 0) return false;
  for (int i = 0; i < nLegal; i++) if (legal[i].path[0] == sel && legal[i].path[legal[i].n - 1] == sq) return true;
  return false;
}
static bool canMoveFrom(int sq) { for (int i = 0; i < nLegal; i++) if (legal[i].path[0] == sq) return true; return false; }

static void draw() {
  fill(0, 0, W, H, pal::BG);
  char s[64];
  text("ШАШКИ", 10, 24, pal::AMBER, F16B);
  static const char* LV[3] = {"легко", "нормально", "важко"};
  snprintf(s, sizeof s, "%s · %u:%u:%u", LV[sv.level], sv.wins, sv.draws, sv.losses);
  text(s, W - 10, 22, pal::SUB, F11, RIGHT);
  snprintf(s, sizeof s, "ти %d  ·  комп'ютер %d", count(1), count(-1));
  text(s, W / 2, 44, pal::TXT, F11B, CENTER);
  fill(BX - 3, BY - 3, 8 * CS + 6, 8 * CS + 6, rgb(0x4a2e1c));
  for (int sq = 0; sq < 64; sq++) {
    int x = BX + (sq % 8) * CS, y = BY + (sq / 8) * CS;
    uint16_t c = dark(sq) ? DARK : LIGHT;
    if (hasLastAi) for (int i = 0; i < lastAi.n; i++) if (lastAi.path[i] == sq) c = mix565(DARK, rgb(0xffcc00), 70);
    if (sq == sel) c = DARK_SEL;
    fill(x, y, CS, CS, c);
    if (isTarget(sq)) fillCircle(x + CS / 2, y + CS / 2, 5, rgb(0x9ad05a));
    else if (sel < 0 && mode == M_PLAY && canMoveFrom(sq) && legal[0].nc) fillCircle(x + CS / 2, y + CS / 2, 13, rgb(0xd04a3a));   // must capture
  }
  for (int sq = 0; sq < 64; sq++) if (sv.b.s[sq]) piece(sq, sv.b.s[sq], sq == sel ? -2 : 0);
  if (mode == M_PLAY || mode == M_OVER) {
    int x = BX + cx * CS, y = BY + cy * CS;
    strokeRect(x, y, CS, CS, pal::AMBER);
    strokeRect(x + 1, y + 1, CS - 2, CS - 2, pal::AMBER);
  }
  const char* hint = mode == M_THINK ? "Комп'ютер думає…" : sel >= 0 ? "OK — сюди · Назад — скасувати" : legal[0].nc && nLegal ? "Бити обов'язково! · Назад — меню" : "OK — вибрати · Назад — меню (нова гра)";
  uiHint(hint);
  if (mode == M_SETUP) {
    std::string o[3] = {"Легко", "Нормально", "Важко"};
    blendRect(0, 0, W, H, pal::SHADE, 170);
    fill(16, 90, 208, 140, pal::PANEL);
    strokeRect(16, 90, 208, 140, pal::AMBER);
    text("НОВА ГРА", W / 2, 110, pal::AMBER, F12B, CENTER);
    text("Ти граєш білими, ходиш першим.", W / 2, 130, pal::TXT, F11, CENTER);
    for (int i = 0; i < 3; i++) {
      int y = 142 + i * 26;
      bool s2 = i == setupSel;
      fill(40, y, 160, 20, s2 ? pal::AMBER : pal::LINE);
      text(o[i], W / 2, y + 15, s2 ? pal::SELTXT : pal::TXT, F12B, CENTER);
    }
  } else if (mode == M_MENU) {
    blendRect(0, 0, W, H, pal::SHADE, 170);
    fill(16, 96, 208, 128, pal::PANEL);
    strokeRect(16, 96, 208, 128, pal::AMBER);
    text("МЕНЮ ГРИ", W / 2, 116, pal::AMBER, F12B, CENTER);
    static const char* O[3] = {"Нова гра", "Хід назад", "Продовжити"};
    for (int i = 0; i < 3; i++) {
      int y = 128 + i * 28;
      fill(40, y, 160, 22, i == menuSel ? pal::AMBER : pal::LINE);
      text(O[i], W / 2, y + 16, i == menuSel ? pal::SELTXT : pal::TXT, F12B, CENTER);
    }
  } else if (mode == M_OVER) {
    std::string o[2] = {"Нова гра", "Хід назад"};
    uiDialog("КІНЕЦЬ ГРИ", overText, o, sv.nh ? 2 : 1, 0);
  }
}

void setup() {
  consoleBegin("checkers");
  inputSetRepeat(REPEAT_MENU, 450, 110);
  memset(&sv, 0, sizeof sv);
  Save* l = new Save;
  if (loadData(l, sizeof *l) && l->magic == MAGIC) sv = *l;
  delete l;
  sv.magic = MAGIC;
  if (sv.level > 2) sv.level = 1;
  bool any = false;
  for (auto p : sv.b.s) if (p) any = true;
  if (any && !sv.over) { sv.turn = 1; refreshLegal(); mode = M_PLAY; }
  else { newGame(); refreshLegal(); mode = M_SETUP; setupSel = sv.level; }
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
      if (confirmExit()) { saveData(&sv, sizeof sv); returnToLauncher(); }
      continue;
    }
    if (mode == M_SETUP) {
      if (k == K_UP) setupSel = (setupSel + 2) % 3;
      if (k == K_DOWN) setupSel = (setupSel + 1) % 3;
      if (k == K_OK) { sv.level = setupSel; newGame(); refreshLegal(); mode = M_PLAY; saveData(&sv, sizeof sv); }
    } else if (mode == M_OVER) {
      if (k == K_LEFT || k == K_RIGHT) {}
      if (k == K_OK) { mode = M_SETUP; setupSel = sv.level; newGame(); refreshLegal(); }
      if (k == K_BACK && sv.nh) { undo(); mode = M_PLAY; }
    } else if (mode == M_PLAY) {
      if (k == K_UP && cy > 0) cy--;
      if (k == K_DOWN && cy < 7) cy++;
      if (k == K_LEFT && cx > 0) cx--;
      if (k == K_RIGHT && cx < 7) cx++;
      int sq = cy * 8 + cx;
      if (k == K_BACK) { if (sel >= 0) sel = -1; else { mode = M_MENU; menuSel = 0; } }   // Back: game menu (new game, undo)
      if (k == K_OK) {
        if (canMoveFrom(sq)) sel = sq;
        else if (sel >= 0 && isTarget(sq)) {
          for (int i = 0; i < nLegal; i++) if (legal[i].path[0] == sel && legal[i].path[legal[i].n - 1] == sq) { playerMove(legal[i]); break; }
        }
      }
    } else if (mode == M_MENU) {
      if (k == K_UP) menuSel = (menuSel + 2) % 3;
      if (k == K_DOWN) menuSel = (menuSel + 1) % 3;
      if (k == K_BACK) mode = M_PLAY;
      if (k == K_OK) {
        mode = M_PLAY;
        if (menuSel == 0) { newGame(); refreshLegal(); mode = M_SETUP; setupSel = sv.level; }
        if (menuSel == 1) undo();
      }
    }
  }
  if (mode == M_THINK) {
    draw(); present();
    Move m;
    if (aiMove(m)) { apply(sv.b, m); if (m.nc) sfxHit(); else tone2(700, 40); lastAi = m; hasLastAi = true; afterMove(m); }
    if (!sv.over) { mode = M_PLAY; sv.turn = 1; refreshLegal(); }
    saveData(&sv, sizeof sv);
    changed = true;
  }
  if (changed) { draw(); present(); }
  delay(10);
}
