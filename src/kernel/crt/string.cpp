/**
 * @file        kernel/crt/string.cpp
 *
 * @brief       Native string operation hooks -- replaces recompiled PPC
 *              implementations of strncmp, strchr, lstrlenA, etc.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */
#include <cstring>

#include <rex/platform.h>

#if !REX_PLATFORM_WIN32
#include <strings.h>
#endif

#include <rex/ppc/function.h>

namespace rex::kernel::crt {

// ---------------------------------------------------------------------------
// C string operations
// ---------------------------------------------------------------------------

static int native_strncmp(const char* s1, const char* s2, size_t n) {
  return std::strncmp(s1, s2, n);
}

static char* native_strncpy(char* dst, const char* src, size_t n) {
  return std::strncpy(dst, src, n);
}

static char* native_strchr(const char* s, int c) {
  return const_cast<char*>(std::strchr(s, c));
}

static char* native_strstr(const char* haystack, const char* needle) {
  return const_cast<char*>(std::strstr(haystack, needle));
}

static char* native_strrchr(const char* s, int c) {
  return const_cast<char*>(std::strrchr(s, c));
}

static char* native_strtok(char* s, const char* delim) {
  return std::strtok(s, delim);
}

static int native_stricmp(const char* s1, const char* s2) {
#if REX_PLATFORM_WIN32
  return _stricmp(s1, s2);
#else
  return strcasecmp(s1, s2);
#endif
}

static int native_strcpy_s(char* dst, size_t dstsz, const char* src) {
  if (!dst || !src || dstsz == 0)
    return 22;  // EINVAL
#if REX_PLATFORM_WIN32
  return strcpy_s(dst, dstsz, src);
#else
  const size_t src_len = std::strlen(src);
  if (src_len + 1 > dstsz) {
    dst[0] = '\0';
    return 34;  // ERANGE
  }
  std::memcpy(dst, src, src_len + 1);
  return 0;
#endif
}

// ---------------------------------------------------------------------------
// Win32 string functions (lstr*)
// ---------------------------------------------------------------------------

static int native_lstrlenA(const char* s) {
  return s ? static_cast<int>(std::strlen(s)) : 0;
}

static char* native_lstrcpyA(char* dst, const char* src) {
  return std::strcpy(dst, src);
}

static char* native_lstrcpynA(char* dst, const char* src, int maxlen) {
  if (maxlen <= 0)
    return dst;
  std::strncpy(dst, src, maxlen - 1);
  dst[maxlen - 1] = '\0';
  return dst;
}

static char* native_lstrcatA(char* dst, const char* src) {
  return std::strcat(dst, src);
}

static int native_lstrcmpiA(const char* s1, const char* s2) {
#if REX_PLATFORM_WIN32
  return _stricmp(s1, s2);
#else
  return strcasecmp(s1, s2);
#endif
}

// ---------------------------------------------------------------------------
// C UTF-16 Widestring functions (int16_t*)
// ---------------------------------------------------------------------------

static unsigned int native_wcslen(const int16_t* str) {
  const int16_t* last = str;
  while (*last++)
    ;
  return last - str - 1;
}

static int native_wcscmp(const int16_t* lhs, const int16_t* rhs) {
  while (*lhs && (*lhs == *rhs)) {
    lhs++;
    rhs++;
  }
  return (int)(*lhs) - (int)(*rhs);
}

static int native_wcsncmp(const int16_t* lhs, const int16_t* rhs, int count) {
  for (; count > 0; count--, lhs++, rhs++) {
    if (*lhs != *rhs)
      return (int)(*lhs) - (int)(*rhs);

    if (*lhs == L'\0')
      return 0;
  }

  return 0;
}

static int native_wcscoll(const int16_t* lhs, const int16_t* rhs) {
  if (lhs && rhs)
    return native_wcscmp(lhs, rhs);
  return 22;  // EINVAL
}

static int16_t* native_wcschr(const int16_t* str, int16_t ch) {
  while (*str && *str != ch)
    str++;

  return (*str == ch) ? (int16_t*)str : 0;
}

static int16_t* native_wcsrchr(const int16_t* str, int16_t ch) {
  const int16_t* res = 0;

  for (; *str; str++)
    if (*str == ch)
      res = str;

  return (int16_t*)(ch ? res : str);
}

static int16_t* native_wcscpy(const int16_t* src, int16_t* dst) {
  int16_t* d = dst;

  while ((*d++ = *src++))
    ;

  return dst;
}

static int16_t* native_wcsncpy(int16_t* dest, const int16_t* src, int count) {
  int16_t* d = dest;
  while (count-- && (*d++ = *src++))
    ;
  while (count--)
    *d++ = 0;
  return dest;
}

static int native_wcsncpy_s(int16_t* dst, size_t dstsz, const int16_t* src,
                             size_t count) {
  if (!dst || !src || dstsz == 0)
    return 22;  // EINVAL

  size_t i = 0;

  for (; i < count && i + 1 < dstsz && src[i]; i++)
    dst[i] = src[i];

  if (i < dstsz)
    dst[i] = 0;
  else
    dst[dstsz - 1] = 0;

  if (i < count && src[i] != 0)
    return 34;  // ERANGE

  return 0;
}

static int16_t* native_wcsstr(const int16_t* dest, const int16_t* src) {
  if (!*src)
    return (int16_t*)dest;

  for (; *dest; dest++) {
    const int16_t* d = dest;
    const int16_t* s = src;

    while (*d && *s && *d == *s) {
      d++;
      s++;
    }

    if (!*s)
      return (int16_t*)dest;
  }

  return 0;
}

}  // namespace rex::kernel::crt

REXCRT_EXPORT(rexcrt_strncmp, rex::kernel::crt::native_strncmp)
REXCRT_EXPORT(rexcrt_strncpy, rex::kernel::crt::native_strncpy)
REXCRT_EXPORT(rexcrt_strchr, rex::kernel::crt::native_strchr)
REXCRT_EXPORT(rexcrt_strstr, rex::kernel::crt::native_strstr)
REXCRT_EXPORT(rexcrt_strrchr, rex::kernel::crt::native_strrchr)
REXCRT_EXPORT(rexcrt_strtok, rex::kernel::crt::native_strtok)
REXCRT_EXPORT(rexcrt__stricmp, rex::kernel::crt::native_stricmp)
REXCRT_EXPORT(rexcrt_strcpy_s, rex::kernel::crt::native_strcpy_s)
REXCRT_EXPORT(rexcrt_lstrlenA, rex::kernel::crt::native_lstrlenA)
REXCRT_EXPORT(rexcrt_lstrcpyA, rex::kernel::crt::native_lstrcpyA)
REXCRT_EXPORT(rexcrt_lstrcpynA, rex::kernel::crt::native_lstrcpynA)
REXCRT_EXPORT(rexcrt_lstrcatA, rex::kernel::crt::native_lstrcatA)
REXCRT_EXPORT(rexcrt_lstrcmpiA, rex::kernel::crt::native_lstrcmpiA)
REXCRT_EXPORT(rexcrt_wcslen, rex::kernel::crt::native_wcslen)
REXCRT_EXPORT(rexcrt_wcscmp, rex::kernel::crt::native_wcscmp)
REXCRT_EXPORT(rexcrt_wcsncmp, rex::kernel::crt::native_wcsncmp)
REXCRT_EXPORT(rexcrt_wcscoll, rex::kernel::crt::native_wcscoll)
REXCRT_EXPORT(rexcrt_wcschr, rex::kernel::crt::native_wcschr)
REXCRT_EXPORT(rexcrt_wcsrchr, rex::kernel::crt::native_wcsrchr)
REXCRT_EXPORT(rexcrt_wcscpy, rex::kernel::crt::native_wcscpy)
REXCRT_EXPORT(rexcrt_wcsncpy, rex::kernel::crt::native_wcsncpy)
REXCRT_EXPORT(rexcrt_wcsncpy_s, rex::kernel::crt::native_wcsncpy_s)
REXCRT_EXPORT(rexcrt_wcsstr, rex::kernel::crt::native_wcsstr)
