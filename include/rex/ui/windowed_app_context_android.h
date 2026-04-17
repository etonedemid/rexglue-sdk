#pragma once
/**
 * Android WindowedAppContext — runs the UI loop via JNI callbacks from Java.
 */

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <android/native_window.h>

#include <rex/ui/windowed_app_context.h>

namespace rex {
namespace ui {
class Presenter;  // forward declaration

class AndroidWindowedAppContext : public WindowedAppContext {
 public:
  AndroidWindowedAppContext();
  ~AndroidWindowedAppContext() override;

  ANativeWindow* native_window() const { return native_window_; }
  void SetNativeWindow(ANativeWindow* window) { native_window_ = window; }

  // Called from the Android render thread to pump pending UI functions.
  void PumpPendingFunctions() { ExecutePendingFunctionsFromUIThread(); }

  // Set by ReXApp::OnInitialize() so that nativePumpEvents can drive the
  // presenter's UI-thread paint loop every Vsync without needing to access
  // the protected Window::presenter() field from the JNI entry point.
  void SetPresenter(Presenter* presenter) { presenter_ = presenter; }
  Presenter* GetPresenter() const { return presenter_; }

 protected:
  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

 private:
  ANativeWindow* native_window_ = nullptr;
  Presenter* presenter_ = nullptr;
  std::atomic<bool> pending_functions_signaled_{false};
};

}  // namespace ui
}  // namespace rex
