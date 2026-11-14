#include "bz_ui.h"
#include <algorithm>
#include "bz_console.h"
#include "bz_fonts.h"

using namespace gfx;

void uiDialog(const std::string& title, const std::string& body, const std::string* opts, int nopt, int sel, bool dim) {
  if (dim) blendRect(0, 0, W, H, pal::SHADE, 184);
  std::vector<std::string> lines = wrap(body, 184, F12);
  if (lines.size() > 14) lines.resize(14);
  int h = 60 + (int)lines.size() * 15, y = std::max(4, (H - h) / 2);
  fill(16, y, 208, h, pal::PANEL);
  strokeRect(16, y, 208, h, pal::AMBER);
  text(title, W / 2, y + 18, pal::AMBER, F12B, CENTER);
  for (size_t i = 0; i < lines.size(); i++) text(lines[i], W / 2, y + 38 + (int)i * 15, pal::TXT, F12, CENTER);
  int bw = nopt > 1 ? 90 : 110, gap = 8, tot = nopt * bw + (nopt - 1) * gap;
  for (int i = 0; i < nopt; i++) {
    int x = (W - tot) / 2 + i * (bw + gap);
    bool s = i == sel;
    fill(x, y + h - 22, bw, 16, s ? pal::AMBER : pal::LINE);
    text(opts[i], x + bw / 2, y + h - 10, s ? pal::SELTXT : pal::TXT, F11B, CENTER);
  }
}

void uiHint(const std::string& s) {
  fill(0, H - 18, W, 18, pal::PANEL);
  text(s, W / 2, H - 5, pal::SUB, F11, CENTER);
}

bool confirmExit() {
  if (!hasLauncher()) return false;
  // The dialog is drawn over the current frame; the game redraws its screen afterwards.
  std::string opts[2] = {"Вийти", "Грати далі"};
  int sel = 1;
  bool first = true;
  inputClear();
  for (;;) {
    uiDialog("МЕНЮ", "Вийти до списку ігор?\nПрогрес збережено.", opts, 2, sel, first);
    first = false;
    present();
    int k;
    while ((k = (consolePoll(), inputGet())) < 0) sleepMs(10);
    if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) sel ^= 1;
    if (k == K_OK) { inputClear(); return sel == 0; }
    if (k == K_BACK) { inputClear(); return false; }
  }
}
