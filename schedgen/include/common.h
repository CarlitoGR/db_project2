/*
 * common.h - Shared constants, status codes and small string helpers.
 *
 * Part of libschedule (static library) for the Faculty Schedule Generator.
 */
#ifndef SCHED_COMMON_H
#define SCHED_COMMON_H

#include <stddef.h>

#define SCHED_VERSION "1.0.0"

/* Fixed field capacities (including the terminating NUL). */
#define SCHED_ID_LEN      24
#define SCHED_CODE_LEN    24
#define SCHED_NAME_LEN    80
#define SCHED_TITLE_LEN   96
#define SCHED_ROOM_LEN    32
#define SCHED_EMAIL_LEN   80
#define SCHED_PHONE_LEN   32
#define SCHED_SECTION_LEN 12
#define SCHED_TEXT_LEN    256
#define SCHED_DISPLAY_LEN 160

/* Status codes returned by library functions. */
typedef enum {
    SCHED_OK = 0,
    SCHED_ERR_IO,
    SCHED_ERR_PARSE,
    SCHED_ERR_MEM,
    SCHED_ERR_ARG
} sched_status;

/* Copy src into dst (capacity cap), trimming leading/trailing whitespace. Always NUL-terminates. */
void sched_copy(char *dst, size_t cap, const char *src);

/* Case-insensitive string comparison (portable replacement for strcasecmp). */
int sched_strcasecmp(const char *a, const char *b);

/* Compare two course codes ignoring case and whitespace ("COSC 631" == "cosc631"). */
int sched_code_equal(const char *a, const char *b);

/* Heap duplicate of a string; returns NULL on allocation failure. */
char *sched_strdup(const char *s);

/*
 * Grow a dynamic array so it can hold at least `need` elements of `elem_size` bytes.
 * Returns 0 on success, -1 on allocation failure (the original block is untouched).
 */
int sched_reserve(void **items, size_t *cap, size_t need, size_t elem_size);

#endif /* SCHED_COMMON_H */
