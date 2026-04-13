/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/system/util/xdbf_utils.h>

namespace rex::ui {
class ImmediateDrawer;
class ImmediateTexture;
}  // namespace rex::ui

namespace rex::system {
class KernelState;
}

namespace rex::system::xam {

struct AchievementState {
  uint32_t id;
  std::string label;
  std::string description;
  std::string unachieved_desc;
  uint32_t image_id;
  uint16_t gamerscore;
  uint32_t flags;
  bool unlocked;
  uint64_t unlock_time;  // FILETIME
};

class AchievementManager {
 public:
  explicit AchievementManager(KernelState* kernel_state);

  // Load achievement definitions from the title's XDBF database.
  void LoadTitleAchievements();

  // Unlock one or more achievements by ID. Returns IDs that were newly
  // unlocked (skips already-unlocked ones).
  std::vector<uint32_t> UnlockAchievements(const std::vector<uint32_t>& ids);

  // Query whether a specific achievement is unlocked.
  bool IsUnlocked(uint32_t id) const;

  // Get the unlock time for an achievement (0 if not unlocked).
  uint64_t GetUnlockTime(uint32_t id) const;

  // Get all achievement states for the current title.
  // Lazily loads from XDBF on first call if not yet loaded.
  const std::vector<AchievementState>& achievements() {
    if (!loaded_)
      LoadTitleAchievements();
    return achievements_;
  }

  // Total unlocked gamerscore for the current title.
  uint32_t GetTotalUnlockedGamerscore() const;

  // Total possible gamerscore for the current title.
  uint32_t GetTotalGamerscore() const;

  // Get raw PNG icon data for an achievement by its achievement ID.
  // Returns empty vector if resolution or image_id is missing.
  std::vector<uint8_t> GetAchievementIconPng(uint32_t achievement_id) const;

 private:
  void LoadUnlockState();
  void SaveUnlockState();
  void ExportIconPngs(const std::filesystem::path& content_dir);
  uint64_t CurrentFileTime() const;

  KernelState* kernel_state_;
  mutable std::mutex mutex_;
  std::vector<AchievementState> achievements_;
  std::unordered_map<uint32_t, size_t> id_to_index_;
  bool loaded_ = false;
};

}  // namespace rex::system::xam
