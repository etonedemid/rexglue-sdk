/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <rex/ui/imgui_dialog.h>

namespace rex::ui {

// Non-blocking toast notification for achievement unlocks.
// Renders as a small overlay in the bottom-right corner and auto-closes
// after a configurable duration.
class AchievementToast : public ImGuiDialog {
 public:
  AchievementToast(ImGuiDrawer* imgui_drawer, std::string title, uint16_t gamerscore,
                   std::vector<uint8_t> icon_png = {},
                   float duration_seconds = 5.0f);
  ~AchievementToast();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::string title_;
  uint16_t gamerscore_;
  float duration_seconds_;
  std::chrono::steady_clock::time_point start_time_;
  bool started_ = false;
  SDL_AudioStream* audio_stream_ = nullptr;

  // Static banner texture (decoded from embedded achievement.png)
  std::unique_ptr<ImmediateTexture> banner_texture_;
  bool banner_decoded_ = false;

  // Per-achievement icon texture (decoded from XDBF PNG data)
  std::vector<uint8_t> icon_png_;
  std::unique_ptr<ImmediateTexture> icon_texture_;
  bool icon_decoded_ = false;
};

}  // namespace rex::ui
