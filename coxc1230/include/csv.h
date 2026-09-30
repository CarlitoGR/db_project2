/*
 * csv.h - Minimal RFC 4180 CSV reader.
 *
 * Supports quoted fields, embedded commas/quotes/newlines, CRLF line endings,
 * a UTF-8 byte-order mark (Excel exports), blank lines and '#' comment lines.
 */
#ifndef SCHED_CSV_H
#define SCHED_CSV_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
    char **fields;
    size_t count;
    size_t cap;
    long line; /* 1-based line number where the record starts */
} csv_row;

typedef struct {
    FILE *fp;
    long line;   /* newlines consumed so far */
    char *buf;   /* scratch buffer for the field being read */
    size_t len;
    size_t cap;
} csv_reader;

/* Open a file for reading. Returns 0 on success, -1 on failure (errno set by fopen). */
int csv_open(csv_reader *reader, const char *path);
void csv_close(csv_reader *reader);

void csv_row_init(csv_row *row);
void csv_row_free(csv_row *row);

/*
 * Read the next non-blank, non-comment record.
 * Returns 1 when a row was read, 0 at end of file, -1 on error (unterminated quote / out of memory).
 */
int csv_next(csv_reader *reader, csv_row *row);

/*
 * Reduce a header to lowercase letters, digits and '#': "Enroll #" -> "enroll#", "End " -> "end",
 * "course_title" -> "coursetitle", "Course Title" -> "coursetitle".
 */
void csv_normalize_header(const char *text, char *out, size_t cap);

/*
 * Index of a header column, or -1 if absent. Comparison uses csv_normalize_header on both sides.
 * A trailing '*' makes it a prefix match: "instructor*" matches "Instructor 2025".
 */
int csv_find_column(const csv_row *header, const char *name);

/* First column matching any of the candidate names (tried in order), or -1. */
int csv_find_column_any(const csv_row *header, const char *const *names, size_t count);

/* Field text at idx, or "" if idx is negative or beyond the row. */
const char *csv_field(const csv_row *row, int idx);

/* Write one field to out, quoting it when required. */
void csv_write_field(FILE *out, const char *text);

#endif /* SCHED_CSV_H */
