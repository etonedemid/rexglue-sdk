/**
 * @file        fiber_android.cpp
 * @brief       Android ARM64 fiber implementation using raw context switch.
 *
 * Android bionic does not provide getcontext/makecontext/swapcontext.
 * This implementation uses a minimal AArch64 cooperative context switch
 * that saves/restores only the callee-saved registers (per AAPCS64).
 *
 * Context layout (168 bytes, stored in Fiber's context_ buffer):
 *   [0..7]     x19       [8..15]    x20
 *   [16..23]   x21       [24..31]   x22
 *   [32..39]   x23       [40..47]   x24
 *   [48..55]   x25       [56..63]   x26
 *   [64..71]   x27       [72..79]   x28
 *   [80..87]   x29 (FP)  [88..95]   x30 (LR)
 *   [96..103]  SP
 *   [104..111] d8        [112..119] d9
 *   [120..127] d10       [128..135] d11
 *   [136..143] d12       [144..151] d13
 *   [152..159] d14       [160..167] d15
 */

#include <rex/platform.h>
#if REX_PLATFORM_ANDROID

#include <rex/thread/fiber.h>

#include <cassert>
#include <cstring>

// The raw context switch. Saves callee-saved registers to *from_ctx,
// restores from *to_ctx, and returns (via the restored LR) into the
// target fiber.
extern "C" void fiber_switch_aarch64(void* from_ctx, void* to_ctx);

// Embedded assembly for the context switch (AArch64).
__asm__(
    ".text\n"
    ".globl fiber_switch_aarch64\n"
    ".type fiber_switch_aarch64, %function\n"
    "fiber_switch_aarch64:\n"
    // Save callee-saved GPRs to from_ctx (x0)
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x9, sp\n"
    "  str x9, [x0, #96]\n"
    // Save callee-saved SIMD regs
    "  stp d8, d9,   [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    // Restore from to_ctx (x1)
    "  ldp x19, x20, [x1, #0]\n"
    "  ldp x21, x22, [x1, #16]\n"
    "  ldp x23, x24, [x1, #32]\n"
    "  ldp x25, x26, [x1, #48]\n"
    "  ldp x27, x28, [x1, #64]\n"
    "  ldp x29, x30, [x1, #80]\n"
    "  ldr x9, [x1, #96]\n"
    "  mov sp, x9\n"
    "  ldp d8, d9,   [x1, #104]\n"
    "  ldp d10, d11, [x1, #120]\n"
    "  ldp d12, d13, [x1, #136]\n"
    "  ldp d14, d15, [x1, #152]\n"
    "  ret\n"
    ".size fiber_switch_aarch64, .-fiber_switch_aarch64\n"
);

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  // Context will be saved on the first SwitchTo away from this fiber.
  std::memset(&f->context_, 0, sizeof(f->context_));
  f->is_thread_fiber_ = true;
  tls_current_ = f;
  return f;
}

// Trampoline: called when a new fiber runs for the first time.
// At this point tls_current_ has been set by SwitchTo.
/*static*/ void Fiber::Trampoline() {
  Fiber* f = tls_current_;
  f->entry_(f->arg_);
  // If the entry function returns, the fiber is dead.
  // In practice, guest code calls SwitchTo before returning.
  assert(false && "Fiber entry function returned");
  __builtin_unreachable();
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  auto* f = new Fiber();
  f->entry_ = entry;
  f->arg_ = arg;
  f->stack_.resize(stack_size);

  // Set up the initial context so that the first fiber_switch_aarch64 into
  // this fiber will land in the trampoline on the new stack.
  std::memset(&f->context_, 0, sizeof(f->context_));

  // Stack grows downward; top is the end of the allocation, 16-byte aligned.
  uintptr_t sp = reinterpret_cast<uintptr_t>(f->stack_.data() + f->stack_.size());
  sp &= ~static_cast<uintptr_t>(15);

  // Store SP (offset 96) and LR (offset 88) in the context buffer.
  auto* ctx = reinterpret_cast<uint64_t*>(&f->context_);
  ctx[12] = sp;                                            // SP at offset 96
  // Point LR to the trampoline — cast the member function pointer.
  // Trampoline is a static member, so taking its address is well-defined.
  ctx[11] = reinterpret_cast<uint64_t>(&Fiber::Trampoline); // LR at offset 88

  return f;
}

void Fiber::SwitchTo(Fiber* target) {
  Fiber* from = tls_current_;
  tls_current_ = target;
  fiber_switch_aarch64(&from->context_, &target->context_);
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Destroy called on the currently running fiber");
  }
  delete this;
}

}  // namespace rex::thread

#endif  // REX_PLATFORM_ANDROID
