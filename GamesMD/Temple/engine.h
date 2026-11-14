#pragma once
// Rules of «Скарби храму», shared by the game and the level checker. Everything is turn based:
// one tick = one player step (or a wait), then gravity, rolling and snakes move once.
#include <stdint.h>
#include <string.h>

namespace tpl {
static const int MAXW = 24, MAXH = 28;
enum : uint8_t { EMPTY, DIRT, WALL, ROCK, GEM, RED, KEY, DOOR, SPIKE, EXIT, CHECK, SNAKE_L, SNAKE_R };
static const uint8_t FALL_BIT = 0x80;   // flag in the cell byte: this rock/gem moved down last tick

struct State {
  uint8_t w, h, px, py, keys, dead, won, gems, need, red;
  uint8_t c[MAXH][MAXW];
};

static inline uint8_t T(const State& s, int x, int y) { return (x < 0 || y < 0 || x >= s.w || y >= s.h) ? WALL : (s.c[y][x] & 0x7f); }
static inline bool round_(uint8_t t) { return t == ROCK || t == GEM || t == RED; }
static inline bool heavy(uint8_t t) { return t == ROCK || t == GEM || t == RED; }

// loads a level written as rows of characters
static void load(State& s, const char* const* rows, int h, int need) {
  memset(&s, 0, sizeof s);
  s.h = h; s.w = (uint8_t)strlen(rows[0]); s.need = need;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < s.w; x++) {
      char ch = rows[y][x];
      uint8_t t = EMPTY;
      switch (ch) {
        case '#': t = WALL; break;   case '.': t = DIRT; break;   case 'O': t = ROCK; break;
        case '*': t = GEM; break;    case 'R': t = RED; break;    case 'K': t = KEY; break;
        case 'D': t = DOOR; break;   case '^': t = SPIKE; break;  case 'E': t = EXIT; break;
        case 'C': t = CHECK; break;  case 'S': t = SNAKE_R; break; case 'P': s.px = x; s.py = y; break;
      }
      s.c[y][x] = t;
    }
}

static inline bool exitOpen(const State& s) { return s.gems >= s.need; }

// dir: 0 up, 1 down, 2 left, 3 right, 4 wait. Returns true if the player actually moved.
static bool step(State& s, int dir, bool* checkpoint = nullptr) {
  if (s.dead || s.won) return false;
  static const int DX[5] = {0, 0, -1, 1, 0}, DY[5] = {-1, 1, 0, 0, 0};
  bool moved = false;
  if (dir < 4) {
    int nx = s.px + DX[dir], ny = s.py + DY[dir];
    uint8_t t = T(s, nx, ny);
    bool go = false;
    switch (t) {
      case EMPTY: case DIRT: case CHECK: go = true; break;
      case GEM: s.gems++; go = true; break;
      case RED: s.red++; go = true; break;
      case KEY: s.keys++; go = true; break;
      case DOOR: if (s.keys) { s.keys--; go = true; } break;
      case SPIKE: s.dead = 1; go = true; break;
      case EXIT: if (exitOpen(s)) { s.won = 1; go = true; } break;
      case SNAKE_L: case SNAKE_R: s.dead = 1; go = true; break;
      case ROCK:   // push sideways if the next cell is free and the rock is resting
        if (DY[dir] == 0 && T(s, nx + DX[dir], ny) == EMPTY && !(s.c[ny][nx] & FALL_BIT)) {
          s.c[ny][nx + DX[dir]] = ROCK; s.c[ny][nx] = EMPTY; go = true;
        }
        break;
    }
    if (go) {
      if (checkpoint && t == CHECK) *checkpoint = true;
      s.c[ny][nx] = t == CHECK ? CHECK : EMPTY;
      s.px = nx; s.py = ny; moved = true;
    }
  }
  if (s.won) return moved;
  // gravity and rolling, bottom row first so stacks fall together
  static bool done[MAXH][MAXW];
  memset(done, 0, sizeof done);
  for (int y = s.h - 2; y >= 0; y--)
    for (int x = 0; x < s.w; x++) {
      uint8_t raw = s.c[y][x], t = raw & 0x7f;
      if (!heavy(t) || done[y][x]) continue;
      bool wasFalling = raw & FALL_BIT;
      uint8_t below = T(s, x, y + 1);
      bool playerBelow = s.px == x && s.py == y + 1;
      if (playerBelow) {
        if (wasFalling) s.dead = 1;              // hit by a falling rock
        s.c[y][x] = t;
        continue;
      }
      if (below == EMPTY) { s.c[y + 1][x] = t | FALL_BIT; s.c[y][x] = EMPTY; done[y + 1][x] = true; continue; }
      if ((below == SNAKE_L || below == SNAKE_R) && wasFalling) { s.c[y + 1][x] = t | FALL_BIT; s.c[y][x] = EMPTY; done[y + 1][x] = true; continue; }   // squashes a snake
      if (round_(below) && !(s.c[y + 1][x] & FALL_BIT)) {   // roll off another rock/gem
        int dirs[2] = {-1, 1};
        bool rolled = false;
        for (int d : dirs) {
          int sx = x + d;
          if (T(s, sx, y) == EMPTY && T(s, sx, y + 1) == EMPTY && !(s.px == sx && (s.py == y || s.py == y + 1))) {
            s.c[y][sx] = t | FALL_BIT; s.c[y][x] = EMPTY; done[y][sx] = true; rolled = true; break;
          }
        }
        if (rolled) continue;
      }
      s.c[y][x] = t;   // resting
    }
  // snakes slide left/right along the floor; touching the player is deadly
  for (int y = 0; y < s.h; y++)
    for (int x = 0; x < s.w; x++) {
      uint8_t t = s.c[y][x];
      if ((t != SNAKE_L && t != SNAKE_R) || done[y][x]) continue;
      int d = t == SNAKE_L ? -1 : 1, nx = x + d;
      if (s.px == nx && s.py == y) { s.dead = 1; continue; }
      if (T(s, nx, y) == EMPTY && T(s, nx, y + 1) != EMPTY) { s.c[y][nx] = t; s.c[y][x] = EMPTY; done[y][nx] = true; }
      else s.c[y][x] = t == SNAKE_L ? SNAKE_R : SNAKE_L;
    }
  if (T(s, s.px - 1, s.py) == SNAKE_R || T(s, s.px + 1, s.py) == SNAKE_L) s.dead = 1;
  return moved;
}
}  // namespace tpl
