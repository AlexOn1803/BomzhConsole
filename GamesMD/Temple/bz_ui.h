#pragma once
// Shared look: palette and modal dialog, so every game feels like one console.
#include <string>
#include "bz_gfx.h"

namespace pal {
constexpr uint16_t BG = gfx::rgb(0x0e1218), PANEL = gfx::rgb(0x161c25), LINE = gfx::rgb(0x253041),
                   TXT = gfx::rgb(0xe8eaee), DIM = gfx::rgb(0x6b7687), SUB = gfx::rgb(0x9aa5b5),
                   AMBER = gfx::rgb(0xffb21a), SELTXT = gfx::rgb(0x1a1200), GREEN = gfx::rgb(0x4cd964),
                   RED = gfx::rgb(0xff4d4f), BLUE = gfx::rgb(0x4aa8ff), YELLOW = gfx::rgb(0xffcc00),
                   SHADE = gfx::rgb(0x05070a);
}

// Dims the current frame and draws a centred dialog with 1-2 buttons (sel = highlighted button).
void uiDialog(const std::string& title, const std::string& body, const std::string* opts, int nopt, int sel, bool dim = true);
// Small hint line at the bottom of a screen, e.g. "OK — грати · Назад — меню".
void uiHint(const std::string& s);
