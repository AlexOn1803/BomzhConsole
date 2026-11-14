#include "bz_input.h"

static const uint32_t DEBOUNCE = 20, LONG_BACK = 900;
static uint8_t repMask = REPEAT_MENU;
static uint16_t repDelay = 450, repEvery = 110;

struct B { volatile bool down = false; bool longSent = false; uint32_t changed = 0, since = 0, next = 0; bool raw = false; };
static B b[NBTN];
// single producer (button timer) / single consumer (game loop) ring buffer
static volatile int q[16];
static volatile uint8_t qh = 0, qt = 0;

static void push(int k) {
  uint8_t n = (qt + 1) & 15;
  if (n != qh) { q[qt] = k; qt = n; }
}

void inputSetRepeat(uint8_t mask, uint16_t d, uint16_t e) { repMask = mask; repDelay = d; repEvery = e; }
void inputClear() { qh = qt; }   // consumer side only: drop what is queued
bool inputHeld(int k) { return k >= 0 && k < NBTN && b[k].down; }

int inputGet() {
  if (qh == qt) return -1;
  int k = q[qh];
  qh = (qh + 1) & 15;
  return k;
}

void inputFeed(uint8_t raw, uint32_t now) {
  for (int i = 0; i < NBTN; i++) {
    bool r = raw & (1u << i);
    B& s = b[i];
    if (r != s.raw) { s.raw = r; s.changed = now; }
    if (now - s.changed < DEBOUNCE) continue;
    if (r && !s.down) {                       // press
      s.down = true; s.since = now; s.longSent = false; s.next = now + repDelay;
      if (i != K_BACK) push(i);
    } else if (!r && s.down) {                // release
      s.down = false;
      if (i == K_BACK && !s.longSent) push(K_BACK);
    } else if (r && s.down) {                 // held
      if (i == K_BACK) {
        if (!s.longSent && now - s.since >= LONG_BACK) { s.longSent = true; push(K_BACK_LONG); }
      } else if ((repMask & (1u << i)) && (int32_t)(now - s.next) >= 0) {
        s.next = now + repEvery;
        push(i);
      }
    }
  }
}
