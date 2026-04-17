/**
 ******************************************************************************
 * ReXGlue SDK - RetroAchievements client
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 ******************************************************************************
 *
 * Wraps the rcheevos rc_client to provide online RetroAchievements support.
 * Runs an HTTP worker thread (libcurl) for all server communication.
 * Thread safety:
 *   DoFrame()     — must be called from the rendering thread.
 *   LoadGame()    — call after the XEX module is loaded (any thread).
 *   Login*()      — safe to call from any thread.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

// Forward-declare rcheevos types (actual includes in ra_client.cpp)
struct rc_client_t;

namespace rex {
class Runtime;
}  // namespace rex

namespace rex::memory {
class Memory;
}  // namespace rex::memory

namespace rex::system::xam {

class AchievementManager;

class RAClient {
 public:
  explicit RAClient(const Runtime* emulator, AchievementManager* achievement_manager,
                    const std::filesystem::path& user_data_root);
  ~RAClient();

  // Disabled copy/move — owns the rc_client_t and HTTP thread.
  RAClient(const RAClient&) = delete;
  RAClient& operator=(const RAClient&) = delete;

  // ---------------------------------------------------------------------------
  // Game session
  // ---------------------------------------------------------------------------

  /// Load the game identified by |xex_path| (host filesystem path to the XEX).
  /// Also accepts the title_id as a fallback for the RA console / game lookup.
  /// Safe to call from any thread; the async load callback fires on the HTTP thread.
  void LoadGame(const std::string& xex_path, uint32_t title_id);

  /// Call once per rendered frame from the rendering thread.
  void DoFrame();

  /// Call when the emulated title soft-resets (state is discarded).
  void Reset();

  // ---------------------------------------------------------------------------
  // Authentication
  // ---------------------------------------------------------------------------

  using LoginCallback = std::function<void(bool /*success*/, std::string /*message*/)>;

  void LoginWithPassword(const std::string& username, const std::string& password,
                         LoginCallback callback);
  void LoginWithToken(const std::string& username, const std::string& token,
                      LoginCallback callback);
  void Logout();

  bool IsLoggedIn() const;
  std::string GetDisplayUsername() const;

 private:
  // Static callbacks and HttpRequest are defined in ra_client.cpp
  // and access private members through friendship.
  friend struct RAClientCallbacks;
  struct HttpRequest {
    std::string url;
    std::string post_data;
    std::string content_type;
    void* callback;
    void* callback_data;
  };

  // ---------------------------------------------------------------------------
  // HTTP worker
  // ---------------------------------------------------------------------------
  void RunHttpThread();
  void EnqueueRequest(HttpRequest req);

  // ---------------------------------------------------------------------------
  // Event handling (called from callbacks with opaque event pointer)
  // ---------------------------------------------------------------------------
  void OnAchievementTriggered(const void* event);
  void OnGameCompleted(const void* event);

  // ---------------------------------------------------------------------------
  // Credential persistence
  // ---------------------------------------------------------------------------
  void LoadSavedCredentials();
  void SaveCredentials(const std::string& username, const std::string& token);
  void ClearCredentials();

  // ---------------------------------------------------------------------------
  // Members
  // ---------------------------------------------------------------------------
  rc_client_t* client_ = nullptr;
  memory::Memory* memory_ = nullptr;
  const Runtime* emulator_ = nullptr;
  AchievementManager* achievement_manager_ = nullptr;
  std::filesystem::path user_data_root_;

  // HTTP worker thread
  std::thread http_thread_;
  std::atomic<bool> http_running_{false};
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::queue<HttpRequest> request_queue_;
};

}  // namespace rex::system::xam
