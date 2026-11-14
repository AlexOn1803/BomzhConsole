// Шахи проти комп'ютера для кишенькової консолі.
// ▲▼◀▶ — курсор, OK — вибрати фігуру / куди ходити, Назад — скасувати або хід назад, довге Назад — меню.
// Пішак, що дійшов до кінця, стає ферзем.
#include <Arduino.h>
#include "BomzhCore.h"
#include <algorithm>

using namespace gfx;

#ifdef ARDUINO
// the AI search is recursive and keeps move lists on the stack: give loop() a bigger stack than the default 8 KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

static const int CS = 28, BX = (240 - 8 * CS) / 2, BY = 52;
enum : int8_t { EMPTY = 0, P = 1, N, B, R, Q, K };
static const uint16_t LIGHT = rgb(0xf0d9b5), DARK = rgb(0xb58863), SEL = rgb(0x8aa04a), LASTM = rgb(0xd8c060),
                      CHECK = rgb(0xe04a3a), WFILL = rgb(0xf8f4ea), WLINE = rgb(0x2a2420), BFILL = rgb(0x2a2420), BLINE = rgb(0xa89880);

struct Pos {
  int8_t b[64];          // + white (player, bottom), - black (computer); square 0 = a8
  int8_t side;           // 1 white to move, -1 black
  uint8_t castle;        // 1 W short, 2 W long, 4 B short, 8 B long
  int8_t ep;             // en-passant target square or -1
  uint8_t half;          // moves since capture / pawn move
};
struct Move { uint8_t from, to; int8_t promo, cap; uint8_t flags; };   // flags: 1 ep, 2 castle
static const int MAXMV = 128;

// ---------- move generation ----------
static const int KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int KG[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static inline bool on(int x, int y) { return x >= 0 && y >= 0 && x < 8 && y < 8; }

static bool attacked(const Pos& p, int sq, int by) {   // is sq attacked by side `by`
  int x = sq % 8, y = sq / 8;
  int py = y + (by > 0 ? 1 : -1);                      // white pawns attack upward (towards row 0)
  for (int dx = -1; dx <= 1; dx += 2) if (on(x + dx, py) && p.b[py * 8 + x + dx] == by * P) return true;
  for (auto& d : KN) if (on(x + d[0], y + d[1]) && p.b[(y + d[1]) * 8 + x + d[0]] == by * N) return true;
  for (auto& d : KG) if (on(x + d[0], y + d[1]) && p.b[(y + d[1]) * 8 + x + d[0]] == by * K) return true;
  for (int i = 0; i < 8; i++) {
    int dx = KG[i][0], dy = KG[i][1], cx = x + dx, cy = y + dy;
    bool diag = dx && dy;
    while (on(cx, cy)) {
      int8_t q = p.b[cy * 8 + cx];
      if (q) {
        if (q == by * Q || q == by * (diag ? B : R)) return true;
        break;
      }
      cx += dx; cy += dy;
    }
  }
  return false;
}
static int kingSq(const Pos& p, int side) { for (int i = 0; i < 64; i++) if (p.b[i] == side * K) return i; return -1; }
static bool inCheck(const Pos& p, int side) { int k = kingSq(p, side); return k >= 0 && attacked(p, k, -side); }

static void add(Move* m, int& n, int f, int t, int8_t cap, int8_t promo = 0, uint8_t fl = 0) { if (n < MAXMV) m[n++] = {(uint8_t)f, (uint8_t)t, promo, cap, fl}; }

static int genPseudo(const Pos& p, Move* m, bool capsOnly) {
  int n = 0, s = p.side;
  for (int sq = 0; sq < 64; sq++) {
    int8_t pc = p.b[sq];
    if (pc * s <= 0) continue;
    int t = pc * s, x = sq % 8, y = sq / 8;
    if (t == P) {
      int dy = s > 0 ? -1 : 1, ny = y + dy, last = s > 0 ? 0 : 7, start = s > 0 ? 6 : 1;
      if (on(x, ny) && !p.b[ny * 8 + x] && !capsOnly) {
        add(m, n, sq, ny * 8 + x, 0, ny == last ? Q : 0);
        if (y == start && !p.b[(ny + dy) * 8 + x]) add(m, n, sq, (ny + dy) * 8 + x, 0);
      } else if (on(x, ny) && !p.b[ny * 8 + x] && ny == last) add(m, n, sq, ny * 8 + x, 0, Q);   // promotion counts as tactical
      for (int dx = -1; dx <= 1; dx += 2) {
        if (!on(x + dx, ny)) continue;
        int to = ny * 8 + x + dx;
        if (p.b[to] * s < 0) add(m, n, sq, to, p.b[to], ny == last ? Q : 0);
        else if (to == p.ep) add(m, n, sq, to, (int8_t)(-s * P), 0, 1);
      }
    } else if (t == N || t == K) {
      auto& D = t == N ? KN : KG;
      for (auto& d : D) {
        if (!on(x + d[0], y + d[1])) continue;
        int to = (y + d[1]) * 8 + x + d[0];
        if (p.b[to] * s > 0) continue;
        if (capsOnly && !p.b[to]) continue;
        add(m, n, sq, to, p.b[to]);
      }
      if (t == K && !capsOnly) {
        int row = s > 0 ? 7 : 0, ks = s > 0 ? 1 : 4, qs = s > 0 ? 2 : 8;
        if (sq == row * 8 + 4 && !attacked(p, sq, -s)) {
          if ((p.castle & ks) && !p.b[row * 8 + 5] && !p.b[row * 8 + 6] && p.b[row * 8 + 7] == s * R && !attacked(p, row * 8 + 5, -s))
            add(m, n, sq, row * 8 + 6, 0, 0, 2);
          if ((p.castle & qs) && !p.b[row * 8 + 3] && !p.b[row * 8 + 2] && !p.b[row * 8 + 1] && p.b[row * 8] == s * R && !attacked(p, row * 8 + 3, -s))
            add(m, n, sq, row * 8 + 2, 0, 0, 2);
        }
      }
    } else {
      for (int i = 0; i < 8; i++) {
        int dx = KG[i][0], dy = KG[i][1];
        bool diag = dx && dy;
        if ((t == B && !diag) || (t == R && diag)) continue;
        int cx = x + dx, cy = y + dy;
        while (on(cx, cy)) {
          int to = cy * 8 + cx;
          if (p.b[to] * s > 0) break;
          if (p.b[to]) { add(m, n, sq, to, p.b[to]); break; }
          if (!capsOnly) add(m, n, sq, to, 0);
          cx += dx; cy += dy;
        }
      }
    }
  }
  return n;
}

static void make(Pos& p, const Move& mv) {
  int s = p.side;
  int8_t pc = p.b[mv.from];
  p.b[mv.to] = mv.promo ? (int8_t)(s * mv.promo) : pc;
  p.b[mv.from] = 0;
  if (mv.flags & 1) p.b[mv.to + (s > 0 ? 8 : -8)] = 0;
  if (mv.flags & 2) {
    int row = mv.to / 8;
    if (mv.to % 8 == 6) { p.b[row * 8 + 5] = p.b[row * 8 + 7]; p.b[row * 8 + 7] = 0; }
    else { p.b[row * 8 + 3] = p.b[row * 8]; p.b[row * 8] = 0; }
  }
  p.ep = -1;
  if (pc * s == P && abs(mv.to - mv.from) == 16) p.ep = (mv.to + mv.from) / 2;
  if (pc * s == K) p.castle &= s > 0 ? ~3 : ~12;
  auto rookGone = [&](int sq) { if (sq == 63) p.castle &= ~1; if (sq == 56) p.castle &= ~2; if (sq == 7) p.castle &= ~4; if (sq == 0) p.castle &= ~8; };
  rookGone(mv.from); rookGone(mv.to);
  p.half = (pc * s == P || mv.cap) ? 0 : p.half + 1;
  p.side = -s;
}

static int genLegal(const Pos& p, Move* out, bool capsOnly = false) {
  static Move m[MAXMV];   // not recursive, so one shared scratch list is enough (keeps the stack small)
  int n = genPseudo(p, m, capsOnly), k = 0;
  for (int i = 0; i < n; i++) {
    Pos q = p;
    make(q, m[i]);
    if (!inCheck(q, p.side)) out[k++] = m[i];
  }
  return k;
}

// ---------- evaluation ----------
static const int VAL[7] = {0, 100, 320, 330, 500, 900, 0};
static const int8_t PST_P[64] = {0, 0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10, 5, 5, 10, 25, 25, 10, 5, 5,
                                 0, 0, 0, 20, 20, 0, 0, 0, 5, -5, -10, 0, 0, -10, -5, 5, 5, 10, 10, -20, -20, 10, 10, 5, 0, 0, 0, 0, 0, 0, 0, 0};
static const int8_t PST_N[64] = {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0, 0, 0, 0, -20, -40, -30, 0, 10, 15, 15, 10, 0, -30, -30, 5, 15, 20, 20, 15, 5, -30,
                                 -30, 0, 15, 20, 20, 15, 0, -30, -30, 5, 10, 15, 15, 10, 5, -30, -40, -20, 0, 5, 5, 0, -20, -40, -50, -40, -30, -30, -30, -30, -40, -50};
static const int8_t PST_B[64] = {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 10, 10, 5, 0, -10, -10, 5, 5, 10, 10, 5, 5, -10,
                                 -10, 0, 10, 10, 10, 10, 0, -10, -10, 10, 10, 10, 10, 10, 10, -10, -10, 5, 0, 0, 0, 0, 5, -10, -20, -10, -10, -10, -10, -10, -10, -20};
static const int8_t PST_R[64] = {0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 10, 10, 10, 10, 10, 5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5,
                                 -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 5, 5, 0, 0, 0};
static const int8_t PST_K[64] = {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                                 -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10, 20, 20, 0, 0, 0, 0, 20, 20, 20, 30, 10, 0, 0, 10, 30, 20};
static const int8_t PST_KE[64] = {-50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0, 0, -10, -20, -30, -30, -10, 20, 30, 30, 20, -10, -30, -30, -10, 30, 40, 40, 30, -10, -30,
                                  -30, -10, 30, 40, 40, 30, -10, -30, -30, -10, 20, 30, 30, 20, -10, -30, -30, -30, 0, 0, 0, 0, -30, -30, -50, -30, -30, -30, -30, -30, -30, -50};

static int evaluate(const Pos& p) {   // from the side to move's point of view
  int s = 0, mat = 0;
  for (int i = 0; i < 64; i++) { int t = abs(p.b[i]); if (t > 1 && t < 6) mat += VAL[t]; }
  bool endgame = mat <= 2600;
  for (int sq = 0; sq < 64; sq++) {
    int8_t pc = p.b[sq];
    if (!pc) continue;
    int t = abs(pc), idx = pc > 0 ? sq : (7 - sq / 8) * 8 + sq % 8;   // tables are written from white's side
    int v = VAL[t];
    switch (t) {
      case P: v += PST_P[idx]; break;
      case N: v += PST_N[idx]; break;
      case B: v += PST_B[idx]; break;
      case R: v += PST_R[idx]; break;
      case Q: v += PST_B[idx] / 2; break;
      case K: v += endgame ? PST_KE[idx] : PST_K[idx]; break;
    }
    s += pc > 0 ? v : -v;
  }
  return s * p.side;
}

// ---------- search ----------
static uint32_t nodes, deadline;
static bool stop;
// one move list per search depth in a static pool, not on the stack (deep searches would overflow it)
static const int MAXPLY = 40;
static Move pool[MAXPLY][MAXMV];
static int order(const Move& m) { return m.cap ? 1000 + VAL[abs(m.cap)] * 10 - VAL[0] : 0 + (m.promo ? 900 : 0); }
static void sortMoves(Move* m, int n, const Move* first) {
  std::sort(m, m + n, [&](const Move& a, const Move& b) {
    bool fa = first && a.from == first->from && a.to == first->to, fb = first && b.from == first->from && b.to == first->to;
    if (fa != fb) return fa;
    return order(a) > order(b);
  });
}

static int quiesce(const Pos& p, int alpha, int beta, int qd, int ply) {
  if ((++nodes & 1023) == 0) { delay(1); if (nowMs() > deadline) stop = true; }
  int stand = evaluate(p);
  if (stand >= beta || qd > 6 || ply >= MAXPLY) return stand;
  if (stand > alpha) alpha = stand;
  Move* m = pool[ply];
  int n = genLegal(p, m, true);
  sortMoves(m, n, nullptr);
  for (int i = 0; i < n && !stop; i++) {
    Pos q = p; make(q, m[i]);
    int v = -quiesce(q, -beta, -alpha, qd + 1, ply + 1);
    if (v >= beta) return v;
    if (v > alpha) alpha = v;
  }
  return alpha;
}

static int search(const Pos& p, int depth, int alpha, int beta, int ply) {
  if ((++nodes & 1023) == 0) { delay(1); if (nowMs() > deadline) stop = true; }
  if (stop) return 0;
  if (p.half >= 100) return 0;
  bool chk = inCheck(p, p.side);
  if (chk) depth++;                                   // check extension
  if (depth <= 0 || ply >= MAXPLY - 8) return quiesce(p, alpha, beta, 0, ply);
  Move* m = pool[ply];
  int n = genLegal(p, m);
  if (!n) return chk ? -30000 + ply : 0;              // mate or stalemate
  sortMoves(m, n, nullptr);
  for (int i = 0; i < n; i++) {
    Pos q = p; make(q, m[i]);
    int v = -search(q, depth - 1, -beta, -alpha, ply + 1);
    if (stop) return 0;
    if (v >= beta) return v;
    if (v > alpha) alpha = v;
  }
  return alpha;
}

static bool think(const Pos& p, int maxDepth, uint32_t ms, Move& best) {
  Move* m = pool[0];
  int n = genLegal(p, m);
  if (!n) return false;
  for (int i = n - 1; i > 0; i--) std::swap(m[i], m[randomU32() % (i + 1)]);   // variety between games
  best = m[0];
  nodes = 0; stop = false; deadline = nowMs() + ms;
  for (int d = 1; d <= maxDepth; d++) {
    sortMoves(m, n, &best);
    Move cur = best;
    int alpha = -100000;
    for (int i = 0; i < n; i++) {
      Pos q = p; make(q, m[i]);
      int v = -search(q, d - 1, -100000, -alpha, 1);
      if (stop) break;
      if (v > alpha) { alpha = v; cur = m[i]; }
    }
    if (stop && d > 1) break;
    best = cur;
    if (alpha > 29000) break;   // found a mate
  }
  return true;
}

// ---------- game state & UI ----------
struct Save { uint32_t magic; Pos pos; uint8_t level, over, nh; Pos hist[20]; uint16_t wins, losses, draws; Move last; uint8_t hasLast; };
static Save sv;
static const uint32_t MAGIC = 0x43485331;
static Move legal[MAXMV];
static int nLegal;
static int cx = 4, cy = 6, sel = -1, setupSel = 1, menuSel = 0;
enum Mode { M_SETUP, M_PLAY, M_THINK, M_OVER, M_MENU };
static Mode mode = M_SETUP;
static char overText[64];

static void startPos() {
  static const int8_t back[8] = {R, N, B, Q, K, B, N, R};
  memset(&sv.pos, 0, sizeof sv.pos);
  for (int x = 0; x < 8; x++) { sv.pos.b[x] = -back[x]; sv.pos.b[8 + x] = -P; sv.pos.b[48 + x] = P; sv.pos.b[56 + x] = back[x]; }
  sv.pos.side = 1; sv.pos.castle = 15; sv.pos.ep = -1; sv.pos.half = 0;
  sv.over = 0; sv.nh = 0; sv.hasLast = 0; sel = -1; cx = 4; cy = 6;
}

static bool insufficient() {
  int minor = 0;
  for (auto pc : sv.pos.b) { int t = abs(pc); if (t == P || t == R || t == Q) return false; if (t == N || t == B) minor++; }
  return minor <= 1;
}

static void refresh() {
  nLegal = genLegal(sv.pos, legal);
  if (!nLegal) {
    sv.over = 1; mode = M_OVER;
    if (inCheck(sv.pos, sv.pos.side)) {
      if (sv.pos.side > 0) { sv.losses++; strcpy(overText, "Мат. Переміг комп'ютер."); }
      else { sv.wins++; strcpy(overText, "Мат! Ти переміг!"); }
    } else { sv.draws++; strcpy(overText, "Пат — нічия."); }
  } else if (sv.pos.half >= 100 || insufficient()) {
    sv.over = 1; mode = M_OVER; sv.draws++;
    strcpy(overText, insufficient() ? "Нічия: нема чим ставити мат." : "Нічия: 50 ходів без взяття.");
  }
}

static void playerMove(const Move& m) {
  if (sv.nh == 20) { memmove(sv.hist, sv.hist + 1, sizeof(Pos) * 19); sv.nh--; }
  sv.hist[sv.nh++] = sv.pos;
  make(sv.pos, m);
  sv.last = m; sv.hasLast = 1; sel = -1;
  refresh();
  if (!sv.over) mode = M_THINK;
  saveData(&sv, sizeof sv);
}

static void undo() {
  if (!sv.nh) return;
  sv.pos = sv.hist[--sv.nh];
  sv.over = 0; sv.hasLast = 0; sel = -1; mode = M_PLAY;
  refresh();
  saveData(&sv, sizeof sv);
}

#define BMP(n, ...) static const char* const n[] = {__VA_ARGS__}
BMP(S_P, "............", "............", ".....##.....", "....####....", "....####....", ".....##.....", "....####....", ".....##.....", "....####....", "...######...", "..########..", "..########..");
BMP(S_N, "............", "....###.....", "...######...", "..########..", ".####.####..", ".###..####..", "......####..", ".....#####..", "....######..", "...#######..", "..#########.", "..#########.");
BMP(S_B, ".....##.....", "....####....", "...##.###...", "...#.####...", "...######...", "....####....", ".....##.....", "....####....", "...######...", "..########..", "..########..", "............");
BMP(S_R, "............", ".##.##.##.#.", ".#########..", "..#######...", "..#######...", "..#######...", "..#######...", "..#######...", ".#########..", "###########.", "###########.", "............");
BMP(S_Q, "#....##....#", "#...####...#", "##..####..##", "############", ".##########.", "..########..", "...######...", "...######...", "..########..", ".##########.", ".##########.", "............");
BMP(S_K, ".....##.....", "....####....", ".....##.....", "..##.##.##..", ".##########.", ".##########.", "..########..", "...######...", "...######...", "..########..", ".##########.", "............");
static const char* const* const SPR[7] = {nullptr, S_P, S_N, S_B, S_R, S_Q, S_K};

static void drawPiece(int sq, int8_t pc, int dy = 0) {
  int x = BX + (sq % 8) * CS + 2, y = BY + (sq / 8) * CS + 3 + dy;
  const char* const* s = SPR[abs(pc)];
  uint16_t f = pc > 0 ? WFILL : BFILL, l = pc > 0 ? WLINE : BLINE;
  for (int o = 0; o < 4; o++) bitmap(s, 12, x + (o == 0) - (o == 1), y + (o == 2) - (o == 3), 2, l);
  bitmap(s, 12, x, y, 2, f);
}

static bool isTarget(int sq) { if (sel < 0) return false; for (int i = 0; i < nLegal; i++) if (legal[i].from == sel && legal[i].to == sq) return true; return false; }
static bool canMove(int sq) { for (int i = 0; i < nLegal; i++) if (legal[i].from == sq) return true; return false; }

static int material(int side) { int v = 0; for (auto pc : sv.pos.b) if (pc * side > 0 && abs(pc) < 6) v += VAL[abs(pc)] / 100 * (abs(pc) == B ? 1 : 1); return v; }

static void draw() {
  fill(0, 0, W, H, pal::BG);
  char s[64];
  text("ШАХИ", 10, 24, pal::AMBER, F16B);
  static const char* LV[3] = {"легко", "нормально", "важко"};
  snprintf(s, sizeof s, "%s · %u:%u:%u", LV[sv.level], sv.wins, sv.draws, sv.losses);
  text(s, W - 10, 22, pal::SUB, F11, RIGHT);
  int d = material(1) - material(-1);
  if (d) snprintf(s, sizeof s, "%s на %d", d > 0 ? "ти попереду" : "комп'ютер попереду", abs(d)); else snprintf(s, sizeof s, "сили рівні");
  if (mode != M_OVER && inCheck(sv.pos, sv.pos.side)) snprintf(s, sizeof s, "ШАХ!");
  text(s, W / 2, 44, inCheck(sv.pos, sv.pos.side) ? pal::RED : pal::TXT, F11B, CENTER);
  int kc = inCheck(sv.pos, sv.pos.side) ? kingSq(sv.pos, sv.pos.side) : -1;
  for (int sq = 0; sq < 64; sq++) {
    int x = BX + (sq % 8) * CS, y = BY + (sq / 8) * CS;
    bool light = ((sq / 8) + (sq % 8)) % 2 == 0;
    uint16_t c = light ? LIGHT : DARK;
    if (sv.hasLast && (sq == sv.last.from || sq == sv.last.to)) c = mix565(c, LASTM, 150);
    if (sq == sel) c = SEL;
    if (sq == kc) c = CHECK;
    fill(x, y, CS, CS, c);
    if (isTarget(sq)) {
      if (sv.pos.b[sq]) { strokeRect(x + 1, y + 1, CS - 2, CS - 2, rgb(0x5a7a2a)); strokeRect(x + 2, y + 2, CS - 4, CS - 4, rgb(0x5a7a2a)); }
      else fillCircle(x + CS / 2, y + CS / 2, 4, rgb(0x5a7a2a));
    }
  }
  for (int sq = 0; sq < 64; sq++) if (sv.pos.b[sq]) drawPiece(sq, sv.pos.b[sq], sq == sel ? -1 : 0);
  // coordinates
  for (int i = 0; i < 8; i++) {
    char c[2] = {(char)('a' + i), 0};
    text(c, BX + i * CS + CS - 5, BY + 8 * CS - 2, ((7 + i) % 2) ? DARK : LIGHT, F9B, CENTER);
  }
  if (mode == M_PLAY) {
    int x = BX + cx * CS, y = BY + cy * CS;
    strokeRect(x, y, CS, CS, pal::AMBER);
    strokeRect(x + 1, y + 1, CS - 2, CS - 2, pal::AMBER);
  }
  uiHint(mode == M_THINK ? "Комп'ютер думає…" : sel >= 0 ? "OK — сюди · Назад — скасувати" : "OK — вибрати · Назад — меню (нова гра)");
  if (mode == M_SETUP) {
    blendRect(0, 0, W, H, pal::SHADE, 170);
    fill(16, 90, 208, 140, pal::PANEL);
    strokeRect(16, 90, 208, 140, pal::AMBER);
    text("НОВА ГРА", W / 2, 110, pal::AMBER, F12B, CENTER);
    text("Ти граєш білими, ходиш першим.", W / 2, 130, pal::TXT, F11, CENTER);
    static const char* O[3] = {"Легко", "Нормально", "Важко"};
    for (int i = 0; i < 3; i++) {
      int y = 142 + i * 26;
      fill(40, y, 160, 20, i == setupSel ? pal::AMBER : pal::LINE);
      text(O[i], W / 2, y + 15, i == setupSel ? pal::SELTXT : pal::TXT, F12B, CENTER);
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
  consoleBegin("chess");
  inputSetRepeat(REPEAT_MENU, 450, 110);
  memset(&sv, 0, sizeof sv);
  Save* l = new Save;
  if (loadData(l, sizeof *l) && l->magic == MAGIC) sv = *l;
  delete l;
  sv.magic = MAGIC;
  if (sv.level > 2) sv.level = 1;
  if (kingSq(sv.pos, 1) >= 0 && !sv.over) { sv.pos.side = 1; mode = M_PLAY; refresh(); }
  else { startPos(); mode = M_SETUP; setupSel = sv.level; refresh(); }
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
      if (k == K_OK) { sv.level = setupSel; startPos(); refresh(); mode = M_PLAY; saveData(&sv, sizeof sv); }
    } else if (mode == M_OVER) {
      if (k == K_OK) { startPos(); refresh(); mode = M_SETUP; setupSel = sv.level; }
      if (k == K_BACK && sv.nh) undo();
    } else if (mode == M_PLAY) {
      if (k == K_UP && cy > 0) cy--;
      if (k == K_DOWN && cy < 7) cy++;
      if (k == K_LEFT && cx > 0) cx--;
      if (k == K_RIGHT && cx < 7) cx++;
      int sq = cy * 8 + cx;
      if (k == K_BACK) { if (sel >= 0) sel = -1; else { mode = M_MENU; menuSel = 0; } }   // Back: game menu (new game, undo)
      if (k == K_OK) {
        if (sv.pos.b[sq] > 0 && canMove(sq)) sel = sq;
        else if (isTarget(sq)) { for (int i = 0; i < nLegal; i++) if (legal[i].from == sel && legal[i].to == sq) { playerMove(legal[i]); break; } }
      }
    } else if (mode == M_MENU) {
      if (k == K_UP) menuSel = (menuSel + 2) % 3;
      if (k == K_DOWN) menuSel = (menuSel + 1) % 3;
      if (k == K_BACK) mode = M_PLAY;
      if (k == K_OK) {
        mode = M_PLAY;
        if (menuSel == 0) { startPos(); refresh(); mode = M_SETUP; setupSel = sv.level; }
        if (menuSel == 1) undo();
      }
    }
  }
  if (mode == M_THINK) {
    draw(); present();
    static const int DEPTH[3] = {1, 3, 5};
    static const uint32_t TIME[3] = {600, 2500, 6000};
    Move m;
    if (think(sv.pos, DEPTH[sv.level], TIME[sv.level], m)) {
      make(sv.pos, m); sv.last = m; sv.hasLast = 1;
    }
    mode = M_PLAY;
    refresh();
    saveData(&sv, sizeof sv);
    changed = true;
  }
  if (changed) { draw(); present(); }
  delay(10);
}
