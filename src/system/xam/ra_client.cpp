/**
 ******************************************************************************
 * ReXGlue SDK - RetroAchievements client implementation
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 ******************************************************************************
 *
 * Implements RAClient using:
 *   - rcheevos rc_client for RA protocol logic
 *   - libcurl for HTTP transport in a dedicated worker thread
 *   - Credentials stored in <user_data_root>/ra_credentials.txt
 */

#include <rex/system/xam/ra_client.h>

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <curl/curl.h>
#include <fmt/format.h>

#include <rc_client.h>
#include <rc_consoles.h>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/system/xam/achievement_manager.h>
#include <rex/ui/achievement_toast.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

namespace rex::system::xam {

// ============================================================================
// Logging helpers
// ============================================================================

#define REXRA_INFO(fmt, ...)  REXSYS_INFO("[RA] " fmt, ##__VA_ARGS__)
#define REXRA_WARN(fmt, ...)  REXSYS_WARN("[RA] " fmt, ##__VA_ARGS__)
#define REXRA_ERROR(fmt, ...) REXSYS_ERROR("[RA] " fmt, ##__VA_ARGS__)
#define REXRA_DEBUG(fmt, ...) REXSYS_DEBUG("[RA] " fmt, ##__VA_ARGS__)

// HttpRequest struct — defined here rather than in the header to avoid
// exposing rcheevos types through the public header.
struct RAClient::HttpRequest {
  std::string url;
  std::string post_data;    // empty string → GET request
  std::string content_type; // default "application/x-www-form-urlencoded"
  rc_client_server_callback_t callback;
  void* callback_data;
};

// Friend struct providing static callbacks with the exact signatures
// expected by rcheevos.  These access RAClient private members.
struct RAClientCallbacks {
  static uint32_t ReadMemory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                             rc_client_t* client) {
    auto* self = static_cast<RAClient*>(rc_client_get_userdata(client));
    if (!self || !self->memory_ || !buffer || num_bytes == 0) {
      return 0;
    }
    const auto* host_ptr = self->memory_->TranslateVirtual<const uint8_t*>(address);
    if (!host_ptr) {
      return 0;
    }
    std::memcpy(buffer, host_ptr, num_bytes);
    return num_bytes;
  }

  static void ServerCall(const rc_api_request_t* request,
                         rc_client_server_callback_t callback, void* callback_data,
                         rc_client_t* client) {
    auto* self = static_cast<RAClient*>(rc_client_get_userdata(client));
    if (!self) {
      rc_api_server_response_t resp{};
      resp.http_status_code = 0;
      callback(&resp, callback_data);
      return;
    }

    RAClient::HttpRequest req;
    req.url = request->url ? request->url : "";
    req.post_data = (request->post_data && request->post_data[0]) ? request->post_data : "";
    req.content_type =
        (request->content_type && request->content_type[0])
            ? request->content_type
            : "application/x-www-form-urlencoded";
    req.callback = callback;
    req.callback_data = callback_data;

    self->EnqueueRequest(std::move(req));
  }

  static void EventHandler(const rc_client_event_t* event, rc_client_t* client) {
    auto* self = static_cast<RAClient*>(rc_client_get_userdata(client));
    if (!self || !event) return;

    switch (event->type) {
      case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        self->OnAchievementTriggered(event);
        break;
      case RC_CLIENT_EVENT_GAME_COMPLETED:
        self->OnGameCompleted(event);
        break;
      default:
        break;
    }
  }
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

RAClient::RAClient(const Runtime* emulator, AchievementManager* achievement_manager,
                   const std::filesystem::path& user_data_root)
    : emulator_(emulator),
      achievement_manager_(achievement_manager),
      user_data_root_(user_data_root) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  client_ = rc_client_create(RAClientCallbacks::ReadMemory, RAClientCallbacks::ServerCall);
  if (!client_) {
    REXRA_ERROR("rc_client_create failed");
    return;
  }

  // Store 'this' so static callbacks can recover the instance.
  rc_client_set_userdata(client_, this);

  // Cache the memory pointer — only valid once a KernelState is up, but we
  // set it here from the Runtime reference so the callback can use it.
  // The pointer itself is stable for the lifetime of the Runtime.
  if (emulator_) {
    memory_ = emulator_->memory();
  }

  rc_client_set_event_handler(client_, RAClientCallbacks::EventHandler);

  // Instruct rcheevos that we want hardcore mode disabled by default.
  // The user can enable it via the login dialog.
  rc_client_set_hardcore_enabled(client_, 0);

  // Start HTTP worker thread.
  http_running_ = true;
  http_thread_ = std::thread(&RAClient::RunHttpThread, this);

  // Attempt auto-login from saved token.
  LoadSavedCredentials();

  REXRA_INFO("RAClient initialized");
}

RAClient::~RAClient() {
  // Signal HTTP thread to stop.
  {
    std::lock_guard lock(queue_mutex_);
    http_running_ = false;
  }
  queue_cv_.notify_all();
  if (http_thread_.joinable()) {
    http_thread_.join();
  }

  if (client_) {
    rc_client_destroy(client_);
    client_ = nullptr;
  }

  curl_global_cleanup();
}

// ============================================================================
// Game session
// ============================================================================

void RAClient::LoadGame(const std::string& xex_path, uint32_t title_id) {
  if (!client_) {
    return;
  }
  if (!IsLoggedIn()) {
    REXRA_DEBUG("not logged in — skipping game load for {:08X}", title_id);
    return;
  }

  REXRA_INFO("loading game: {} (title_id={:08X})", xex_path, title_id);

  // rc_client_begin_identify_and_load_game will hash the file at xex_path.
  // For Xbox 360 (RC_CONSOLE_XBOX_360 = 101) rcheevos hashes the XEX.
  // RC_CONSOLE_UNKNOWN asks rcheevos to detect the console from the file/path.
  // Once rcheevos gains a dedicated RC_CONSOLE_XBOX_360 constant, switch to it.
  rc_client_begin_identify_and_load_game(
      client_, RC_CONSOLE_UNKNOWN, xex_path.c_str(), nullptr, 0,
      [](int result, const char* error_message, rc_client_t* client, void*) {
        auto* self = static_cast<RAClient*>(rc_client_get_userdata(client));
        if (result == RC_OK) {
          const rc_client_game_t* game = rc_client_get_game_info(client);
          REXRA_INFO("game loaded: {} (id={})", game ? game->title : "?",
                     game ? game->id : 0);
        } else {
          REXRA_WARN("game load failed: {} (code={})",
                     error_message ? error_message : "unknown", result);
          (void)self;
        }
      },
      nullptr);
}

void RAClient::DoFrame() {
  if (client_) {
    rc_client_do_frame(client_);
  }
}

void RAClient::Reset() {
  if (client_) {
    rc_client_reset(client_);
  }
}

// ============================================================================
// Authentication
// ============================================================================

void RAClient::LoginWithPassword(const std::string& username, const std::string& password,
                                  LoginCallback callback) {
  if (!client_) {
    if (callback) callback(false, "RAClient not initialized");
    return;
  }

  REXRA_INFO("logging in as '{}' (password)...", username);

  struct CallbackData {
    RAClient* self;
    LoginCallback cb;
  };
  auto* data = new CallbackData{this, std::move(callback)};

  rc_client_begin_login_with_password(
      client_, username.c_str(), password.c_str(),
      [](int result, const char* error_message, rc_client_t* client, void* userdata) {
        auto* d = static_cast<CallbackData*>(userdata);
        auto* self = static_cast<RAClient*>(rc_client_get_userdata(client));
        bool ok = (result == RC_OK);
        std::string msg = error_message ? error_message : "";

        if (ok) {
          const rc_client_user_t* user = rc_client_get_user_info(client);
          if (user) {
            REXRA_INFO("logged in as '{}'", user->display_name);
            self->SaveCredentials(user->username, user->token);
          }
        } else {
          REXRA_WARN("login failed: {}", msg);
        }

        if (d->cb) d->cb(ok, msg);
        delete d;
      },
      data);
}

void RAClient::LoginWithToken(const std::string& username, const std::string& token,
                               LoginCallback callback) {
  if (!client_) {
    if (callback) callback(false, "RAClient not initialized");
    return;
  }

  REXRA_INFO("logging in as '{}' (token)...", username);

  struct CallbackData {
    RAClient* self;
    LoginCallback cb;
  };
  auto* data = new CallbackData{this, std::move(callback)};

  rc_client_begin_login_with_token(
      client_, username.c_str(), token.c_str(),
      [](int result, const char* error_message, rc_client_t* client, void* userdata) {
        auto* d = static_cast<CallbackData*>(userdata);
        auto* self = static_cast<RAClient*>(rc_client_get_userdata(client));
        bool ok = (result == RC_OK);
        std::string msg = error_message ? error_message : "";

        if (ok) {
          const rc_client_user_t* user = rc_client_get_user_info(client);
          if (user) {
            REXRA_INFO("token login succeeded for '{}'", user->display_name);
            self->SaveCredentials(user->username, user->token);
          }
        } else {
          REXRA_WARN("token login failed: {}", msg);
          self->ClearCredentials();
        }

        if (d->cb) d->cb(ok, msg);
        delete d;
      },
      data);
}

void RAClient::Logout() {
  if (client_) {
    rc_client_logout(client_);
    REXRA_INFO("logged out");
  }
  ClearCredentials();
}

bool RAClient::IsLoggedIn() const {
  if (!client_) return false;
  const rc_client_user_t* user = rc_client_get_user_info(client_);
  return user != nullptr;
}

std::string RAClient::GetDisplayUsername() const {
  if (!client_) return {};
  const rc_client_user_t* user = rc_client_get_user_info(client_);
  return user ? std::string(user->display_name) : std::string();
}

// Static callbacks are defined as RAClientCallbacks members above.

// ============================================================================
// Event handling
// ============================================================================

void RAClient::OnAchievementTriggered(const void* event_ptr) {
  const auto* event = static_cast<const rc_client_event_t*>(event_ptr);
  if (!event || !event->achievement) return;

  const char* title = event->achievement->title;
  uint32_t points = event->achievement->points;
  uint32_t ra_id = event->achievement->id;

  REXRA_INFO("achievement triggered: \"{}\" (+{} pts, ra_id={})",
             title ? title : "?", points, ra_id);

  // Mirror into local AchievementManager if an entry with the same name exists.
  // (RA ids don't match Xbox achievement ids, so we match by title string.)
  if (achievement_manager_) {
    const auto& achievements = achievement_manager_->achievements();
    for (const auto& a : achievements) {
      if (!a.unlocked && title && a.label == title) {
        achievement_manager_->UnlockAchievements({a.id});
        break;
      }
    }
  }

  // Show an achievement toast in the UI thread.
  if (!emulator_) return;
  auto* app_context = emulator_->app_context();
  auto* imgui_drawer = emulator_->imgui_drawer();
  if (!app_context || !imgui_drawer) return;

  std::string label = title ? title : "Achievement Unlocked";
  uint16_t gs = static_cast<uint16_t>(points);

  // Try to find the matching local achievement icon by title match.
  std::vector<uint8_t> icon;
  if (achievement_manager_) {
    const auto& achievements = achievement_manager_->achievements();
    for (const auto& a : achievements) {
      if (title && a.label == title) {
        icon = achievement_manager_->GetAchievementIconPng(a.id);
        break;
      }
    }
  }

  app_context->CallInUIThread(
      [imgui_drawer, label = std::move(label), gs,
       icon = std::move(icon)]() mutable {
        new rex::ui::AchievementToast(imgui_drawer, std::move(label), gs,
                                      std::move(icon));
      });
}

void RAClient::OnGameCompleted(const void* /*event_ptr*/) {
  REXRA_INFO("game completed / mastery!");
  // Could show a special toast in the future.
}

// ============================================================================
// HTTP worker thread (libcurl)
// ============================================================================

namespace {

// libcurl write callback — appends received data into a std::string.
size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

}  // namespace

void RAClient::EnqueueRequest(HttpRequest req) {
  {
    std::lock_guard lock(queue_mutex_);
    request_queue_.push(std::move(req));
  }
  queue_cv_.notify_one();
}

void RAClient::RunHttpThread() {
  REXRA_DEBUG("HTTP worker thread started");

  CURL* curl = curl_easy_init();
  if (!curl) {
    REXRA_ERROR("curl_easy_init failed — HTTP worker exiting");
    return;
  }

  // Build User-Agent string using rcheevos' own version clause.
  char ua_clause[64] = {};
  rc_client_get_user_agent_clause(client_, ua_clause, sizeof(ua_clause));
  const std::string user_agent = fmt::format("rexglue-renut/1.0 {}", ua_clause);

  while (true) {
    HttpRequest req;
    {
      std::unique_lock lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { return !request_queue_.empty() || !http_running_; });
      if (!http_running_ && request_queue_.empty()) {
        break;
      }
      req = std::move(request_queue_.front());
      request_queue_.pop();
    }

    // Execute the HTTP request.
    std::string response_body;
    long http_status = 0;
    bool ok = false;

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    struct curl_slist* headers = nullptr;
    if (!req.post_data.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.post_data.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                       static_cast<long>(req.post_data.size()));
      std::string ct_header = fmt::format("Content-Type: {}", req.content_type);
      headers = curl_slist_append(headers, ct_header.c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
      ok = true;
    } else {
      REXRA_WARN("curl error: {} (url={})", curl_easy_strerror(res), req.url);
    }

    if (headers) {
      curl_slist_free_all(headers);
    }

    // Deliver result back to rcheevos — the callback MUST always be called.
    rc_api_server_response_t resp{};
    resp.http_status_code = ok ? static_cast<uint32_t>(http_status) : 0;
    resp.body = ok ? response_body.c_str() : nullptr;
    resp.body_length = ok ? static_cast<uint32_t>(response_body.size()) : 0;
    req.callback(&resp, req.callback_data);
  }

  curl_easy_cleanup(curl);
  REXRA_DEBUG("HTTP worker thread exited");
}

// ============================================================================
// Credential persistence
// ============================================================================

void RAClient::LoadSavedCredentials() {
  auto path = user_data_root_ / "ra_credentials.txt";
  std::ifstream f(path);
  if (!f.is_open()) {
    return;
  }

  std::string username, token;
  std::getline(f, username);
  std::getline(f, token);

  if (!username.empty() && !token.empty()) {
    REXRA_INFO("auto-login with saved credentials for '{}'", username);
    LoginWithToken(username, token,
                   [](bool ok, const std::string& msg) {
                     if (!ok) {
                       REXRA_WARN("auto-login failed: {}", msg);
                     }
                   });
  }
}

void RAClient::SaveCredentials(const std::string& username, const std::string& token) {
  if (user_data_root_.empty()) return;
  std::filesystem::create_directories(user_data_root_);
  auto path = user_data_root_ / "ra_credentials.txt";
  std::ofstream f(path, std::ios::trunc);
  if (f.is_open()) {
    f << username << '\n' << token << '\n';
  }
}

void RAClient::ClearCredentials() {
  auto path = user_data_root_ / "ra_credentials.txt";
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace rex::system::xam
