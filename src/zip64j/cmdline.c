/*
 * cmdline.c - Command-line tokenizer (CP932-safe).
 *
 * Adapted from zip32j's CMDLINE.C by Yoshioka Tsuneo, whose header reads:
 *   "You can use this file as Public Domain Software.
 *    Copy,Edit,Re-distibute and for any purpose,you can use this file."
 * Local changes: functions prefixed with zip64j_ to avoid colliding with
 * zip30's own realloc2 / splitarg helpers.
 */

#include "cmdline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Shift-JIS / CP932 double-byte character detection. First byte ranges:
 * 0x81-0x9F or 0xE0-0xFC; second byte: 0x40-0x7E or 0x80-0xFC. */
#define UCH(c)       ((unsigned char)(c))
#define IS_SJIS_1(c) ((0x81 <= UCH(c) && UCH(c) <= 0x9F) || (0xE0 <= UCH(c) && UCH(c) <= 0xFC))
#define IS_SJIS_2(c) ((0x40 <= UCH(c) && UCH(c) <= 0x7E) || (0x80 <= UCH(c) && UCH(c) <= 0xFC))

int zip64j_ptrarraylen(void **ptr)
{
    int len = 0;
    while (*ptr++) len++;
    return len;
}

/* realloc wrapper that writes back only on success. */
static void *realloc_inplace(void **ptr, size_t size)
{
    void *ret = realloc(*ptr, size);
    if (ret) *ptr = ret;
    return ret;
}

/* Pack a char*[] into a single allocation: [ptrs][NULL][strings]. */
static char **strarraydup(char **src)
{
    int n, i;
    size_t alllen = 0;
    char **out = NULL;
    char *strbase;

    n = zip64j_ptrarraylen((void **)src);
    for (i = 0; i < n; i++) alllen += strlen(src[i]) + 1;

    out = (char **)malloc(sizeof(char *) * (n + 1) + alllen);
    if (!out) return NULL;

    strbase = (char *)out + sizeof(char *) * (n + 1);
    for (i = 0; i < n; i++) {
        size_t len = strlen(src[i]) + 1;
        memcpy(strbase, src[i], len);
        out[i] = strbase;
        strbase += len;
    }
    out[n] = NULL;
    return out;
}

static char **split_cmdline(const char *cmdline)
{
    const char *p = cmdline;
    char **files = NULL;
    char **files_out = NULL;
    int filenum = 0;
    int i;

    while (isspace(UCH(*p))) p++;

    while (*p) {
        size_t capacity = 4096;
        size_t len = 0;
        char *file = (char *)malloc(capacity);
        char *fp;
        int quote_mode = 0;

        if (!file) goto cleanup;
        fp = file;

        while ((!isspace(UCH(*p)) || quote_mode) && *p != '\0') {
            if (*p == '"') {
                quote_mode = !quote_mode;
                p++;
                continue;
            }
            if (len + 5 >= capacity) {
                char *nf;
                capacity += 4096;
                nf = (char *)realloc(file, capacity);
                if (!nf) { free(file); goto cleanup; }
                file = nf;
                fp = file + len;
            }
            if (IS_SJIS_1(*p) && IS_SJIS_2(*(p + 1))) {
                *fp++ = *p++;
                len++;
            }
            *fp++ = *p++;
            len++;
        }
        *fp = '\0';

        if (!realloc_inplace((void **)&files, sizeof(char *) * (filenum + 1))) {
            free(file);
            goto cleanup;
        }
        files[filenum++] = file;

        while (isspace(UCH(*p))) p++;
    }

    if (!realloc_inplace((void **)&files, sizeof(char *) * (filenum + 1))) {
        goto cleanup;
    }
    files[filenum] = NULL;
    files_out = strarraydup(files);

cleanup:
    if (files) {
        for (i = 0; i < filenum; i++) free(files[i]);
        free(files);
    }
    return files_out;
}

static char *loadfile(const char *fname)
{
    FILE *fp;
    char *body = NULL;
    struct stat st;
    size_t n;

    if (stat(fname, &st) != 0) return NULL;
    fp = fopen(fname, "rb");
    if (!fp) return NULL;

    body = (char *)malloc((size_t)st.st_size + 1);
    if (!body) { fclose(fp); return NULL; }

    n = fread(body, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (n != (size_t)st.st_size) { free(body); return NULL; }

    body[st.st_size] = '\0';
    return body;
}

char **zip64j_split_cmdline_with_response(const char *cmdline)
{
    char **files = NULL;
    char **walk;
    char **merged = NULL;
    int merged_count = 0;
    char ***expanded = NULL;
    int expanded_count = 0;
    char **result = NULL;
    int i;

    files = split_cmdline(cmdline);
    if (!files) return NULL;

    for (walk = files; *walk; walk++) {
        if (**walk == '@') {
            char *body = loadfile(*walk + 1);
            char **sub;
            char **cursor;
            if (!body) goto cleanup;
            sub = split_cmdline(body);
            free(body);
            if (!sub) goto cleanup;

            if (!realloc_inplace((void **)&expanded,
                                 sizeof(char **) * (expanded_count + 1))) {
                free(sub);
                goto cleanup;
            }
            expanded[expanded_count++] = sub;

            for (cursor = sub; *cursor; cursor++) {
                if (!realloc_inplace((void **)&merged,
                                     sizeof(char *) * (merged_count + 1))) {
                    goto cleanup;
                }
                merged[merged_count++] = *cursor;
            }
        } else {
            if (!realloc_inplace((void **)&merged,
                                 sizeof(char *) * (merged_count + 1))) {
                goto cleanup;
            }
            merged[merged_count++] = *walk;
        }
    }

    if (!realloc_inplace((void **)&merged,
                         sizeof(char *) * (merged_count + 1))) {
        goto cleanup;
    }
    merged[merged_count] = NULL;
    result = strarraydup(merged);

cleanup:
    free(files);
    free(merged);
    if (expanded) {
        for (i = 0; i < expanded_count; i++) free(expanded[i]);
        free(expanded);
    }
    return result;
}
