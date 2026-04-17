/**
 * @file        rex/ui/overlay/achievements_overlay.h
 *
 * @brief       ImGui overlay showing all achievements for the current title.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <memory>
#include <unordered_map>
#include <rex/ui/imgui_dialog.h>

namespace rex::ui {

class AchievementsOverlay : public ImGuiDialog {
 public:
  explicit AchievementsOverlay(ImGuiDrawer* imgui_drawer);

  void ToggleVisible() { visible_ = !visible_; }
  bool IsVisible() const { return visible_; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool visible_ = false;
  std::unordered_map<uint32_t, std::unique_ptr<ImmediateTexture>> icon_textures_;
  bool icons_loaded_ = false;
};

}  // namespace rex::ui
