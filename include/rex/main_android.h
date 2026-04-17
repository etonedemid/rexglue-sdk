/**
 * @file        main_android.h
 * @brief       Android-specific helpers — API level detection.
 */

#pragma once

#include <rex/platform.h>

#if REX_PLATFORM_ANDROID

#include <android/api-level.h>

namespace rex {

// Returns the runtime Android API level (e.g. 30, 34).
// This is the device's API level, not the compile-time target.
inline int GetAndroidApiLevel() {
  return android_get_device_api_level();
}

}  // namespace rex

#endif  // REX_PLATFORM_ANDROID
