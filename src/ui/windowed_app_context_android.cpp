/**
 * Android WindowedAppContext implementation.
 */

#include <rex/ui/windowed_app_context_android.h>
#include <rex/logging.h>

namespace rex {
namespace ui {

AndroidWindowedAppContext::AndroidWindowedAppContext() = default;

AndroidWindowedAppContext::~AndroidWindowedAppContext() {
  // Execute remaining pending functions before destruction.
  ExecutePendingFunctionsFromUIThread();
}

void AndroidWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  pending_functions_signaled_.store(true, std::memory_order_release);
}

void AndroidWindowedAppContext::PlatformQuitFromUIThread() {
  // On Android, quitting means the Activity will finish.
  // The Java side will detect this and call finish().
}

}  // namespace ui
}  // namespace rex
