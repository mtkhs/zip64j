/*
 * cmdline.h - Command-line tokenizer.
 *
 * Splits a whitespace-separated command line (CP932-safe, quote-aware) into
 * a NULL-terminated array of char*. Response files (@name) are expanded
 * transparently.
 */

#ifndef ZIP64J_CMDLINE_H
#define ZIP64J_CMDLINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a malloc'd NULL-terminated array of char*. The returned block is
 * one allocation — free() on the returned pointer releases both the outer
 * array and every string it points into (strarraydup packs them together).
 * Returns NULL on allocation failure or malformed response file.
 */
char **zip64j_split_cmdline_with_response(const char *cmdline);

/* Length of a NULL-terminated pointer array. */
int zip64j_ptrarraylen(void **ptr);

#ifdef __cplusplus
}
#endif

#endif /* ZIP64J_CMDLINE_H */
