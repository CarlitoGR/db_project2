/*
 * diag.c - Diagnostics collector implementation.
 */
#include "diag.h"

#include "common.h"

#include <stdarg.h>
#include <stdlib.h>

void diag_init(diag_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void diag_free(diag_list *list)
{
    free(list->items);
    diag_init(list);
}

void diag_add(diag_list *list, diag_level level, const char *fmt, ...)
{
    va_list args;
    diag_t *entry;

    if (sched_reserve((void **)&list->items, &list->cap, list->count + 1, sizeof(diag_t)) != 0) {
        return;
    }
    entry = &list->items[list->count++];
    entry->level = level;
    va_start(args, fmt);
    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
    va_end(args);
}

size_t diag_count(const diag_list *list, diag_level level)
{
    size_t i;
    size_t n = 0;

    for (i = 0; i < list->count; i++) {
        if (list->items[i].level == level) {
            n++;
        }
    }
    return n;
}

const char *diag_level_name(diag_level level)
{
    switch (level) {
    case DIAG_INFO:
        return "INFO";
    case DIAG_WARN:
        return "WARNING";
    case DIAG_ERROR:
        return "ERROR";
    }
    return "UNKNOWN";
}

void diag_print(const diag_list *list, FILE *out, diag_level min_level)
{
    size_t i;

    for (i = 0; i < list->count; i++) {
        if (list->items[i].level >= min_level) {
            fprintf(out, "%s: %s\n", diag_level_name(list->items[i].level), list->items[i].message);
        }
    }
}
