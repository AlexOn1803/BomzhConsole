#include "bz_sound.h"

// effects (shared by the device and the PC simulator)
static const uint16_t T_CLICK[] = {1800, 12, 0, 0};
static const uint16_t T_SELECT[] = {1200, 30, 1800, 40, 0, 0};
static const uint16_t T_COIN[] = {1568, 50, 2093, 90, 0, 0};
static const uint16_t T_HIT[] = {220, 40, 160, 60, 110, 80, 0, 0};
static const uint16_t T_JUMP[] = {600, 25, 900, 25, 1200, 25, 0, 0};
static const uint16_t T_WIN[] = {1047, 90, 1319, 90, 1568, 90, 2093, 220, 0, 0};
static const uint16_t T_LOSE[] = {784, 140, 659, 140, 523, 140, 392, 300, 0, 0};
void sfxClick() { playTune(T_CLICK); }
void sfxSelect() { playTune(T_SELECT); }
void sfxCoin() { playTune(T_COIN); }
void sfxHit() { playTune(T_HIT); }
void sfxJump() { playTune(T_JUMP); }
void sfxWin() { playTune(T_WIN); }
void sfxLose() { playTune(T_LOSE); }

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>
#include "bz_config.h"

static bool enabled = true, ready = false;
static esp_timer_handle_t tmr = nullptr;
static const uint16_t* volatile seq = nullptr;
static uint16_t single[4];

static void noteOff() { ledcWriteTone(PIN_BUZZER, 0); }
static void nextNote(void*) {
  const uint16_t* s = seq;
  if (!s || (s[0] == 0 && s[1] == 0)) { noteOff(); seq = nullptr; return; }
  if (s[0]) ledcWriteTone(PIN_BUZZER, s[0]); else noteOff();
  seq = s + 2;
  esp_timer_start_once(tmr, (uint64_t)s[1] * 1000);
}

void soundInit() {
  Preferences p;
  if (p.begin("launcher", true)) { enabled = p.getBool("sound", true); p.end(); }
  if (!ledcAttach(PIN_BUZZER, 2000, 8)) return;
  noteOff();
  esp_timer_create_args_t a = {};
  a.callback = nextNote;
  a.name = "sound";
  ready = esp_timer_create(&a, &tmr) == ESP_OK;
}
bool soundEnabled() { return enabled; }
void soundSetEnabled(bool on) {
  enabled = on;
  Preferences p;
  if (p.begin("launcher", false)) { p.putBool("sound", on); p.end(); }
  if (!on && ready) { esp_timer_stop(tmr); seq = nullptr; noteOff(); }
}
void playTune(const uint16_t* notes) {
  if (!enabled || !ready) return;
  esp_timer_stop(tmr);
  seq = notes;
  nextNote(nullptr);
}
void tone2(uint16_t hz, uint16_t ms) {
  single[0] = hz; single[1] = ms; single[2] = 0; single[3] = 0;
  playTune(single);
}
#else
// PC simulator: silent
static bool enabled = true;
void soundInit() {}
bool soundEnabled() { return enabled; }
void soundSetEnabled(bool on) { enabled = on; }
void playTune(const uint16_t*) {}
void tone2(uint16_t, uint16_t) {}
#endif
