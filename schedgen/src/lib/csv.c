/*
 * csv.c - Minimal RFC 4180 CSV reader/writer.
 */
#include "csv.h"

#include "common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int csv_open(csv_reader *reader, const char *path)
{
    int c1;

    reader->fp = fopen(path, "rb");
    reader->line = 0;
    reader->buf = NULL;
    reader->len = 0;
    reader->cap = 0;
    if (reader->fp == NULL) {
        return -1;
    }
    /* Skip a UTF-8 BOM (EF BB BF) if present. */
    c1 = fgetc(reader->fp);
    if (c1 == 0xEF) {
        int c2 = fgetc(reader->fp);
        int c3 = fgetc(reader->fp);

        if (c2 != 0xBB || c3 != 0xBF) {
            rewind(reader->fp);
        }
    } else if (c1 != EOF) {
        ungetc(c1, reader->fp);
    }
    return 0;
}

void csv_close(csv_reader *reader)
{
    if (reader->fp != NULL) {
        fclose(reader->fp);
        reader->fp = NULL;
    }
    free(reader->buf);
    reader->buf = NULL;
    reader->len = 0;
    reader->cap = 0;
}

void csv_row_init(csv_row *row)
{
    row->fields = NULL;
    row->count = 0;
    row->cap = 0;
    row->line = 0;
}

static void row_clear(csv_row *row)
{
    size_t i;

    for (i = 0; i < row->count; i++) {
        free(row->fields[i]);
    }
    row->count = 0;
}

void csv_row_free(csv_row *row)
{
    row_clear(row);
    free(row->fields);
    csv_row_init(row);
}

static int buf_append(csv_reader *reader, char c)
{
    if (sched_reserve((void **)&reader->buf, &reader->cap, reader->len + 2, 1) != 0) {
        return -1;
    }
    reader->buf[reader->len++] = c;
    return 0;
}

static int push_field(csv_reader *reader, csv_row *row)
{
    char *copy;

    if (buf_append(reader, '\0') != 0) {
        return -1;
    }
    copy = sched_strdup(reader->buf);
    reader->len = 0;
    if (copy == NULL) {
        return -1;
    }
    if (sched_reserve((void **)&row->fields, &row->cap, row->count + 1, sizeof(char *)) != 0) {
        free(copy);
        return -1;
    }
    row->fields[row->count++] = copy;
    return 0;
}

/* Read one physical record. Returns 1 = record, 0 = EOF, -1 = error. */
static int read_record(csv_reader *reader, csv_row *row)
{
    int c;
    int in_quotes = 0;
    int any = 0;

    row_clear(row);
    reader->len = 0;

    /* A line whose first character is '#' is a comment: discard it raw (quotes inside are ignored). */
    for (;;) {
        c = fgetc(reader->fp);
        if (c != '#') {
            if (c != EOF) {
                ungetc(c, reader->fp);
            }
            break;
        }
        while ((c = fgetc(reader->fp)) != EOF && c != '\n') {
        }
        reader->line++;
        if (c == EOF) {
            return 0;
        }
    }
    row->line = reader->line + 1;

    while ((c = fgetc(reader->fp)) != EOF) {
        any = 1;
        if (in_quotes) {
            if (c == '"') {
                int next = fgetc(reader->fp);

                if (next == '"') {
                    if (buf_append(reader, '"') != 0) {
                        return -1;
                    }
                } else {
                    in_quotes = 0;
                    if (next != EOF) {
                        ungetc(next, reader->fp);
                    }
                }
            } else {
                if (c == '\n') {
                    reader->line++;
                }
                if (buf_append(reader, (char)c) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (c == '"') {
            in_quotes = 1;
            continue;
        }
        if (c == ',') {
            if (push_field(reader, row) != 0) {
                return -1;
            }
            continue;
        }
        if (c == '\r') {
            int next = fgetc(reader->fp);

            if (next != '\n' && next != EOF) {
                ungetc(next, reader->fp);
            }
            c = '\n';
        }
        if (c == '\n') {
            reader->line++;
            return push_field(reader, row) == 0 ? 1 : -1;
        }
        if (buf_append(reader, (char)c) != 0) {
            return -1;
        }
    }
    if (in_quotes) {
        return -1;
    }
    if (!any) {
        return 0;
    }
    return push_field(reader, row) == 0 ? 1 : -1;
}

static int row_is_skippable(const csv_row *row)
{
    size_t i;
    const char *first;

    if (row->count == 0) {
        return 1;
    }
    first = row->fields[0];
    while (isspace((unsigned char)*first)) {
        first++;
    }
    if (*first == '#') {
        return 1;
    }
    for (i = 0; i < row->count; i++) {
        const char *p = row->fields[i];

        while (*p != '\0') {
            if (!isspace((unsigned char)*p)) {
                return 0;
            }
            p++;
        }
    }
    return 1;
}

int csv_next(csv_reader *reader, csv_row *row)
{
    for (;;) {
        int rc = read_record(reader, row);

        if (rc <= 0) {
            return rc;
        }
        if (!row_is_skippable(row)) {
            return 1;
        }
    }
}

void csv_normalize_header(const char *text, char *out, size_t cap)
{
    size_t n = 0;

    if (cap == 0) {
        return;
    }
    for (; *text != '\0' && n + 1 < cap; text++) {
        unsigned char c = (unsigned char)*text;

        if (isalnum(c) || c == '#' || c == '*') {
            out[n++] = (char)tolower(c);
        }
    }
    out[n] = '\0';
}

int csv_find_column(const csv_row *header, const char *name)
{
    size_t i;
    char want[SCHED_TEXT_LEN];
    char have[SCHED_TEXT_LEN];
    size_t want_len;
    int prefix = 0;

    csv_normalize_header(name, want, sizeof(want));
    want_len = strlen(want);
    if (want_len > 0 && want[want_len - 1] == '*') {
        want[--want_len] = '\0';
        prefix = 1;
    }
    for (i = 0; i < header->count; i++) {
        csv_normalize_header(header->fields[i], have, sizeof(have));
        if (prefix ? strncmp(have, want, want_len) == 0 : strcmp(have, want) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int csv_find_column_any(const csv_row *header, const char *const *names, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        int idx = csv_find_column(header, names[i]);

        if (idx >= 0) {
            return idx;
        }
    }
    return -1;
}

const char *csv_field(const csv_row *row, int idx)
{
    if (idx < 0 || (size_t)idx >= row->count) {
        return "";
    }
    return row->fields[idx];
}

void csv_write_field(FILE *out, const char *text)
{
    const char *p;

    if (strpbrk(text, ",\"\r\n") == NULL) {
        fputs(text, out);
        return;
    }
    fputc('"', out);
    for (p = text; *p != '\0'; p++) {
        if (*p == '"') {
            fputc('"', out);
        }
        fputc(*p, out);
    }
    fputc('"', out);
}
