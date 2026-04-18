/**
 * @file        glibc_compat.cpp
 * @brief       Pin dynamic glibc symbol versions to a widely-supported minimum
 *
 * When the project is compiled on a system with glibc 2.32+ the linker
 * naturally selects the newest available versioned symbol for every imported
 * function.  This file uses .symver assembler directives to redirect those
 * references to older versioned aliases that have been stable since glibc 2.17
 * (Ubuntu 14.04 / CentOS 7 / Amazon Linux 2).
 *
 * Rules:
 *   - Only functions that exist in BOTH the old and the new version table are
 *     redirected here.  Functions that were genuinely new (e.g. strlcpy@2.38,
 *     pthread_cond_clockwait@2.30) are handled separately.
 *   - The file must be compiled as C++ so that it is pulled into the link even
 *     when LTO is active.
 *   - These directives affect only *this* translation unit's references; they
 *     work because the GNU linker resolves all TU-level .symver directives
 *     before emitting the final dynamic symbol table.
 */

#if defined(__linux__) && !defined(__ANDROID__)

// ── Math — single-precision floats ──────────────────────────────────────────
// glibc 2.31+ exposed versioned float math symbols; binaries built on 2.43
// reference GLIBC_2.43 variants.  The GLIBC_2.2.5 aliases still exist.
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver asinf,asinf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver ceilf,ceilf@GLIBC_2.2.5");
__asm__(".symver cosf,cosf@GLIBC_2.2.5");
__asm__(".symver expf,expf@GLIBC_2.2.5");
__asm__(".symver floorf,floorf@GLIBC_2.2.5");
__asm__(".symver fmodf,fmodf@GLIBC_2.2.5");
__asm__(".symver log2f,log2f@GLIBC_2.2.5");
__asm__(".symver log10f,log10f@GLIBC_2.2.5");
__asm__(".symver logf,logf@GLIBC_2.2.5");
__asm__(".symver powf,powf@GLIBC_2.2.5");
__asm__(".symver sinf,sinf@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
__asm__(".symver tanf,tanf@GLIBC_2.2.5");

// ── Math — double-precision ──────────────────────────────────────────────────
__asm__(".symver fmod,fmod@GLIBC_2.2.5");
__asm__(".symver hypot,hypot@GLIBC_2.2.5");
__asm__(".symver pow,pow@GLIBC_2.2.5");

// ── Dynamic linker ────────────────────────────────────────────────────────────
// In glibc 2.34 libdl.so was merged into libc.so; the GLIBC_2.2.5 aliases
// remain available for backwards compat.
__asm__(".symver dlclose,dlclose@GLIBC_2.2.5");
__asm__(".symver dlerror,dlerror@GLIBC_2.2.5");
__asm__(".symver dlopen,dlopen@GLIBC_2.2.5");
__asm__(".symver dlsym,dlsym@GLIBC_2.2.5");

// ── pthreads ──────────────────────────────────────────────────────────────────
// libpthread.so was merged into libc.so in glibc 2.34; the GLIBC_2.2.5 and
// GLIBC_2.3.2 aliases remain available on 2.34+ for backwards compat.
__asm__(".symver pthread_attr_setstacksize,pthread_attr_setstacksize@GLIBC_2.2.5");
__asm__(".symver pthread_cancel,pthread_cancel@GLIBC_2.2.5");
__asm__(".symver pthread_create,pthread_create@GLIBC_2.2.5");
__asm__(".symver pthread_detach,pthread_detach@GLIBC_2.2.5");
__asm__(".symver pthread_getspecific,pthread_getspecific@GLIBC_2.2.5");
__asm__(".symver pthread_join,pthread_join@GLIBC_2.2.5");
__asm__(".symver pthread_key_create,pthread_key_create@GLIBC_2.2.5");
__asm__(".symver pthread_key_delete,pthread_key_delete@GLIBC_2.2.5");
__asm__(".symver pthread_kill,pthread_kill@GLIBC_2.2.5");
__asm__(".symver pthread_mutexattr_destroy,pthread_mutexattr_destroy@GLIBC_2.2.5");
__asm__(".symver pthread_mutexattr_init,pthread_mutexattr_init@GLIBC_2.2.5");
__asm__(".symver pthread_mutexattr_settype,pthread_mutexattr_settype@GLIBC_2.3.2");
__asm__(".symver pthread_mutex_consistent,pthread_mutex_consistent@GLIBC_2.12");
__asm__(".symver pthread_mutex_trylock,pthread_mutex_trylock@GLIBC_2.2.5");
__asm__(".symver pthread_once,pthread_once@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_destroy,pthread_rwlock_destroy@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_init,pthread_rwlock_init@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_rdlock,pthread_rwlock_rdlock@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_tryrdlock,pthread_rwlock_tryrdlock@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_trywrlock,pthread_rwlock_trywrlock@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_unlock,pthread_rwlock_unlock@GLIBC_2.2.5");
__asm__(".symver pthread_rwlock_wrlock,pthread_rwlock_wrlock@GLIBC_2.2.5");
__asm__(".symver pthread_setspecific,pthread_setspecific@GLIBC_2.2.5");
// setaffinity/sigmask were added in GLIBC_2.3.4 / GLIBC_2.3.2 respectively
__asm__(".symver pthread_getaffinity_np,pthread_getaffinity_np@GLIBC_2.3.4");
__asm__(".symver pthread_setaffinity_np,pthread_setaffinity_np@GLIBC_2.3.4");
__asm__(".symver pthread_setname_np,pthread_setname_np@GLIBC_2.12");
__asm__(".symver pthread_sigmask,pthread_sigmask@GLIBC_2.2.5");
__asm__(".symver pthread_sigqueue,pthread_sigqueue@GLIBC_2.11");
__asm__(".symver pthread_mutexattr_setrobust,pthread_mutexattr_setrobust@GLIBC_2.12");
// pthread_cond_clockwait was genuinely new in GLIBC_2.30 — no older alias.

// ── POSIX cancellation helpers (pulled into libc.so in 2.34) ─────────────────
__asm__(".symver __pthread_register_cancel,__pthread_register_cancel@GLIBC_2.2.5");
__asm__(".symver __pthread_unregister_cancel,__pthread_unregister_cancel@GLIBC_2.2.5");
__asm__(".symver __pthread_unwind_next,__pthread_unwind_next@GLIBC_2.2.5");

// ── POSIX semaphores ─────────────────────────────────────────────────────────
__asm__(".symver sem_destroy,sem_destroy@GLIBC_2.2.5");
__asm__(".symver sem_getvalue,sem_getvalue@GLIBC_2.2.5");
__asm__(".symver sem_init,sem_init@GLIBC_2.2.5");
__asm__(".symver sem_post,sem_post@GLIBC_2.2.5");
__asm__(".symver sem_timedwait,sem_timedwait@GLIBC_2.2.5");
__asm__(".symver sem_trywait,sem_trywait@GLIBC_2.2.5");
__asm__(".symver sem_wait,sem_wait@GLIBC_2.2.5");

// ── POSIX shared memory ──────────────────────────────────────────────────────
__asm__(".symver shm_open,shm_open@GLIBC_2.2.5");
__asm__(".symver shm_unlink,shm_unlink@GLIBC_2.2.5");

#endif  // __linux__ && !__ANDROID__
