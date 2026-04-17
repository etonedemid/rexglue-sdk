/**
 * Android Window implementation — wraps ANativeWindow from Java SurfaceView.
 *
 * Unlike GTK or Win32 windows, on Android the window (Activity + SurfaceView)
 * is managed entirely by Java. This class acts as a thin bridge.
 */

#include <memory>

#include <android/native_window.h>

#include <rex/logging.h>
#include <rex/ui/surface_android.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_android.h>

namespace rex {
namespace ui {

class AndroidWindow : public Window {
 public:
  AndroidWindow(WindowedAppContext& app_context, const std::string_view title,
                uint32_t desired_logical_width, uint32_t desired_logical_height)
      : Window(app_context, title, desired_logical_width, desired_logical_height) {}

  ~AndroidWindow() override { EnterDestructor(); }

  void SetNativeWindow(ANativeWindow* window) {
    native_window_ = window;
    if (window) {
      uint32_t w = static_cast<uint32_t>(ANativeWindow_getWidth(window));
      uint32_t h = static_cast<uint32_t>(ANativeWindow_getHeight(window));
      surface_width_ = w;
      surface_height_ = h;
    }
  }

  void NotifySurfaceChanged(uint32_t width, uint32_t height) {
    surface_width_ = width;
    surface_height_ = height;
    WindowDestructionReceiver destruction_receiver(this);
    OnActualSizeUpdate(width, height, destruction_receiver);
    if (!destruction_receiver.IsWindowDestroyed()) {
      OnSurfaceChanged(true);
    }
  }

  void NotifySurfaceDestroyed() {
    native_window_ = nullptr;
    OnSurfaceChanged(false);
  }

  void* GetNativeWindowHandle() const override { return native_window_; }

 protected:
  bool OpenImpl() override {
    // Retrieve the native window from the AndroidWindowedAppContext if not
    // already set. The JNI bridge stores it there before app initialization.
    if (!native_window_) {
      auto& android_ctx =
          static_cast<AndroidWindowedAppContext&>(app_context());
      if (android_ctx.native_window()) {
        SetNativeWindow(android_ctx.native_window());
      }
    }

    if (native_window_) {
      WindowDestructionReceiver destruction_receiver(this);
      OnActualSizeUpdate(surface_width_, surface_height_, destruction_receiver);
      if (!destruction_receiver.IsWindowDestroyed()) {
        OnFocusUpdate(true, destruction_receiver);
      }
    }
    return true;
  }

  void RequestCloseImpl() override {
    // On Android, the Java Activity controls closing.
    WindowDestructionReceiver destruction_receiver(this);
    OnBeforeClose(destruction_receiver);
  }

  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override {
    if (!native_window_) return nullptr;
    if (!(allowed_types & Surface::kTypeFlag_AndroidNativeWindow)) return nullptr;
    return std::make_unique<AndroidNativeWindowSurface>(native_window_, surface_width_,
                                                        surface_height_);
  }

  void RequestPaintImpl() override {
    // On Android, rendering is driven by the main loop, not paint requests.
  }

 private:
  ANativeWindow* native_window_ = nullptr;
  uint32_t surface_width_ = 0;
  uint32_t surface_height_ = 0;
};

// Factory for Window::Create on Android
std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<AndroidWindow>(app_context, title, desired_logical_width,
                                         desired_logical_height);
}

}  // namespace ui
}  // namespace rex
