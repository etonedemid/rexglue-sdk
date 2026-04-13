#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>

#include <rex/system/kernel_state.h>
#include <rex/system/xam/achievement_manager.h>
#include <rex/system/xam/app_manager.h>
#include <rex/system/xam/ra_client.h>

namespace rex {
namespace kernel {
namespace xam {
namespace apps {

class XgiApp : public system::xam::App {
 public:
  explicit XgiApp(system::KernelState* kernel_state);

  X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length) override;

  system::xam::AchievementManager* achievement_manager() const {
    return achievement_manager_.get();
  }
  system::xam::RAClient* ra_client() const { return ra_client_.get(); }

 private:
  std::unique_ptr<system::xam::AchievementManager> achievement_manager_;
  std::unique_ptr<system::xam::RAClient> ra_client_;
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace rex
