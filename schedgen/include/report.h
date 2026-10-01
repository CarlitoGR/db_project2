/*
 * report.h - Output generation (formatted text report and CSV data files).
 *
 * The CSV files are the hand-off point to scripts/export_docs.py, which turns them into
 * Word (.docx) and Excel (.xlsx) documents.
 */
#ifndef SCHED_REPORT_H
#define SCHED_REPORT_H

#include "diag.h"
#include "model.h"
#include "scheduler.h"

#include <stddef.h>

/* Upper bound on display rows per faculty member (40 h/week in 30-min blocks + lecturer blocks). */
#define REPORT_MAX_OH_ROWS 128

/* Office hours merged for display: same time + same source on several days becomes one row. */
typedef struct {
    daymask_t days;
    int start;
    int end;
    oh_source source;
} oh_row;

typedef struct {
    const char *syllabus_path;
    const char *faculty_path;
    const sched_config *config;
} report_context;

/* Merge one faculty member's blocks into display rows. Returns the number of rows written. */
size_t report_merge_hours(const oh_list *hours, size_t faculty_idx, oh_row *rows, size_t max_rows);

/* "CTEC 350.170 Prin & Meth of Intru Det & Pre" (section ID + title without the session tag). */
void report_course_display(const course_t *c, char *buf, size_t cap);

/* "Monday & Wednesday 4:00 PM to 6:50 PM", "Online asynchronous (no set time)" or "TBA *" ('*' = see note). */
void report_course_times(const course_t *c, char *buf, size_t cap);

/* Human-readable schedule for every faculty member (class table, office-hours table, weekly grid). */
sched_status report_write_text(const char *path, const report_context *ctx, const course_list *courses,
                               const faculty_list *faculty, const oh_list *hours, const diag_list *diags);

/* faculty_summary.csv, classes.csv and office_hours.csv inside dir. */
sched_status report_write_csv(const char *dir, const course_list *courses, const faculty_list *faculty,
                              const oh_list *hours);

/* Every diagnostic, one per line. */
sched_status report_write_diagnostics(const char *path, const diag_list *diags);

#endif /* SCHED_REPORT_H */
