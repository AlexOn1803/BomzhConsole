// Бомж-сим для кишенькової консолі: кнопки, збереження на SD, вихід у меню.
#include <Arduino.h>
#include "BomzhCore.h"
#include "game.h"
#include "ui.h"

uint32_t platformRandom() { return randomU32(); }

struct SaveBlob { State s; UIState u; };
static SaveBlob blob;
static bool dirty = false;
static uint32_t lastChange = 0;

static void saveNow() {
  blob.s = S;
  blob.u = ui;
  saveData(&blob, sizeof blob);
  dirty = false;
}

void setup() {
  consoleBegin("bomzh");
  inputSetRepeat(REPEAT_MENU);
  newGame(0);
  if (loadData(&blob, sizeof blob) && blob.s.magic == SAVE_MAGIC) {
    S = blob.s;
    ui = blob.u;
    loadedState();
    if (!sdReady()) msg = "SD-карти немає: гра збережеться у пам'ять плати.";
  } else if (!sdReady()) {
    msg = "SD-карти немає: гра збережеться у пам'ять плати.";
  }
  draw();
  present();
}

void loop() {
  consolePoll();
  int k = inputGet();
  if (k == K_BACK_LONG) {
    saveNow();
    if (confirmExit()) returnToLauncher();
    draw();
    present();
  } else if (k >= 0) {
    if (k == K_OK) sfxSelect(); else sfxClick();
    key(k);
    draw();
    present();
    dirty = true;
    lastChange = nowMs();
  }
  if (dirty && nowMs() - lastChange > 2000) saveNow();
  delay(5);
}
