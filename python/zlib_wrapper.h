/* Wrapper header to prevent fdopen macro redefinition on macOS */
#ifndef ZLIB_WRAPPER_H
#define ZLIB_WRAPPER_H

/* On macOS, zutil.h defines fdopen as a macro to NULL when Z_SOLO is not defined.
 * This conflicts with system headers that declare fdopen as a function.
 * We work around this by pre-defining fdopen to itself, which causes the
 * #ifndef fdopen check in zutil.h to skip the problematic macro definition.
 */
#if defined(__APPLE__) || defined(MACOS) || defined(TARGET_OS_MAC)
#define fdopen fdopen
#endif

#endif /* ZLIB_WRAPPER_H */
