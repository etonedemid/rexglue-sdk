/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <rex/ui/achievement_toast.h>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>
#include <imgui.h>

#include <rex/ui/achievement_image_data.h>
#include <rex/ui/achievement_sound_data.h>
#include <rex/ui/imgui_drawer.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../thirdparty/tracy/profiler/src/stb_image.h"

namespace rex::ui {

AchievementToast::AchievementToast(ImGuiDrawer* imgui_drawer, std::string title,
                                   uint16_t gamerscore,
                                   std::vector<uint8_t> icon_png,
                                   float duration_seconds)
    : ImGuiDialog(imgui_drawer),
      title_(std::move(title)),
      gamerscore_(gamerscore),
      icon_png_(std::move(icon_png)),
      duration_seconds_(duration_seconds) {}

AchievementToast::~AchievementToast() {
  if (audio_stream_) {
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
  }
}

void AchievementToast::OnDraw(ImGuiIO& io) {
  if (!started_) {
    start_time_ = std::chrono::steady_clock::now();
    started_ = true;

    // Decode embedded achievement.png banner → RGBA and upload to GPU.
    if (!banner_decoded_) {
      banner_decoded_ = true;
      int w = 0, h = 0, channels = 0;
      unsigned char* rgba = stbi_load_from_memory(
          kAchievementImageData, static_cast<int>(kAchievementImageSize),
          &w, &h, &channels, 4);
      if (rgba && w > 0 && h > 0) {
        banner_texture_ = imgui_drawer()->CreateTexture(
            static_cast<uint32_t>(w), static_cast<uint32_t>(h),
            ImmediateTextureFilter::kLinear, false, rgba);
        stbi_image_free(rgba);
      }
    }

    // Decode the per-achievement icon PNG (from XDBF) if one was supplied.
    if (!icon_decoded_ && !icon_png_.empty()) {
      icon_decoded_ = true;
      int iw = 0, ih = 0, ich = 0;
      unsigned char* icon_rgba = stbi_load_from_memory(
          icon_png_.data(), static_cast<int>(icon_png_.size()),
          &iw, &ih, &ich, 4);
      if (icon_rgba && iw > 0 && ih > 0) {
        icon_texture_ = imgui_drawer()->CreateTexture(
            static_cast<uint32_t>(iw), static_cast<uint32_t>(ih),
            ImmediateTextureFilter::kLinear, false, icon_rgba);
        stbi_image_free(icon_rgba);
      }
      icon_png_.clear();  // free memory after upload
    }

    // Play the achievement unlock sound.
    SDL_AudioSpec spec;
    uint8_t* audio_buf = nullptr;
    uint32_t audio_len = 0;
    SDL_IOStream* io_stream =
        SDL_IOFromConstMem(kAchievementSoundData, kAchievementSoundSize);
    if (io_stream &&
        SDL_LoadWAV_IO(io_stream, /*closeio=*/true, &spec, &audio_buf, &audio_len)) {
      audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                &spec, nullptr, nullptr);
      if (audio_stream_) {
        SDL_PutAudioStreamData(audio_stream_, audio_buf, static_cast<int>(audio_len));
        SDL_ResumeAudioStreamDevice(audio_stream_);
      }
      SDL_free(audio_buf);
    }
  }

  auto elapsed = std::chrono::steady_clock::now() - start_time_;
  float elapsed_sec =
      std::chrono::duration_cast<std::chrono::duration<float>>(elapsed).count();

  if (elapsed_sec >= duration_seconds_) {
    Close();
    return;
  }

  // Fade in/out animation
  float alpha = 1.0f;
  constexpr float kFadeDuration = 0.4f;
  if (elapsed_sec < kFadeDuration) {
    alpha = elapsed_sec / kFadeDuration;
  } else if (elapsed_sec > duration_seconds_ - kFadeDuration) {
    alpha = (duration_seconds_ - elapsed_sec) / kFadeDuration;
  }

  // Layout: render the achievement.png banner (820×208 → scaled to 270×69)
  // centered horizontally at the bottom, with gamerscore/title text overlay.
  constexpr float kBannerWidth = 270.0f;
  constexpr float kBannerHeight = 69.0f;
  float padding = 20.0f;

  ImVec2 display_size = io.DisplaySize;
  ImVec2 window_pos((display_size.x - kBannerWidth) * 0.5f,
                    display_size.y - kBannerHeight - padding);

  ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(kBannerWidth, kBannerHeight), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoSavedSettings;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

  if (ImGui::Begin("##AchievementToast", nullptr, flags)) {
    // Draw the achievement.png banner as background.
    if (banner_texture_) {
      ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
      ImGui::ImageWithBg(
          reinterpret_cast<ImTextureID>(banner_texture_.get()),
          ImVec2(kBannerWidth, kBannerHeight),
          ImVec2(0, 0), ImVec2(1, 1),
          ImVec4(0, 0, 0, 0),
          ImVec4(1, 1, 1, alpha));
    }

    // Overlay gamerscore + title text in the lower portion of the banner.
    // The achievement ring icon occupies ~25% of the left side; text area is to the right.
    ImVec2 win_pos = ImGui::GetWindowPos();
    float text_x = win_pos.x + kBannerWidth * 0.28f - 10.0f; 
    float text_y = win_pos.y + kBannerHeight * 0.60f;

    ImFont* font = ImGui::GetFont();
    float scaled_size = ImGui::GetFontSize() * 1.35f;
    ImGui::GetWindowDrawList()->AddText(
        font, scaled_size,
        ImVec2(text_x, text_y),
        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)),
        (std::to_string(gamerscore_) + " G - " + title_).c_str());

    // Draw per-achievement icon (rounded) on the left side, over the banner's ring slot.
    if (icon_texture_) {
      constexpr float kIconSize = 53.0f;
      constexpr float kIconX = 8.0f;
      float icon_y = (kBannerHeight - kIconSize) * 0.5f;
      ImVec2 icon_min(win_pos.x + kIconX, win_pos.y + icon_y);
      ImVec2 icon_max(icon_min.x + kIconSize, icon_min.y + kIconSize);
      ImGui::GetWindowDrawList()->AddImageRounded(
          reinterpret_cast<ImTextureID>(icon_texture_.get()),
          icon_min, icon_max,
          ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
          ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)),
          6.0f);
    }
  }
  ImGui::End();

  ImGui::PopStyleVar(3);
}

}  // namespace rex::ui
