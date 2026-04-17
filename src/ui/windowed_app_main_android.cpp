/**
 * @file        windowed_app_main_android.cpp
 * @brief       Android JNI entry point for ReXGlue windowed apps.
 *
 * Mirrors windowed_app_main_posix.cpp but uses JNI callbacks from a Java
 * Activity instead of a GTK event loop.
 *
 * The Java activity (RenutActivity) calls these native methods:
 *   nativeInit        — first surface available, initialise everything
 *   nativeSurfaceChanged  — surface resized
 *   nativeSurfaceDestroyed — surface lost (pause rendering)
 *   nativePumpEvents  — called per-frame to drain UI-thread work
 *   nativeShutdown    — Activity is finishing
 */

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_android.h>

#include <spdlog/sinks/callback_sink.h>

#define LOG_TAG "ReXGlue"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Global state — one app instance per process (matches desktop model)
// ---------------------------------------------------------------------------
static JavaVM* g_jvm = nullptr;
static std::unique_ptr<rex::ui::AndroidWindowedAppContext> g_app_context;
static std::unique_ptr<rex::ui::WindowedApp> g_app;
static ANativeWindow* g_native_window = nullptr;
static bool g_initialised = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Helper: convert jstring to std::string
static std::string JstringToString(JNIEnv* env, jstring jstr) {
  if (!jstr) return {};
  const char* chars = env->GetStringUTFChars(jstr, nullptr);
  std::string result(chars);
  env->ReleaseStringUTFChars(jstr, chars);
  return result;
}

// ---------------------------------------------------------------------------
// nativeInit — called from Java when the first ANativeWindow is available.
//
// Parameters from Java:
//   surface     — the Android Surface (wraps ANativeWindow)
//   appId       — identifier registered via REX_DEFINE_APP (e.g. "renut")
//   gameDir     — user-selected game assets directory
//   filesDir    — Context.getFilesDir(), writable private app directory
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_com_rexglue_renut_RenutActivity_nativeInit(
    JNIEnv* env, jobject /*thiz*/, jobject surface,
    jstring app_id_jstr, jstring data_dir_jstr, jstring files_dir_jstr) {

  if (g_initialised) {
    LOGI("nativeInit: already initialised");
    return JNI_TRUE;
  }

  // Cache the JavaVM (SDL3 owns JNI_OnLoad, so we obtain it here)
  env->GetJavaVM(&g_jvm);

  std::string app_id = JstringToString(env, app_id_jstr);
  std::string data_dir = JstringToString(env, data_dir_jstr);
  std::string files_dir = JstringToString(env, files_dir_jstr);

  LOGI("nativeInit: app=%s game=%s files=%s", app_id.c_str(), data_dir.c_str(), files_dir.c_str());

  // Obtain ANativeWindow
  g_native_window = ANativeWindow_fromSurface(env, surface);
  if (!g_native_window) {
    LOGE("nativeInit: ANativeWindow_fromSurface returned null");
    return JNI_FALSE;
  }

  // Platform-specific subsystem init
  rex::memory::AndroidInitialize();
  rex::filesystem::AndroidInitialize();

  // Initialise cvar system with no command-line args
  char prog[] = "renut";
  char* argv[] = {prog, nullptr};
  rex::cvar::Init(1, argv);
  rex::cvar::ApplyEnvironment();

  // On Android, $HOME is unset or points to /data (no write permission).
  // Override user_data_root to the app's private writable directory so that
  // ReXApp::OnInitialize() doesn't try to create_directories under /data/.
  if (!files_dir.empty() && !app_id.empty()) {
    rex::cvar::SetFlagByName("user_data_root", files_dir + "/" + app_id);
    LOGI("nativeInit: user_data_root = %s/%s", files_dir.c_str(), app_id.c_str());
  }

  // Create the app context and hand it the native window
  g_app_context = std::make_unique<rex::ui::AndroidWindowedAppContext>();
  g_app_context->SetNativeWindow(g_native_window);

  // Look up the app creator registered by REX_DEFINE_APP
  auto creator = rex::ui::WindowedApp::GetCreator(app_id);
  if (!creator) {
    LOGE("nativeInit: no creator for '%s'", app_id.c_str());
    return JNI_FALSE;
  }

  // Create the app
  g_app = creator(*g_app_context);

  // Set game_directory positional arg to the user-selected assets path
  {
    std::map<std::string, std::string> parsed;
    if (!data_dir.empty()) {
      parsed["game_directory"] = data_dir;
    }
    g_app->SetParsedArguments(std::move(parsed));
  }

  // Initialise logging to logcat (no file)
  rex::InitLogging(nullptr);

  // Forward all REXLOG messages to Android logcat so they appear in adb logcat.
  {
    auto logcat_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [](const spdlog::details::log_msg& msg) {
          android_LogPriority prio;
          switch (msg.level) {
            case spdlog::level::trace:
            case spdlog::level::debug:     prio = ANDROID_LOG_DEBUG; break;
            case spdlog::level::info:      prio = ANDROID_LOG_INFO;  break;
            case spdlog::level::warn:      prio = ANDROID_LOG_WARN;  break;
            case spdlog::level::err:       prio = ANDROID_LOG_ERROR; break;
            case spdlog::level::critical:  prio = ANDROID_LOG_FATAL; break;
            default:                       prio = ANDROID_LOG_DEBUG; break;
          }
          std::string text(msg.payload.begin(), msg.payload.end());
          __android_log_print(prio, "ReXGlue", "%s", text.c_str());
        });
    logcat_sink->set_level(spdlog::level::trace);
    rex::AddSink(logcat_sink);
  }

  // SDL3 normally has SDL_SetMainReady() called from SDLActivity.nativeSetupJNI()
  // via checkJNIReady(). Since we bypass SDLActivity entirely, call it here so
  // SDL_InitSubSystem(SDL_INIT_AUDIO) doesn't fail with "not initialized" and so
  // that the audio driver can open a device normally.
  {
    extern void SDL_SetMainReady(void);
    SDL_SetMainReady();
  }

  // Run app initialisation (creates Runtime, Window, graphics, launches module)
  if (!g_app->OnInitialize()) {
    LOGE("nativeInit: OnInitialize failed");
    g_app.reset();
    g_app_context.reset();
    ANativeWindow_release(g_native_window);
    g_native_window = nullptr;
    return JNI_FALSE;
  }

  g_initialised = true;
  LOGI("nativeInit: success");
  return JNI_TRUE;
}

// ---------------------------------------------------------------------------
// nativeSurfaceChanged — surface resized (or format changed)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_rexglue_renut_RenutActivity_nativeSurfaceChanged(
    JNIEnv* /*env*/, jobject /*thiz*/, jint width, jint height) {

  if (!g_initialised) return;
  LOGI("nativeSurfaceChanged: %dx%d", width, height);
  // The window object is internal to ReXApp; surface resize is handled
  // through the presenter's swapchain recreation on the next frame.
}

// ---------------------------------------------------------------------------
// nativeSurfaceDestroyed — surface lost; must stop rendering
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_rexglue_renut_RenutActivity_nativeSurfaceDestroyed(
    JNIEnv* /*env*/, jobject /*thiz*/) {

  if (!g_initialised) return;
  LOGI("nativeSurfaceDestroyed");
  // Release our reference; the presenter will detect the invalid surface.
  if (g_native_window) {
    ANativeWindow_release(g_native_window);
    g_native_window = nullptr;
  }
  if (g_app_context) {
    g_app_context->SetNativeWindow(nullptr);
  }
}

// ---------------------------------------------------------------------------
// nativePumpEvents — called per-frame from Choreographer / render thread
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_rexglue_renut_RenutActivity_nativePumpEvents(
    JNIEnv* /*env*/, jobject /*thiz*/) {

  if (!g_initialised || !g_app_context) return;
  g_app_context->PumpPendingFunctions();

  // Drive the presenter's UI-thread paint loop every Vsync tick.
  // On desktop, the OS sends paint events; on Android we pump explicitly.
  if (auto* presenter = g_app_context->GetPresenter()) {
    presenter->PaintFromUIThread(false);
  }
}

// ---------------------------------------------------------------------------
// nativeShutdown — Activity is finishing, tear everything down
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_rexglue_renut_RenutActivity_nativeShutdown(
    JNIEnv* /*env*/, jobject /*thiz*/) {

  if (!g_initialised) return;
  LOGI("nativeShutdown");

  if (g_app_context) {
    g_app_context->SetPresenter(nullptr);
  }

  if (g_app) {
    g_app->InvokeOnDestroy();
    g_app.reset();
  }
  if (g_app_context) {
    g_app_context.reset();
  }
  if (g_native_window) {
    ANativeWindow_release(g_native_window);
    g_native_window = nullptr;
  }

  rex::filesystem::AndroidShutdown();
  rex::memory::AndroidShutdown();
  rex::ShutdownLogging();

  g_initialised = false;
  LOGI("nativeShutdown: complete");
}
