/**
 * @file        ui/overlay/achievements_overlay.cpp
 *
 * @brief       All-achievements ImGui overlay with icon rendering.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include <rex/ui/overlay/achievements_overlay.h>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/achievement_manager.h>
#include <rex/ui/imgui_drawer.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../thirdparty/tracy/profiler/src/stb_image.h"

namespace rex::ui {

AchievementsOverlay::AchievementsOverlay(ImGuiDrawer* imgui_drawer)
    : ImGuiDialog(imgui_drawer) {
  // Register a command in the F4 settings menu
  rex::cvar::RegisterFlag({
      "show_achievements",
      rex::cvar::FlagType::Command,
      "Achievements",
      "Open the achievements overlay",
      [](std::string_view) { return false; },
      []() { return "<command>"; },
      [this]() { ToggleVisible(); },
      rex::cvar::Lifecycle::kHotReload,
      {},
      "<command>",
      false});
}

void AchievementsOverlay::OnDraw(ImGuiIO& io) {
  if (!visible_) return;

  auto* ks = rex::system::KernelState::shared();
  if (!ks || !ks->achievement_manager()) return;
  auto* achievement_manager = ks->achievement_manager();

  const auto& achievements = achievement_manager->achievements();

  // Load icon textures lazily
  if (!icons_loaded_) {
    icons_loaded_ = true;
    auto* drawer = imgui_drawer()->immediate_drawer();
    if (drawer) {
      for (const auto& a : achievements) {
        auto png = achievement_manager->GetAchievementIconPng(a.id);
        if (png.empty()) continue;
        int w = 0, h = 0, channels = 0;
        unsigned char* rgba = stbi_load_from_memory(
            png.data(), static_cast<int>(png.size()), &w, &h, &channels, 4);
        if (rgba && w > 0 && h > 0) {
          icon_textures_[a.id] = drawer->CreateTexture(
              static_cast<uint32_t>(w), static_cast<uint32_t>(h),
              ImmediateTextureFilter::kLinear, false, rgba);
          stbi_image_free(rgba);
        }
      }
    }
  }

  ImVec2 display_size = io.DisplaySize;
  float win_w = display_size.x * 0.6f;
  float win_h = display_size.y * 0.7f;
  if (win_w < 400.0f) win_w = 400.0f;
  if (win_h < 300.0f) win_h = 300.0f;

  ImGui::SetNextWindowPos(
      ImVec2((display_size.x - win_w) * 0.5f, (display_size.y - win_h) * 0.5f),
      ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Appearing);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
  bool open = visible_;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

  if (ImGui::Begin("Achievements", &open, flags)) {
    // Header: gamerscore summary
    uint32_t unlocked_gs = achievement_manager->GetTotalUnlockedGamerscore();
    uint32_t total_gs = achievement_manager->GetTotalGamerscore();
    uint32_t unlocked_count = 0;
    for (const auto& a : achievements) {
      if (a.unlocked) ++unlocked_count;
    }

    ImGui::Text("%u / %u achievements unlocked  |  %u / %u G",
                unlocked_count, static_cast<uint32_t>(achievements.size()),
                unlocked_gs, total_gs);
    ImGui::Separator();

    // Achievement list
    constexpr float kIconSize = 48.0f;
    constexpr float kItemHeight = 58.0f;

    for (const auto& a : achievements) {
      ImGui::PushID(static_cast<int>(a.id));

      float start_y = ImGui::GetCursorPosY();

      // Icon
      auto icon_it = icon_textures_.find(a.id);
      if (icon_it != icon_textures_.end() && icon_it->second) {
        ImGui::SetCursorPosY(start_y + (kItemHeight - kIconSize) * 0.5f - 4.0f);
        if (a.unlocked) {
          ImGui::Image(
              reinterpret_cast<ImTextureID>(icon_it->second.get()),
              ImVec2(kIconSize, kIconSize));
        } else {
          // Greyed-out for locked
          ImGui::ImageWithBg(
              reinterpret_cast<ImTextureID>(icon_it->second.get()),
              ImVec2(kIconSize, kIconSize),
              ImVec2(0, 0), ImVec2(1, 1),
              ImVec4(0, 0, 0, 0),
              ImVec4(0.3f, 0.3f, 0.3f, 0.6f));
        }
        ImGui::SameLine();
      }

      // Text
      ImGui::BeginGroup();
      if (a.unlocked) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", a.label.c_str());
      } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", a.label.c_str());
      }

      const char* desc = a.unlocked ? a.description.c_str() : a.unachieved_desc.c_str();
      if (desc && desc[0]) {
        ImGui::TextWrapped("%s", desc);
      }

      ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%u G", a.gamerscore);

      if (a.unlocked) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  Unlocked");
      }
      ImGui::EndGroup();

      // Ensure consistent item height
      float used = ImGui::GetCursorPosY() - start_y;
      if (used < kItemHeight) {
        ImGui::SetCursorPosY(start_y + kItemHeight);
      }

      ImGui::Separator();
      ImGui::PopID();
    }

    if (achievements.empty()) {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         "No achievements found for this title.");
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();

  if (!open) {
    visible_ = false;
  }
}

}  // namespace rex::ui
