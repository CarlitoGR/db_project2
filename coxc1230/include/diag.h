/*
 * diag.h - Diagnostics collector (info / warning / error messages).
 *
 * Every module reports problems here instead of printing directly, so the
 * main program decides where messages go (stderr, a report file, or both).
 */
#ifndef SCHED_DIAG_H
#define SCHED_DIAG_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    DIAG_INFO = 0,
    DIAG_WARN,
    DIAG_ERROR
} diag_level;

typedef struct {
    diag_level level;
    char message[320];
} diag_t;

typedef struct {
    diag_t *items;
    size_t count;
    size_t cap;
} diag_list;

#if defined(__GNUC__) || defined(__clang__)
#define SCHED_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define SCHED_PRINTF(fmt_idx, arg_idx)
#endif

void diag_init(diag_list *list);
void diag_free(diag_list *list);

/* Append a formatted message. Silently truncates long messages; ignores allocation failure. */
void diag_add(diag_list *list, diag_level level, const char *fmt, ...) SCHED_PRINTF(3, 4);

size_t diag_count(const diag_list *list, diag_level level);
const char *diag_level_name(diag_level level);

/* Print every message at or above min_level, one per line: "WARNING: ..." */
void diag_print(const diag_list *list, FILE *out, diag_level min_level);

#endif /* SCHED_DIAG_H */
