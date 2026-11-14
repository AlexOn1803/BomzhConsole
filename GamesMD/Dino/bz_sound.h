#pragma once
// Simple beeper on a passive piezo. Sounds play in the background (games don't wait for them).
#include <stdint.h>

void soundInit();                         // called by consoleBegin()
bool soundEnabled();                      // the "Звук" switch from the launcher
void soundSetEnabled(bool on);            // saves the switch for every game
void tone2(uint16_t hz, uint16_t ms);     // one note (replaces whatever is playing)
// a short tune: pairs {frequency Hz (0 = pause), duration ms}, ended by {0, 0}
void playTune(const uint16_t* notes);

// ready-made effects, the same in every game
void sfxClick();    // cursor / menu move
void sfxSelect();   // confirm
void sfxCoin();     // picked something up
void sfxHit();      // damage, explosion
void sfxJump();
void sfxWin();      // level / goal done
void sfxLose();     // game over
