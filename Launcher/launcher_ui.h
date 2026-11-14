#pragma once
// Launcher screens (pure drawing into gfx::fb, so the PC simulator can render them too).
#include <stdint.h>
#include <string>
#include <vector>

struct GameEntry {
  std::string file;      // "snake.bin"
  std::string title;     // "Змійка"
  std::string desc;      // one short line
  uint32_t size = 0;
  bool installed = false;  // this exact file is already in the game slot
  bool onSd = true;        // false: only the installed copy exists (card removed / file deleted)
  // icon from the .txt next to the game: up to 16×16 points, one colour (lives in a few bytes of RAM)
  uint16_t iconRows[16] = {0};
  uint8_t iconW = 0, iconH = 0;
  uint16_t iconColor = 0xFD20;
  bool hasColor = false;
};

// Reads "<name>.txt": line 1 title, line 2 description, then optionally
//   колір=4cd964                               (or color=… / #4cd964)
//   BM(IC_X, "..##..", ".####.", ...);          rows in quotes, '#' = point, '.' = empty
// or the same rows one per line without quotes.
void parseGameTxt(const std::string& txt, GameEntry& g);

enum ItemKind { IT_GAME, IT_WIFI, IT_AUTORUN, IT_UPDATE, IT_SOUND };
struct MenuItem { ItemKind kind; int game; };

struct LauncherState {
  std::vector<GameEntry> games;
  std::vector<MenuItem> items;
  int sel = 0, top = 0;
  bool sd = false, autorun = false, fwPending = false, sound = true;   // fwPending: /firmware/launcher.bin is waiting
  uint64_t sdTotal = 0, sdUsed = 0;
  std::string msg;
};

void buildItems(LauncherState& st);
void drawMenu(LauncherState& st);
void drawInstall(const std::string& title, uint32_t done, uint32_t total, const char* head = "Встановлюю");
void drawWifiHeader(int clients);
void drawWifiStatus(const std::string& status);
void drawWifi(const char* ssid, const char* pass, const char* url, const std::string& status, int clients);
std::string titleFor(const std::string& file, std::string* desc);
