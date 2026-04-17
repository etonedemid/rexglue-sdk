/**
 * @file        filesystem_android.cpp
 * @brief       Android-specific filesystem helpers.
 *
 * Provides stub implementations for Android content-URI and lifecycle
 * functions declared in <rex/filesystem.h>.
 */

#include <rex/platform.h>

#if REX_PLATFORM_ANDROID

#include <rex/filesystem.h>
#include <android/log.h>

#define LOG_TAG "ReXGlue"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace rex {
namespace filesystem {

void AndroidInitialize() {
  // Nothing required at the moment — Android file I/O uses standard POSIX.
}

void AndroidShutdown() {
  // Nothing to tear down.
}

bool IsAndroidContentUri(const std::string_view source) {
  return source.starts_with("content://");
}

int OpenAndroidContentFileDescriptor(const std::string_view /*uri*/,
                                     const char* /*mode*/) {
  LOGW("OpenAndroidContentFileDescriptor: content URIs not yet supported");
  return -1;
}

}  // namespace filesystem
}  // namespace rex

#endif  // REX_PLATFORM_ANDROID
