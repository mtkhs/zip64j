/*
 * debug_log.h - Opt-in diagnostic log shared by zip64j.dll and unzip64.dll.
 *
 * Activation: the log is enabled if "%TEMP%\zip64j_debug.on" exists at
 * first-call time. Output goes to "%TEMP%\zip64j_debug.log" (appended).
 * When the sentinel file is absent, every call is a near-zero no-op.
 *
 * Both DLLs append to the same file with independent critical sections, so
 * each line carries a tag (set per target via ZIP64J_LOG_TAG) identifying
 * which DLL emitted it.
 *
 * Safe to call from any of the exported APIs; serialized by an internal
 * critical section.
 */

#ifndef ZIP64J_DEBUG_LOG_H
#define ZIP64J_DEBUG_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

int  zip64j_log_enabled(void);
void zip64j_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZIP64J_DEBUG_LOG_H */
