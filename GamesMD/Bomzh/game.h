#pragma once
// Game rules of "Бомж-сим": state, actions, days, events. No drawing here.
#include <stdint.h>
#include <string>
#include <vector>

constexpr int NTABS = 13;
constexpr uint32_t SAVE_MAGIC = 0xB0E50003;  // change when State layout changes

struct Loan { int amt, due; uint8_t back; };

// Plain data only, so it can be copied for the preview and saved to flash as bytes.
struct State {
  uint32_t magic;
  int day, age;
  int hp, food, mood, rep;
  int64_t uah, usd, bot, bank;
  int8_t house, cloth, edu, trans, bag;
  uint8_t docs, phone, guitar, lic;
  int skB, jx[8];
  uint8_t biz[6];
  int fr, lastFr;
  Loan loans[8];
  int nloans;
  int pol, won;
  int spray, dogH, copUntil, guardUntil, lawUntil;
  uint8_t dog, card, selfdef, press;
  int wh, workers, contrUntil, comp;
  int crit, best;
  float rate, brate;
  int wxT, wxC;
  uint8_t dead;
  char deadWhy[140];
  // transient flags used while an action runs
  uint8_t pv, lost, half;
  int rh;
};

struct Cost {
  int64_t uah = -1, usd = -1;
  bool any() const { return uah >= 0 || usd >= 0; }
};

enum Kind : uint8_t { K_ACT, K_INFO, K_RESET, K_LOCKED };

struct Item {
  std::string label, price, desc, need, ownedPrice, locked;
  Kind kind = K_ACT;
  bool owned = false, cur = false;
  Cost cost;
  int days = 1, id = 0, arg = 0;
};

struct Ask {
  bool on = false;
  std::string text, opt[2];
  int kind = 0, sel = 0;
  int64_t amt = 0;
};

struct UIState { int tab = 1, cur[NTABS] = {0}, toff = 0; bool confirm = false; };

#include "bz_input.h"   // K_UP K_DOWN K_LEFT K_RIGHT K_OK K_BACK K_BACK_LONG

extern State S;
extern UIState ui;
extern Ask ask;
extern std::string msg, popup;

uint32_t platformRandom();  // provided by main.cpp (device) or the PC simulator

void newGame(int best);
void loadedState();  // call after S was restored from flash
void key(int k);
std::vector<Item> acts(int tab);
bool canPay(const Cost& c);
// Change of hp, food, mood, uah, usd, bot, rep for an item: d[k][0]=min, d[k][1]=max.
bool preview(const Item& it, int64_t d[7][2]);

// helpers the UI needs
extern const char* TABS[NTABS];
std::string fmt(int64_t n);
std::string dd(int day);           // "05.09"
int weekday(int day);              // 0 = Sunday
extern const char* WD[7];
extern const char* WXN[8];
int64_t cap();
