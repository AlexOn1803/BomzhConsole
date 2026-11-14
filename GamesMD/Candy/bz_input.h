#pragma once
// Button events: debounce, auto-repeat for chosen keys, short/long press for BACK.
// Platform-independent: the device (or the PC simulator) feeds raw button states.
#include <stdint.h>

enum Key : int { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_BACK, K_BACK_LONG, K_COUNT };
constexpr int NBTN = 6;  // physical buttons: UP DOWN LEFT RIGHT OK BACK

#define KEYBIT(k) (1u << (k))
constexpr uint8_t REPEAT_MENU = KEYBIT(K_UP) | KEYBIT(K_DOWN) | KEYBIT(K_LEFT) | KEYBIT(K_RIGHT);

void inputSetRepeat(uint8_t mask, uint16_t delayMs = 450, uint16_t everyMs = 110);
void inputFeed(uint8_t raw, uint32_t nowMs);  // raw: bit i = physical button i held
int  inputGet();                              // next event or -1
bool inputHeld(int k);                        // debounced "is held now"
void inputClear();                            // drop queued events (e.g. after a dialog)
