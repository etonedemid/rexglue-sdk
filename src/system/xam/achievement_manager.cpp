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
#include <fstream>
#include <sstream>

#include <fmt/format.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/string/util.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

namespace {

// Minimal JSON string escaping (no external JSON library dependency).
std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

// Very small JSON value parser — handles the subset we write.
// Skips whitespace, returns the next non-whitespace char, or 0 on EOF.
char SkipWs(std::istream& in) {
  char c;
  while (in.get(c)) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      return c;
  }
  return 0;
}

// Read a JSON string (after the opening '"' has been consumed).
std::string ReadJsonString(std::istream& in) {
  std::string out;
  char c;
  while (in.get(c)) {
    if (c == '"')
      return out;
    if (c == '\\') {
      if (!in.get(c))
        break;
      switch (c) {
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        default:
          out += c;
          break;
      }
    } else {
      out += c;
    }
  }
  return out;
}

// Read a JSON number token (integer or unsigned).
uint64_t ReadJsonUint(std::istream& in, char first) {
  std::string num;
  num += first;
  char c;
  while (in.get(c)) {
    if (c >= '0' && c <= '9') {
      num += c;
    } else {
      in.putback(c);
      break;
    }
  }
  return std::stoull(num);
}

// Read a JSON boolean.
bool ReadJsonBool(std::istream& in, char first) {
  // first is 't' or 'f'
  if (first == 't') {
    in.ignore(3);  // "rue"
    return true;
  }
  in.ignore(4);  // "alse"
  return false;
}

}  // namespace

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
  auto file_path = content_dir / "achievements.json";

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return;
  }

  // Parse the JSON array of achievement objects.
  // Expected format: [ { "id": N, "unlocked": bool, "unlock_time": N }, ... ]
  char c = SkipWs(file);
  if (c != '[')
    return;

  while (file.good()) {
    c = SkipWs(file);
    if (c == ']')
      break;
    if (c == ',')
      c = SkipWs(file);
    if (c != '{')
      break;

    uint32_t id = 0;
    bool unlocked = false;
    uint64_t unlock_time = 0;

    // Read key-value pairs within the object.
    while (file.good()) {
      c = SkipWs(file);
      if (c == '}')
        break;
      if (c == ',')
        c = SkipWs(file);
      if (c != '"')
        break;
      std::string key = ReadJsonString(file);
      c = SkipWs(file);  // ':'
      if (c != ':')
        break;
      c = SkipWs(file);  // value start

      if (key == "id") {
        id = static_cast<uint32_t>(ReadJsonUint(file, c));
      } else if (key == "unlocked") {
        unlocked = ReadJsonBool(file, c);
      } else if (key == "unlock_time") {
        unlock_time = ReadJsonUint(file, c);
      } else {
        // Skip unknown values — primitive only (string or number or bool).
        if (c == '"') {
          ReadJsonString(file);
        } else if (c == 't' || c == 'f') {
          ReadJsonBool(file, c);
        } else {
          ReadJsonUint(file, c);
        }
      }
    }

    if (unlocked) {
      auto it = id_to_index_.find(id);
      if (it != id_to_index_.end()) {
        achievements_[it->second].unlocked = true;
        achievements_[it->second].unlock_time = unlock_time;
      }
    }
  }

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
  auto file_path = content_dir / "achievements.json";

  std::ofstream file(file_path);
  if (!file.is_open()) {
    REXSYS_ERROR("AchievementManager: failed to save state to {}", file_path.string());
    return;
  }

  file << "[\n";
  for (size_t i = 0; i < achievements_.size(); ++i) {
    const auto& a = achievements_[i];
    file << "  {\n";
    file << "    \"id\": " << a.id << ",\n";
    file << "    \"label\": \"" << JsonEscape(a.label) << "\",\n";
    file << "    \"description\": \"" << JsonEscape(a.description) << "\",\n";
    file << "    \"gamerscore\": " << a.gamerscore << ",\n";
    file << "    \"unlocked\": " << (a.unlocked ? "true" : "false") << ",\n";
    file << "    \"unlock_time\": " << a.unlock_time << "\n";
    file << "  }";
    if (i + 1 < achievements_.size()) {
      file << ",";
    }
    file << "\n";
  }
  file << "]\n";

  // Export each achievement icon as a PNG file in an "icons" subdirectory.
  ExportIconPngs(content_dir);
}

void AchievementManager::ExportIconPngs(const std::filesystem::path& content_dir) {
  auto icons_dir = content_dir / "icons";
  std::filesystem::create_directories(icons_dir);

  const util::XdbfGameData db = kernel_state_->title_xdbf();
  if (!db.is_valid()) {
    return;
  }

  for (const auto& a : achievements_) {
    if (!a.image_id) {
      continue;
    }
    auto icon_path = icons_dir / fmt::format("{}.png", a.id);
    if (std::filesystem::exists(icon_path)) {
      continue;  // Already exported.
    }
    auto block = db.GetEntry(util::XdbfSection::kImage, a.image_id);
    if (!block) {
      continue;
    }
    std::ofstream icon_file(icon_path, std::ios::binary);
    if (icon_file.is_open()) {
      icon_file.write(reinterpret_cast<const char*>(block.buffer),
                      static_cast<std::streamsize>(block.size));
    }
  }
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
