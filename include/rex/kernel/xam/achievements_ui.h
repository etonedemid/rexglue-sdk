/**
 * @file        rex/kernel/xam/achievements_ui.h
 * @brief       Public API to open the achievements overlay from non-XAM code.
 */
#pragma once

namespace rex::ui {
class ImGuiDrawer;
}

namespace rex::kernel::xam {

/// Opens the achievements dialog as a standalone overlay (no XAM dispatch).
/// Safe to call from any UI-thread context (e.g. settings overlay button).
void OpenAchievementsOverlay(rex::ui::ImGuiDrawer* drawer);

}  // namespace rex::kernel::xam
