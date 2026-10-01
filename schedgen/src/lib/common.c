/*
 * common.c - Small string and memory helpers shared by all modules.
 */
#include "common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void sched_copy(char *dst, size_t cap, const char *src)
{
    size_t len;

    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (*src != '\0' && isspace((unsigned char)*src)) {
        src++;
    }
    len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) {
        len--;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

int sched_strcasecmp(const char *a, const char *b)
{
    int ca;
    int cb;

    do {
        ca = tolower((unsigned char)*a++);
        cb = tolower((unsigned char)*b++);
    } while (ca != '\0' && ca == cb);
    return ca - cb;
}

int sched_code_equal(const char *a, const char *b)
{
    for (;;) {
        while (*a != '\0' && isspace((unsigned char)*a)) {
            a++;
        }
        while (*b != '\0' && isspace((unsigned char)*b)) {
            b++;
        }
        if (*a == '\0' || *b == '\0') {
            return *a == *b;
        }
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
}

char *sched_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy != NULL) {
        memcpy(copy, s, len);
    }
    return copy;
}

int sched_reserve(void **items, size_t *cap, size_t need, size_t elem_size)
{
    size_t new_cap;
    void *grown;

    if (need <= *cap) {
        return 0;
    }
    new_cap = (*cap == 0) ? 8 : *cap;
    while (new_cap < need) {
        new_cap *= 2;
    }
    grown = realloc(*items, new_cap * elem_size);
    if (grown == NULL) {
        return -1;
    }
    *items = grown;
    *cap = new_cap;
    return 0;
}
