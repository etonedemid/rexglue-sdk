#pragma once
/**
 * Android surface implementation — wraps ANativeWindow* for Vulkan surface creation.
 */

#include <rex/ui/surface.h>

#include <android/native_window.h>

namespace rex {
namespace ui {

class AndroidNativeWindowSurface final : public Surface {
 public:
  explicit AndroidNativeWindowSurface(ANativeWindow* window, uint32_t width, uint32_t height)
      : window_(window), width_(width), height_(height) {}

  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

  void UpdateSize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
  }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    if (!window_) return false;
    width_out = width_;
    height_out = height_;
    return width_ > 0 && height_ > 0;
  }

 private:
  ANativeWindow* window_;
  uint32_t width_;
  uint32_t height_;
};

}  // namespace ui
}  // namespace rex
