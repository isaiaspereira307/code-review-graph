#ifndef CRG_C_CORE_EXPORT_H
#define CRG_C_CORE_EXPORT_H

/* Symbol visibility for the shared library consumed through ctypes. */

#if defined(_WIN32)
#  if defined(CRG_C_CORE_BUILD)
#    define CRG_API __declspec(dllexport)
#  else
#    define CRG_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define CRG_API __attribute__((visibility("default")))
#else
#  define CRG_API
#endif

#endif /* CRG_C_CORE_EXPORT_H */
