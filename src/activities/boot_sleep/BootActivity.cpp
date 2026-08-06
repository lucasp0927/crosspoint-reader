#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

// Branch and short SHA, injected by scripts/git_branch.py (empty for the dev
// env, whose CROSSPOINT_VERSION already carries them). Defined here as a
// fallback so a build without the pre-script still compiles.
#ifndef CROSSPOINT_BUILD_ID
#define CROSSPOINT_BUILD_ID ""
#endif

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  // Adjacent string literals, so this concatenates at compile time.
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION CROSSPOINT_BUILD_ID);
  renderer.displayBuffer();
}
