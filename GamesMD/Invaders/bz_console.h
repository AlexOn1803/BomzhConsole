#pragma once
// Console services shared by the launcher and every game.
#include <stddef.h>
#include <stdint.h>
#include "bz_input.h"

// Starts buttons, display and SD card. gameId is the save-file name (≤ 12 latin chars, e.g. "snake").
// Holding BACK while the game boots returns straight to the launcher.
void consoleBegin(const char* gameId);
void consolePoll();                 // read buttons -> input events (call every loop)
void present();                     // push gfx::fb to the display
uint32_t nowMs();
void sleepMs(uint32_t ms);
uint32_t randomU32();

bool sdReady();
// Saves: /saves/<gameId>.sav on the SD card plus a backup copy in internal flash (NVS).
bool saveData(const void* data, size_t len);
bool loadData(void* data, size_t len);   // true if a valid save of exactly len bytes was found

bool hasLauncher();                 // launcher (factory partition) is installed
void returnToLauncher();            // reboot into the launcher (does nothing if there is none)

// Shared "exit to menu?" dialog drawn over the current frame. Returns true if the player confirmed.
bool confirmExit();
