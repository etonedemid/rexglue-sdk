/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <rex/system/xam/achievement_manager.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <fmt/format.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/string/util.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

namespace rex::system::xam {

AchievementManager::AchievementManager(KernelState* kernel_state) : kernel_state_(kernel_state) {}

void AchievementManager::LoadTitleAchievements() {
  std::lock_guard lock(mutex_);

  achievements_.clear();
  id_to_index_.clear();
  loaded_ = false;

  const util::XdbfGameData db = kernel_state_->title_xdbf();
  if (!db.is_valid()) {
    return;
  }

  const XLanguage language = db.GetExistingLanguage(db.default_language());
  const auto achievement_list = db.GetAchievements();

  for (const auto& entry : achievement_list) {
    AchievementState state{};
    state.id = entry.id;
    state.label = db.GetStringTableEntry(language, entry.label_id);
    state.description = db.GetStringTableEntry(language, entry.description_id);
    state.unachieved_desc = db.GetStringTableEntry(language, entry.unachieved_id);
    state.image_id = entry.image_id;
    state.gamerscore = entry.gamerscore;
    state.flags = entry.flags;
    state.unlocked = false;
    state.unlock_time = 0;

    id_to_index_[state.id] = achievements_.size();
    achievements_.push_back(std::move(state));
  }

  LoadUnlockState();
  loaded_ = true;

  REXSYS_INFO("AchievementManager: loaded {} achievements for title {:08X}", achievements_.size(),
              kernel_state_->title_id());
}

std::vector<uint32_t> AchievementManager::UnlockAchievements(const std::vector<uint32_t>& ids) {
  std::lock_guard lock(mutex_);

  if (!loaded_) {
    return {};
  }

  std::vector<uint32_t> newly_unlocked;
  uint64_t now = CurrentFileTime();

  for (uint32_t id : ids) {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) {
      REXSYS_WARN("AchievementManager: unknown achievement ID {}", id);
      continue;
    }

    auto& achievement = achievements_[it->second];
    if (achievement.unlocked) {
      continue;
    }

    achievement.unlocked = true;
    achievement.unlock_time = now;
    newly_unlocked.push_back(id);

    REXSYS_INFO("Achievement unlocked: \"{}\" (+{} G)", achievement.label, achievement.gamerscore);
  }

  if (!newly_unlocked.empty()) {
    SaveUnlockState();
  }

  return newly_unlocked;
}

bool AchievementManager::IsUnlocked(uint32_t id) const {
  std::lock_guard lock(mutex_);
  auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return false;
  }
  return achievements_[it->second].unlocked;
}

uint64_t AchievementManager::GetUnlockTime(uint32_t id) const {
  std::lock_guard lock(mutex_);
  auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return 0;
  }
  return achievements_[it->second].unlock_time;
}

uint32_t AchievementManager::GetTotalUnlockedGamerscore() const {
  std::lock_guard lock(mutex_);
  uint32_t total = 0;
  for (const auto& a : achievements_) {
    if (a.unlocked) {
      total += a.gamerscore;
    }
  }
  return total;
}

uint32_t AchievementManager::GetTotalGamerscore() const {
  std::lock_guard lock(mutex_);
  uint32_t total = 0;
  for (const auto& a : achievements_) {
    total += a.gamerscore;
  }
  return total;
}

std::vector<uint8_t> AchievementManager::GetAchievementIconPng(uint32_t achievement_id) const {
  std::lock_guard lock(mutex_);
  auto it = id_to_index_.find(achievement_id);
  if (it == id_to_index_.end()) {
    return {};
  }
  uint32_t image_id = achievements_[it->second].image_id;
  if (!image_id) {
    return {};
  }
  const util::XdbfGameData db = kernel_state_->title_xdbf();
  if (!db.is_valid()) {
    return {};
  }
  auto block = db.GetEntry(util::XdbfSection::kImage, image_id);
  if (!block) {
    return {};
  }
  return std::vector<uint8_t>(block.buffer, block.buffer + block.size);
}

void AchievementManager::LoadUnlockState() {
  auto content_dir = kernel_state_->content_manager()->ResolveGameUserContentPath();
  auto file_path = content_dir / "achievements.bin";

  auto file = rex::filesystem::OpenFile(file_path, "rb");
  if (!file) {
    return;
  }

  // Format: [uint32_t count] [uint32_t id, uint64_t unlock_time] * count
  uint32_t count = 0;
  if (fread(&count, sizeof(count), 1, file) != 1) {
    fclose(file);
    return;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id = 0;
    uint64_t unlock_time = 0;
    if (fread(&id, sizeof(id), 1, file) != 1)
      break;
    if (fread(&unlock_time, sizeof(unlock_time), 1, file) != 1)
      break;

    auto it = id_to_index_.find(id);
    if (it != id_to_index_.end()) {
      achievements_[it->second].unlocked = true;
      achievements_[it->second].unlock_time = unlock_time;
    }
  }

  fclose(file);

  uint32_t unlocked_count = 0;
  for (const auto& a : achievements_) {
    if (a.unlocked)
      ++unlocked_count;
  }
  REXSYS_INFO("AchievementManager: {}/{} achievements previously unlocked", unlocked_count,
              achievements_.size());
}

void AchievementManager::SaveUnlockState() {
  auto content_dir = kernel_state_->content_manager()->ResolveGameUserContentPath();
  std::filesystem::create_directories(content_dir);
  auto file_path = content_dir / "achievements.bin";

  auto file = rex::filesystem::OpenFile(file_path, "wb");
  if (!file) {
    REXSYS_ERROR("AchievementManager: failed to save unlock state to {}", file_path.string());
    return;
  }

  // Count unlocked
  uint32_t count = 0;
  for (const auto& a : achievements_) {
    if (a.unlocked)
      ++count;
  }

  fwrite(&count, sizeof(count), 1, file);
  for (const auto& a : achievements_) {
    if (a.unlocked) {
      fwrite(&a.id, sizeof(a.id), 1, file);
      fwrite(&a.unlock_time, sizeof(a.unlock_time), 1, file);
    }
  }

  fclose(file);
}

uint64_t AchievementManager::CurrentFileTime() const {
  // Windows FILETIME: 100-nanosecond intervals since 1601-01-01
  // Unix epoch offset in FILETIME units
  constexpr uint64_t kEpochDiff = 116444736000000000ULL;
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto hundred_ns =
      std::chrono::duration_cast<std::chrono::duration<uint64_t, std::ratio<1, 10000000>>>(
          duration);
  return hundred_ns.count() + kEpochDiff;
}

}  // namespace rex::system::xam
