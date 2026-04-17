/**
 ******************************************************************************
 * ReXGlue SDK                                                                *
 ******************************************************************************
 * Gamer Profile — shared profile, achievement, and playtime storage          *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

namespace rex::gamer {

struct Achievement {
    uint32_t title_id;
    uint16_t achievement_id;
    std::string name;
    std::string description;
    uint16_t gamerscore;
    int64_t unlocked_time;  // 0 if locked, unix timestamp if unlocked
    std::string image_path; // relative path to icon if available

    bool is_unlocked() const { return unlocked_time != 0; }
};

struct GamePlaytime {
    uint32_t title_id;
    std::string title_name;
    uint64_t total_seconds;
    int64_t last_played;  // unix timestamp
};

struct GamerProfile {
    std::string gamertag;
    std::string gamerpic_path;  // path to gamerpic image
    uint64_t xuid;
    uint32_t total_gamerscore;
    int64_t created_time;

    std::vector<Achievement> achievements;
    std::vector<GamePlaytime> playtime;
};

class GamerProfileManager {
public:
    static GamerProfileManager& instance();

    /// Get shared storage root: ~/.local/share/rexglue/
    std::filesystem::path shared_root() const;

    /// Profile file path
    std::filesystem::path profile_path() const;

    /// Achievements storage path for a title
    std::filesystem::path achievements_path(uint32_t title_id) const;

    /// Playtime storage path
    std::filesystem::path playtime_path() const;

    /// Gamerpics directory
    std::filesystem::path gamerpics_dir() const;

    /// Load or create the active profile. If none exists, creates a default.
    bool load_or_create_default();

    /// Load profile from disk
    bool load();

    /// Save profile to disk
    bool save() const;

    /// Whether a profile exists
    bool profile_exists() const;

    /// Get current profile (call load first)
    const GamerProfile& profile() const { return profile_; }
    GamerProfile& profile() { return profile_; }

    /// Set gamertag
    void set_gamertag(const std::string& tag);

    /// Set gamerpic (copies file to shared store)
    void set_gamerpic(const std::filesystem::path& source);

    // ── Achievement tracking ──

    /// Load achievements for a given title from shared store
    std::vector<Achievement> load_achievements(uint32_t title_id) const;

    /// Unlock an achievement, returns true if newly unlocked
    bool unlock_achievement(uint32_t title_id, uint16_t achievement_id,
                            const std::string& name, const std::string& desc,
                            uint16_t gamerscore);

    /// Save achievements for a title to shared store
    void save_achievements(uint32_t title_id, const std::vector<Achievement>& achievements) const;

    /// Get total gamerscore across all titles
    uint32_t compute_total_gamerscore() const;

    // ── Playtime tracking ──

    /// Start tracking playtime for a title
    void start_session(uint32_t title_id, const std::string& title_name);

    /// End the current play session (saves elapsed time)
    void end_session();

    /// Get playtime for a specific title
    GamePlaytime get_playtime(uint32_t title_id) const;

private:
    GamerProfileManager() = default;

    GamerProfile profile_;
    mutable std::mutex mutex_;

    // Active session tracking
    uint32_t session_title_id_ = 0;
    std::string session_title_name_;
    std::chrono::steady_clock::time_point session_start_;
    bool session_active_ = false;

    void ensure_dirs() const;
};

}  // namespace rex::gamer
