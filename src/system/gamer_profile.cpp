/**
 ******************************************************************************
 * ReXGlue SDK                                                                *
 ******************************************************************************
 * Gamer Profile — shared profile, achievement, and playtime storage          *
 ******************************************************************************
 */

#include <rex/system/gamer_profile.h>

#include <ctime>
#include <fstream>
#include <random>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <rex/logging.h>

namespace rex::gamer {

using json = nlohmann::json;

static constexpr const char* kProfileFileName = "profile.json";
static constexpr const char* kPlaytimeFileName = "playtime.json";
static constexpr const char* kAchievementsDir = "achievements";
static constexpr const char* kGamepicsDir = "gamerpics";
static constexpr const char* kProfilesDir = "profiles";
static constexpr const char* kProfilesIndexFile = "profiles.json";

static int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

static uint64_t generate_xuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    // Set bits 54-55 to avoid the mask check (0x00C0000000000000)
    // Use 0xB13E prefix like the original, but randomize the rest
    std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFF);
    return 0xB13E000000000000ULL | dist(gen);
}

GamerProfileManager& GamerProfileManager::instance() {
    static GamerProfileManager mgr;
    return mgr;
}

std::filesystem::path GamerProfileManager::shared_root() const {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) {
        return std::filesystem::path(xdg) / "rexglue";
    }
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata && appdata[0]) {
        return std::filesystem::path(appdata) / "rexglue";
    }
#endif
    const char* home = std::getenv("HOME");
    if (home && home[0]) {
        return std::filesystem::path(home) / ".local" / "share" / "rexglue";
    }
    return std::filesystem::current_path() / ".rexglue";
}

std::filesystem::path GamerProfileManager::profile_root() const {
    return shared_root() / kProfilesDir / fmt::format("{:016X}", active_xuid_);
}

std::filesystem::path GamerProfileManager::profile_path() const {
    return profile_root() / kProfileFileName;
}

std::filesystem::path GamerProfileManager::achievements_path(uint32_t title_id) const {
    return profile_root() / kAchievementsDir / fmt::format("{:08X}.json", title_id);
}

std::filesystem::path GamerProfileManager::playtime_path() const {
    return profile_root() / kPlaytimeFileName;
}

std::filesystem::path GamerProfileManager::gamerpics_dir() const {
    return shared_root() / kGamepicsDir;
}

void GamerProfileManager::ensure_dirs() const {
    std::filesystem::create_directories(profile_root() / kAchievementsDir);
    std::filesystem::create_directories(gamerpics_dir());
}

void GamerProfileManager::set_active_xuid(uint64_t xuid) {
    active_xuid_ = xuid;
}

std::vector<ProfileEntry> GamerProfileManager::enumerate_profiles() const {
    std::vector<ProfileEntry> entries;
    auto index_path = shared_root() / kProfilesIndexFile;
    if (!std::filesystem::exists(index_path)) return entries;

    try {
        std::ifstream f(index_path);
        if (!f.is_open()) return entries;
        json j = json::parse(f);
        for (auto& entry : j) {
            ProfileEntry pe;
            pe.xuid = std::stoull(entry.value("xuid", "0"), nullptr, 0);
            pe.gamertag = entry.value("gamertag", "Player");
            if (pe.xuid != 0) entries.push_back(pe);
        }
    } catch (...) {}

    return entries;
}

void GamerProfileManager::save_profile_index() const {
    try {
        std::filesystem::create_directories(shared_root());
        auto index_path = shared_root() / kProfilesIndexFile;

        // Read existing entries, update/add ours
        json arr = json::array();
        if (std::filesystem::exists(index_path)) {
            std::ifstream f(index_path);
            if (f.is_open()) {
                try { arr = json::parse(f); } catch (...) { arr = json::array(); }
            }
        }

        bool found = false;
        auto xuid_str = fmt::format("0x{:016X}", profile_.xuid);
        for (auto& entry : arr) {
            if (entry.value("xuid", "") == xuid_str) {
                entry["gamertag"] = profile_.gamertag;
                found = true;
                break;
            }
        }
        if (!found) {
            json entry;
            entry["xuid"] = xuid_str;
            entry["gamertag"] = profile_.gamertag;
            arr.push_back(entry);
        }

        std::ofstream f(index_path);
        if (f.is_open()) f << arr.dump(2);
    } catch (...) {}
}

void GamerProfileManager::migrate_legacy_data() {
    // Check for legacy flat layout: shared_root()/profile.json
    auto legacy_profile = shared_root() / kProfileFileName;
    if (!std::filesystem::exists(legacy_profile)) return;

    try {
        std::ifstream f(legacy_profile);
        if (!f.is_open()) return;
        json j = json::parse(f);
        f.close();

        uint64_t xuid = 0;
        if (j.contains("xuid") && j["xuid"].is_string()) {
            xuid = std::stoull(j["xuid"].get<std::string>(), nullptr, 0);
        } else {
            xuid = j.value("xuid", generate_xuid());
        }

        // Create per-XUID directory and move files
        auto dest_dir = shared_root() / kProfilesDir / fmt::format("{:016X}", xuid);
        std::filesystem::create_directories(dest_dir / kAchievementsDir);

        // Move profile.json
        std::filesystem::rename(legacy_profile, dest_dir / kProfileFileName);

        // Move playtime.json
        auto legacy_playtime = shared_root() / kPlaytimeFileName;
        if (std::filesystem::exists(legacy_playtime)) {
            std::filesystem::rename(legacy_playtime, dest_dir / kPlaytimeFileName);
        }

        // Move achievements/
        auto legacy_achiev = shared_root() / kAchievementsDir;
        if (std::filesystem::exists(legacy_achiev) && std::filesystem::is_directory(legacy_achiev)) {
            for (auto& entry : std::filesystem::directory_iterator(legacy_achiev)) {
                std::filesystem::rename(entry.path(), dest_dir / kAchievementsDir / entry.path().filename());
            }
            std::filesystem::remove(legacy_achiev);
        }

        // Create profiles.json index
        auto gamertag = j.value("gamertag", "Player");
        json arr = json::array();
        json pe;
        pe["xuid"] = fmt::format("0x{:016X}", xuid);
        pe["gamertag"] = gamertag;
        arr.push_back(pe);
        std::ofstream idx(shared_root() / kProfilesIndexFile);
        if (idx.is_open()) idx << arr.dump(2);

        REXSYS_INFO("Migrated legacy profile data to per-XUID folder {:016X}", xuid);
    } catch (const std::exception& e) {
        REXSYS_ERROR("Failed to migrate legacy profile data: {}", e.what());
    }
}

bool GamerProfileManager::profile_exists() const {
    if (active_xuid_ == 0) {
        // Check if any profiles exist at all (legacy or new)
        auto legacy = shared_root() / kProfileFileName;
        auto index = shared_root() / kProfilesIndexFile;
        return std::filesystem::exists(legacy) || std::filesystem::exists(index);
    }
    return std::filesystem::exists(profile_path());
}

bool GamerProfileManager::load_or_create_default() {
    std::lock_guard lock(mutex_);

    // Migrate legacy flat layout if present
    migrate_legacy_data();

    // If an active_xuid_ is set and the profile exists, load it
    if (active_xuid_ != 0 && std::filesystem::exists(profile_path())) {
        return load_impl();
    }

    // Try to load first profile from index
    auto profiles = enumerate_profiles();
    if (!profiles.empty() && active_xuid_ == 0) {
        active_xuid_ = profiles[0].xuid;
        if (std::filesystem::exists(profile_path())) {
            return load_impl();
        }
    }

    // Create default profile
    if (active_xuid_ == 0) {
        active_xuid_ = generate_xuid();
    }
    ensure_dirs();

    profile_.gamertag = "Player";
    profile_.xuid = generate_xuid();
    profile_.gamerpic_path = "";
    profile_.total_gamerscore = 0;
    profile_.created_time = now_unix();

    REXSYS_INFO("Created default gamer profile '{}'", profile_.gamertag);
    return save_impl();
}

bool GamerProfileManager::load() {
    std::lock_guard lock(mutex_);
    return load_impl();
}

bool GamerProfileManager::load_impl() {
    try {
        std::ifstream f(profile_path());
        if (!f.is_open()) return false;

        json j = json::parse(f);

        profile_.gamertag = j.value("gamertag", "Player");

        // XUID: support both hex string (new) and numeric (legacy)
        if (j.contains("xuid") && j["xuid"].is_string()) {
            profile_.xuid = std::stoull(j["xuid"].get<std::string>(), nullptr, 0);
        } else {
            profile_.xuid = j.value("xuid", generate_xuid());
        }

        profile_.gamerpic_path = j.value("gamerpic_path", "");
        profile_.total_gamerscore = j.value("total_gamerscore", 0u);
        profile_.created_time = j.value("created_time", now_unix());

        // Load playtime entries
        profile_.playtime.clear();
        auto pt_path = playtime_path();
        if (std::filesystem::exists(pt_path)) {
            std::ifstream pf(pt_path);
            if (pf.is_open()) {
                json pj = json::parse(pf);
                for (auto& [key, val] : pj.items()) {
                    GamePlaytime pt;
                    pt.title_id = std::stoul(key, nullptr, 16);
                    pt.title_name = val.value("title_name", "");
                    pt.total_seconds = val.value("total_seconds", uint64_t(0));
                    pt.last_played = val.value("last_played", int64_t(0));
                    profile_.playtime.push_back(pt);
                }
            }
        }

        REXSYS_INFO("Loaded gamer profile '{}' (XUID: {:016X})", profile_.gamertag, profile_.xuid);
        return true;
    } catch (const json::exception& e) {
        REXSYS_ERROR("Failed to load gamer profile: {}", e.what());
        return false;
    }
}

bool GamerProfileManager::save() const {
    std::lock_guard lock(mutex_);
    return save_impl();
}

bool GamerProfileManager::save_impl() const {
    try {
        ensure_dirs();

        // Save profile — XUID as hex string for cross-app compatibility
        json j;
        j["gamertag"] = profile_.gamertag;
        j["xuid"] = fmt::format("0x{:016X}", profile_.xuid);
        j["gamerpic_path"] = profile_.gamerpic_path;
        j["total_gamerscore"] = profile_.total_gamerscore;
        j["created_time"] = profile_.created_time;

        std::ofstream f(profile_path());
        if (!f.is_open()) return false;
        f << j.dump(2);
        f.close();

        // Save playtime
        json pj;
        for (auto& pt : profile_.playtime) {
            auto key = fmt::format("{:08X}", pt.title_id);
            pj[key]["title_name"] = pt.title_name;
            pj[key]["total_seconds"] = pt.total_seconds;
            pj[key]["last_played"] = pt.last_played;
        }

        std::ofstream pf(playtime_path());
        if (pf.is_open()) {
            pf << pj.dump(2);
            pf.close();
        }

        // Update profile index
        save_profile_index();

        return true;
    } catch (const json::exception& e) {
        REXSYS_ERROR("Failed to save gamer profile: {}", e.what());
        return false;
    }
}

void GamerProfileManager::set_gamertag(const std::string& tag) {
    std::lock_guard lock(mutex_);
    profile_.gamertag = tag;
    save_impl();
}

void GamerProfileManager::set_gamerpic(const std::filesystem::path& source) {
    if (!std::filesystem::exists(source)) return;

    std::lock_guard lock(mutex_);
    ensure_dirs();
    auto dest = gamerpics_dir() / source.filename();
    std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing);
    profile_.gamerpic_path = dest.string();
    save_impl();
}

// ── Achievement tracking ──

std::vector<Achievement> GamerProfileManager::load_achievements(uint32_t title_id) const {
    std::lock_guard lock(mutex_);
    return load_achievements_impl(title_id);
}

std::vector<Achievement> GamerProfileManager::load_achievements_impl(uint32_t title_id) const {
    std::vector<Achievement> result;

    auto path = achievements_path(title_id);
    if (!std::filesystem::exists(path)) return result;

    try {
        std::ifstream f(path);
        if (!f.is_open()) return result;

        json j = json::parse(f);
        for (auto& entry : j) {
            Achievement a;
            a.title_id = title_id;
            a.achievement_id = entry.value("id", uint16_t(0));
            a.name = entry.value("name", "");
            a.description = entry.value("description", "");
            a.gamerscore = entry.value("gamerscore", uint16_t(0));
            a.unlocked_time = entry.value("unlocked_time", int64_t(0));
            a.image_path = entry.value("image_path", "");
            result.push_back(a);
        }
    } catch (const json::exception& e) {
        REXSYS_ERROR("Failed to load achievements for {:08X}: {}", title_id, e.what());
    }

    return result;
}

bool GamerProfileManager::unlock_achievement(uint32_t title_id, uint16_t achievement_id,
                                              const std::string& name, const std::string& desc,
                                              uint16_t gamerscore) {
    std::lock_guard lock(mutex_);
    auto achievements = load_achievements_impl(title_id);

    // Check if already unlocked
    for (auto& a : achievements) {
        if (a.achievement_id == achievement_id) {
            if (a.is_unlocked()) return false;  // already unlocked
            a.unlocked_time = now_unix();
            save_achievements_impl(title_id, achievements);
            profile_.total_gamerscore += gamerscore;
            save_impl();
            REXSYS_INFO("Achievement unlocked: {} (+{}G)", name, gamerscore);
            return true;
        }
    }

    // New achievement entry
    Achievement a;
    a.title_id = title_id;
    a.achievement_id = achievement_id;
    a.name = name;
    a.description = desc;
    a.gamerscore = gamerscore;
    a.unlocked_time = now_unix();
    achievements.push_back(a);

    save_achievements_impl(title_id, achievements);
    profile_.total_gamerscore += gamerscore;
    save_impl();
    REXSYS_INFO("Achievement unlocked: {} (+{}G)", name, gamerscore);
    return true;
}

void GamerProfileManager::save_achievements(uint32_t title_id,
                                             const std::vector<Achievement>& achievements) const {
    std::lock_guard lock(mutex_);
    save_achievements_impl(title_id, achievements);
}

void GamerProfileManager::save_achievements_impl(uint32_t title_id,
                                                  const std::vector<Achievement>& achievements) const {
    try {
        ensure_dirs();
        json j = json::array();
        for (auto& a : achievements) {
            json entry;
            entry["id"] = a.achievement_id;
            entry["name"] = a.name;
            entry["description"] = a.description;
            entry["gamerscore"] = a.gamerscore;
            entry["unlocked_time"] = a.unlocked_time;
            entry["image_path"] = a.image_path;
            j.push_back(entry);
        }

        std::ofstream f(achievements_path(title_id));
        if (f.is_open()) {
            f << j.dump(2);
        }
    } catch (const json::exception& e) {
        REXSYS_ERROR("Failed to save achievements for {:08X}: {}", title_id, e.what());
    }
}

uint32_t GamerProfileManager::compute_total_gamerscore() const {
    uint32_t total = 0;

    auto achiev_dir = profile_root() / kAchievementsDir;
    if (!std::filesystem::exists(achiev_dir)) return 0;

    for (auto& entry : std::filesystem::directory_iterator(achiev_dir)) {
        if (entry.path().extension() != ".json") continue;
        try {
            std::ifstream f(entry.path());
            json j = json::parse(f);
            for (auto& a : j) {
                if (a.value("unlocked_time", int64_t(0)) != 0) {
                    total += a.value("gamerscore", uint16_t(0));
                }
            }
        } catch (...) {}
    }

    return total;
}

// ── Playtime tracking ──

void GamerProfileManager::start_session(uint32_t title_id, const std::string& title_name) {
    std::lock_guard lock(mutex_);
    session_title_id_ = title_id;
    session_title_name_ = title_name;
    session_start_ = std::chrono::steady_clock::now();
    session_active_ = true;
    REXSYS_INFO("Play session started for {} ({:08X})", title_name, title_id);
}

void GamerProfileManager::end_session() {
    std::lock_guard lock(mutex_);
    if (!session_active_) return;

    auto elapsed = std::chrono::steady_clock::now() - session_start_;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    session_active_ = false;

    // Find or create playtime entry
    GamePlaytime* found = nullptr;
    for (auto& pt : profile_.playtime) {
        if (pt.title_id == session_title_id_) {
            found = &pt;
            break;
        }
    }

    if (!found) {
        profile_.playtime.push_back({});
        found = &profile_.playtime.back();
        found->title_id = session_title_id_;
        found->title_name = session_title_name_;
        found->total_seconds = 0;
    }

    found->total_seconds += static_cast<uint64_t>(seconds);
    found->last_played = now_unix();

    REXSYS_INFO("Play session ended for {} — {} seconds this session, {} total",
                session_title_name_, seconds, found->total_seconds);

    save_impl();
}

GamePlaytime GamerProfileManager::get_playtime(uint32_t title_id) const {
    for (auto& pt : profile_.playtime) {
        if (pt.title_id == title_id) return pt;
    }
    return {title_id, "", 0, 0};
}

}  // namespace rex::gamer
