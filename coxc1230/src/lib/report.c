/*
 * report.c - Formatted text report and CSV data files.
 */
#include "report.h"

#include "csv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NOTE_TEXT "NOTE: Instructors are not to be interrupted during class times."
#define TBL_MAX_COLS 4
#define RULE_WIDTH 96

/* ---------------------------------------------------------------------------------------------- */
/* Small text-table renderer                                                                      */
/* ---------------------------------------------------------------------------------------------- */

typedef struct {
    char cell[TBL_MAX_COLS][SCHED_DISPLAY_LEN];
} tbl_row;

typedef struct {
    int ncols;
    const char *headers[TBL_MAX_COLS];
    tbl_row *rows;
    size_t count;
    size_t cap;
} text_table;

static void tbl_init(text_table *t, int ncols, const char *h0, const char *h1, const char *h2, const char *h3)
{
    t->ncols = ncols;
    t->headers[0] = h0;
    t->headers[1] = h1;
    t->headers[2] = h2;
    t->headers[3] = h3;
    t->rows = NULL;
    t->count = 0;
    t->cap = 0;
}

static void tbl_free(text_table *t)
{
    free(t->rows);
    t->rows = NULL;
    t->count = 0;
    t->cap = 0;
}

static int tbl_add(text_table *t, const char *c0, const char *c1, const char *c2, const char *c3)
{
    tbl_row *r;
    const char *cells[TBL_MAX_COLS];
    int i;

    if (sched_reserve((void **)&t->rows, &t->cap, t->count + 1, sizeof(tbl_row)) != 0) {
        return -1;
    }
    cells[0] = c0;
    cells[1] = c1;
    cells[2] = c2;
    cells[3] = c3;
    r = &t->rows[t->count++];
    for (i = 0; i < TBL_MAX_COLS; i++) {
        snprintf(r->cell[i], sizeof(r->cell[i]), "%s", (i < t->ncols && cells[i] != NULL) ? cells[i] : "");
    }
    return 0;
}

static void tbl_rule(FILE *out, const size_t *width, int ncols)
{
    int c;
    size_t k;

    for (c = 0; c < ncols; c++) {
        fputc('+', out);
        for (k = 0; k < width[c] + 2; k++) {
            fputc('-', out);
        }
    }
    fputs("+\n", out);
}

static void tbl_line(FILE *out, const size_t *width, int ncols, const char *const *cells)
{
    int c;

    for (c = 0; c < ncols; c++) {
        fprintf(out, "| %-*s ", (int)width[c], cells[c]);
    }
    fputs("|\n", out);
}

static void tbl_print(FILE *out, const text_table *t)
{
    size_t width[TBL_MAX_COLS] = { 0 };
    size_t r;
    int c;

    for (c = 0; c < t->ncols; c++) {
        width[c] = strlen(t->headers[c]);
        for (r = 0; r < t->count; r++) {
            size_t len = strlen(t->rows[r].cell[c]);

            if (len > width[c]) {
                width[c] = len;
            }
        }
    }
    tbl_rule(out, width, t->ncols);
    tbl_line(out, width, t->ncols, t->headers);
    tbl_rule(out, width, t->ncols);
    for (r = 0; r < t->count; r++) {
        const char *cells[TBL_MAX_COLS];

        for (c = 0; c < t->ncols; c++) {
            cells[c] = t->rows[r].cell[c];
        }
        tbl_line(out, width, t->ncols, cells);
    }
    if (t->count == 0) {
        const char *cells[TBL_MAX_COLS] = { "(none)", "", "", "" };

        tbl_line(out, width, t->ncols, cells);
    }
    tbl_rule(out, width, t->ncols);
}

/* ---------------------------------------------------------------------------------------------- */
/* Shared formatting helpers                                                                      */
/* ---------------------------------------------------------------------------------------------- */


static const char *source_label(oh_source source)
{
    return source == OH_SOURCE_LECTURER ? "Lecturer" : "Generated";
}

static const char *meeting_label(meeting_type m)
{
    switch (m) {
    case MEET_SCHEDULED:
        return "Scheduled";
    case MEET_ASYNC:
        return "Asynchronous";
    case MEET_TBA:
        return "TBA";
    }
    return "TBA";
}

/* Title without the trailing "(7R1)" session tag, which is shown in its own column. */
static void title_without_session(const course_t *c, char *buf, size_t cap)
{
    char work[SCHED_TITLE_LEN];
    size_t len;

    sched_copy(work, sizeof(work), c->title);
    len = strlen(work);
    if (c->session[0] != '\0' && len > 0 && work[len - 1] == ')') {
        char *open = strrchr(work, '(');

        if (open != NULL) {
            *open = '\0';
        }
    }
    sched_copy(buf, cap, work);
}

void report_course_display(const course_t *c, char *buf, size_t cap)
{
    char title[SCHED_TITLE_LEN];

    title_without_session(c, title, sizeof(title));
    if (title[0] != '\0') {
        snprintf(buf, cap, "%s %s", c->id, title);
    } else {
        snprintf(buf, cap, "%s", c->id);
    }
}

void report_course_times(const course_t *c, char *buf, size_t cap)
{
    char days[64];
    char range[48];

    switch (c->meeting) {
    case MEET_SCHEDULED:
        tu_format_days_long(c->days, days, sizeof(days));
        tu_format_range(c->start, c->end, range, sizeof(range));
        snprintf(buf, cap, "%s %s%s", days, range, c->note[0] != '\0' ? " *" : "");
        break;
    case MEET_ASYNC:
        snprintf(buf, cap, "Online asynchronous (no set time)");
        break;
    case MEET_TBA:
        snprintf(buf, cap, "TBA *");
        break;
    }
}

static int compare_course_ptr(const void *pa, const void *pb)
{
    const course_t *a = *(const course_t *const *)pa;
    const course_t *b = *(const course_t *const *)pb;
    int cmp = strcmp(a->session, b->session);
    int da;
    int db;

    if (cmp != 0) {
        return cmp;
    }
    da = tu_first_day(a->days);
    db = tu_first_day(b->days);
    if (da != db) {
        return da - db;
    }
    if (a->start != b->start) {
        return a->start - b->start;
    }
    return strcmp(a->id, b->id);
}

/* Collect sorted pointers to the sections taught by faculty fidx (-1 = unknown instructor). Caller frees. */
static const course_t **courses_for(const course_list *courses, int fidx, size_t *n_out)
{
    const course_t **list;
    size_t i;
    size_t n = 0;

    *n_out = 0;
    list = malloc((courses->count + 1) * sizeof(*list));
    if (list == NULL) {
        return NULL;
    }
    for (i = 0; i < courses->count; i++) {
        if (courses->items[i].faculty_idx == fidx) {
            list[n++] = &courses->items[i];
        }
    }
    if (n > 1) {
        qsort(list, n, sizeof(*list), compare_course_ptr);
    }
    *n_out = n;
    return list;
}

static int compare_oh_row(const void *pa, const void *pb)
{
    const oh_row *a = pa;
    const oh_row *b = pb;
    int da = tu_first_day(a->days);
    int db = tu_first_day(b->days);

    if (da != db) {
        return da - db;
    }
    return a->start - b->start;
}

size_t report_merge_hours(const oh_list *hours, size_t faculty_idx, oh_row *rows, size_t max_rows)
{
    size_t i;
    size_t n = 0;

    for (i = 0; i < hours->count; i++) {
        const oh_block_t *b = &hours->items[i];
        size_t r;
        int merged = 0;

        if (b->faculty_idx != faculty_idx) {
            continue;
        }
        for (r = 0; r < n; r++) {
            if (rows[r].start == b->start && rows[r].end == b->end && rows[r].source == b->source) {
                rows[r].days |= DAYMASK(b->day);
                merged = 1;
                break;
            }
        }
        if (!merged && n < max_rows) {
            rows[n].days = DAYMASK(b->day);
            rows[n].start = b->start;
            rows[n].end = b->end;
            rows[n].source = b->source;
            n++;
        }
    }
    if (n > 1) {
        qsort(rows, n, sizeof(oh_row), compare_oh_row);
    }
    return n;
}

static int total_enrollment(const course_t *const *mine, size_t n)
{
    size_t i;
    int total = 0;

    for (i = 0; i < n; i++) {
        if (mine[i]->enrollment > 0) {
            total += mine[i]->enrollment;
        }
    }
    return total;
}

static void print_banner(FILE *out, char ch)
{
    int i;

    for (i = 0; i < RULE_WIDTH; i++) {
        fputc(ch, out);
    }
    fputc('\n', out);
}

/* ---------------------------------------------------------------------------------------------- */
/* Text report                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

static sched_status print_class_table(FILE *out, const course_t *const *mine, size_t n)
{
    text_table tbl;
    size_t i;
    int notes = 0;

    tbl_init(&tbl, 4, "Course", "Session", "Times", "Location");
    for (i = 0; i < n; i++) {
        char course_txt[SCHED_DISPLAY_LEN];
        char times_txt[SCHED_DISPLAY_LEN];

        report_course_display(mine[i], course_txt, sizeof(course_txt));
        report_course_times(mine[i], times_txt, sizeof(times_txt));
        if (tbl_add(&tbl, course_txt, mine[i]->session[0] ? mine[i]->session : "-", times_txt, mine[i]->room) != 0) {
            tbl_free(&tbl);
            return SCHED_ERR_MEM;
        }
    }
    tbl_print(out, &tbl);
    tbl_free(&tbl);
    for (i = 0; i < n; i++) {
        if (mine[i]->note[0] != '\0' && mine[i]->meeting != MEET_ASYNC) {
            fprintf(out, "  * %s: %s\n", mine[i]->id, mine[i]->note);
            notes++;
        }
    }
    if (notes > 0) {
        fputc('\n', out);
    }
    return SCHED_OK;
}

static void print_weekly_view(FILE *out, const faculty_t *fac, size_t fidx, const course_t *const *mine, size_t n,
                              const oh_list *hours)
{
    int first = 8 * 60;
    int last = 17 * 60;
    int last_day = DAY_FRI;
    int t;
    int day;
    size_t i;

    for (i = 0; i < n; i++) {
        if (mine[i]->meeting != MEET_SCHEDULED) {
            continue;
        }
        if (mine[i]->start < first) {
            first = mine[i]->start;
        }
        if (mine[i]->end > last) {
            last = mine[i]->end;
        }
        if (mine[i]->days & (DAYMASK(DAY_SAT) | DAYMASK(DAY_SUN))) {
            last_day = DAY_SUN;
        }
    }
    for (i = 0; i < hours->count; i++) {
        if (hours->items[i].faculty_idx != fidx) {
            continue;
        }
        if (hours->items[i].start < first) {
            first = hours->items[i].start;
        }
        if (hours->items[i].end > last) {
            last = hours->items[i].end;
        }
        if (hours->items[i].day >= DAY_SAT) {
            last_day = DAY_SUN;
        }
    }
    first = (first / 30) * 30;
    last = ((last + 29) / 30) * 30;

    fprintf(out, "WEEKLY VIEW - %s  (session code = class in that session, BOTH = both sessions, OH = office "
            "hours)\n", fac->name);
    fprintf(out, "%-10s", "");
    for (day = 0; day <= last_day; day++) {
        fprintf(out, " %-8s", tu_day_abbr3(day));
    }
    fputc('\n', out);
    for (t = first; t < last; t += 30) {
        char label[16];

        tu_format_time(t, label, sizeof(label));
        fprintf(out, "%10s", label);
        for (day = 0; day <= last_day; day++) {
            const char *session = NULL;
            int in_class = 0;
            int mixed = 0;
            int in_office = 0;
            char mark[16] = ".";

            for (i = 0; i < n; i++) {
                const course_t *c = mine[i];

                if (c->meeting != MEET_SCHEDULED || (c->days & DAYMASK(day)) == 0
                    || !tu_overlaps(t, t + 30, c->start, c->end)) {
                    continue;
                }
                if (in_class && session != NULL && strcmp(session, c->session) != 0) {
                    mixed = 1;
                }
                in_class = 1;
                session = c->session;
            }
            for (i = 0; i < hours->count; i++) {
                const oh_block_t *b = &hours->items[i];

                if (b->faculty_idx == fidx && b->day == day && tu_overlaps(t, t + 30, b->start, b->end)) {
                    in_office = 1;
                }
            }
            if (in_class) {
                snprintf(mark, sizeof(mark), "%s", mixed ? "BOTH" : (session[0] != '\0' ? session : "CLASS"));
            }
            if (in_office) {
                snprintf(mark, sizeof(mark), "%s", in_class ? "CLASS/OH" : "OH");
            }
            fprintf(out, " %-8s", mark);
        }
        fputc('\n', out);
    }
}

static sched_status print_faculty(FILE *out, const course_list *courses, const faculty_t *fac, size_t fidx,
                                  const oh_list *hours)
{
    const course_t **mine;
    size_t n = 0;
    size_t i;
    text_table tbl;
    oh_row rows[REPORT_MAX_OH_ROWS];
    size_t nrows;
    char location[SCHED_ROOM_LEN + 32];
    int required = sched_required_minutes(fac, fidx, courses);
    int assigned = sched_assigned_minutes(hours, fidx);

    mine = courses_for(courses, (int)fidx, &n);
    if (mine == NULL) {
        return SCHED_ERR_MEM;
    }
    sched_office_location(fac, fidx, courses, location, sizeof(location));

    print_banner(out, '=');
    fprintf(out, " %s (%s)\n", fac->name, fac->id);
    fprintf(out, " Department: %s | Office hours location: %s", fac->department[0] ? fac->department : "-",
            location);
    if (fac->email[0] != '\0') {
        fprintf(out, " | %s", fac->email);
    }
    if (fac->phone[0] != '\0') {
        fprintf(out, " | %s", fac->phone);
    }
    fprintf(out, "\n Sections: %zu | Students enrolled: %d\n", n, total_enrollment(mine, n));
    print_banner(out, '=');

    fputs("\nCLASS SCHEDULE\n", out);
    if (print_class_table(out, mine, n) != SCHED_OK) {
        free(mine);
        return SCHED_ERR_MEM;
    }

    if (n == 0) {
        fputs("\nOFFICE HOURS: none assigned (no sections this term)\n\n", out);
        free(mine);
        return SCHED_OK;
    }
    fprintf(out, "\nOFFICE HOURS  (required %.2f h/week, assigned %.2f h/week; valid for both sessions)\n",
            required / 60.0, assigned / 60.0);
    tbl_init(&tbl, 4, "Day(s)", "Hours", "Location", "Arranged by");
    nrows = report_merge_hours(hours, fidx, rows, sizeof(rows) / sizeof(rows[0]));
    for (i = 0; i < nrows; i++) {
        char days_txt[SCHED_DISPLAY_LEN];
        char range_txt[48];

        tu_format_days_long(rows[i].days, days_txt, sizeof(days_txt));
        tu_format_range(rows[i].start, rows[i].end, range_txt, sizeof(range_txt));
        if (tbl_add(&tbl, days_txt, range_txt, location, source_label(rows[i].source)) != 0) {
            tbl_free(&tbl);
            free(mine);
            return SCHED_ERR_MEM;
        }
    }
    tbl_print(out, &tbl);
    tbl_free(&tbl);
    fprintf(out, "%s\n\n", NOTE_TEXT);

    print_weekly_view(out, fac, fidx, mine, n, hours);
    fputc('\n', out);
    free(mine);
    return SCHED_OK;
}

static sched_status print_unassigned(FILE *out, const course_list *courses)
{
    size_t n = 0;
    const course_t **orphans = courses_for(courses, -1, &n);
    sched_status st = SCHED_OK;

    if (orphans == NULL) {
        return SCHED_ERR_MEM;
    }
    if (n > 0) {
        print_banner(out, '=');
        fputs(" SECTIONS WITH AN INSTRUCTOR NOT FOUND IN THE FACULTY FILE\n", out);
        print_banner(out, '=');
        st = print_class_table(out, orphans, n);
        fputc('\n', out);
    }
    free(orphans);
    return st;
}

sched_status report_write_text(const char *path, const report_context *ctx, const course_list *courses,
                               const faculty_list *faculty, const oh_list *hours, const diag_list *diags)
{
    FILE *out = fopen(path, "w");
    size_t f;
    size_t i;
    size_t n_async = 0;
    size_t n_tba = 0;
    time_t now = time(NULL);
    char stamp[64] = "unknown";
    struct tm *tm_now = localtime(&now);
    sched_status st = SCHED_OK;

    if (out == NULL) {
        return SCHED_ERR_IO;
    }
    if (tm_now != NULL) {
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", tm_now);
    }
    for (i = 0; i < courses->count; i++) {
        n_async += courses->items[i].meeting == MEET_ASYNC;
        n_tba += courses->items[i].meeting == MEET_TBA;
    }
    print_banner(out, '#');
    fputs(" FACULTY TEACHING & OFFICE-HOURS SCHEDULE\n", out);
    fprintf(out, " Generated by schedgen %s on %s\n", SCHED_VERSION, stamp);
    fprintf(out, " Class schedule file : %s\n", ctx->syllabus_path);
    fprintf(out, " Faculty file        : %s\n", ctx->faculty_path);
    fprintf(out, " Faculty: %zu   Sections: %zu (%zu asynchronous, %zu TBA)   Class buffer: %d min\n", faculty->count,
            courses->count, n_async, n_tba, ctx->config->buffer_min);
    fprintf(out, " Diagnostics: %zu error(s), %zu warning(s), %zu info\n", diag_count(diags, DIAG_ERROR),
            diag_count(diags, DIAG_WARN), diag_count(diags, DIAG_INFO));
    print_banner(out, '#');
    fputc('\n', out);

    for (f = 0; f < faculty->count && st == SCHED_OK; f++) {
        st = print_faculty(out, courses, &faculty->items[f], f, hours);
    }
    if (st == SCHED_OK) {
        st = print_unassigned(out, courses);
    }
    if (diags->count > 0) {
        print_banner(out, '-');
        fputs("DIAGNOSTICS\n", out);
        diag_print(diags, out, DIAG_INFO);
    }
    if (fclose(out) != 0 && st == SCHED_OK) {
        st = SCHED_ERR_IO;
    }
    return st;
}

/* ---------------------------------------------------------------------------------------------- */
/* CSV data files                                                                                 */
/* ---------------------------------------------------------------------------------------------- */

static void write_row(FILE *out, const char *const *fields, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (i > 0) {
            fputc(',', out);
        }
        csv_write_field(out, fields[i]);
    }
    fputc('\n', out);
}

static FILE *open_in_dir(const char *dir, const char *name)
{
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, name);

    if (n < 0 || (size_t)n >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return fopen(path, "w");
}

static sched_status write_faculty_summary(const char *dir, const course_list *courses, const faculty_list *faculty,
                                          const oh_list *hours)
{
    static const char *const header[] = {
        "faculty_id", "name", "department", "office_hours_location", "email", "phone", "sections", "enrollment",
        "oh_required_hours", "oh_assigned_hours"
    };
    FILE *out = open_in_dir(dir, "faculty_summary.csv");
    size_t f;

    if (out == NULL) {
        return SCHED_ERR_IO;
    }
    write_row(out, header, sizeof(header) / sizeof(header[0]));
    for (f = 0; f < faculty->count; f++) {
        const faculty_t *fac = &faculty->items[f];
        char location[SCHED_ROOM_LEN + 32];
        char count_txt[16];
        char enroll_txt[16];
        char req_txt[16];
        char asg_txt[16];
        const char *fields[10];
        size_t n = 0;
        const course_t **mine = courses_for(courses, (int)f, &n);

        if (mine == NULL) {
            fclose(out);
            return SCHED_ERR_MEM;
        }
        sched_office_location(fac, f, courses, location, sizeof(location));
        snprintf(count_txt, sizeof(count_txt), "%zu", n);
        snprintf(enroll_txt, sizeof(enroll_txt), "%d", total_enrollment(mine, n));
        snprintf(req_txt, sizeof(req_txt), "%.2f", n > 0 ? sched_required_minutes(fac, f, courses) / 60.0 : 0.0);
        snprintf(asg_txt, sizeof(asg_txt), "%.2f", sched_assigned_minutes(hours, f) / 60.0);
        free(mine);
        fields[0] = fac->id;
        fields[1] = fac->name;
        fields[2] = fac->department;
        fields[3] = location;
        fields[4] = fac->email;
        fields[5] = fac->phone;
        fields[6] = count_txt;
        fields[7] = enroll_txt;
        fields[8] = req_txt;
        fields[9] = asg_txt;
        write_row(out, fields, 10);
    }
    return fclose(out) == 0 ? SCHED_OK : SCHED_ERR_IO;
}

static void write_class_row(FILE *out, const faculty_t *fac, const course_t *c)
{
    char display[SCHED_DISPLAY_LEN];
    char title[SCHED_TITLE_LEN];
    char days_short[16] = "";
    char days_long[80] = "";
    char start_txt[16] = "";
    char end_txt[16] = "";
    char times_txt[SCHED_DISPLAY_LEN];
    char credits_txt[8];
    char enroll_txt[16] = "";
    const char *fields[19];

    report_course_display(c, display, sizeof(display));
    title_without_session(c, title, sizeof(title));
    report_course_times(c, times_txt, sizeof(times_txt));
    if (c->meeting == MEET_SCHEDULED) {
        tu_format_days_short(c->days, days_short, sizeof(days_short));
        tu_format_days_long(c->days, days_long, sizeof(days_long));
        tu_format_time(c->start, start_txt, sizeof(start_txt));
        tu_format_time(c->end, end_txt, sizeof(end_txt));
    }
    snprintf(credits_txt, sizeof(credits_txt), "%d", c->credits);
    if (c->enrollment >= 0) {
        snprintf(enroll_txt, sizeof(enroll_txt), "%d", c->enrollment);
    }
    fields[0] = fac != NULL ? fac->id : "";
    fields[1] = fac != NULL ? fac->name : "";
    fields[2] = c->instructor;
    fields[3] = c->id;
    fields[4] = c->course;
    fields[5] = c->section;
    fields[6] = title;
    fields[7] = c->session;
    fields[8] = display;
    fields[9] = meeting_label(c->meeting);
    fields[10] = days_short;
    fields[11] = days_long;
    fields[12] = start_txt;
    fields[13] = end_txt;
    fields[14] = times_txt;
    fields[15] = c->room;
    fields[16] = credits_txt;
    fields[17] = enroll_txt;
    fields[18] = c->note;
    write_row(out, fields, 19);
}

static sched_status write_classes(const char *dir, const course_list *courses, const faculty_list *faculty)
{
    static const char *const header[] = {
        "faculty_id", "faculty_name", "instructor_as_listed", "section_id", "course", "section", "title", "session",
        "course_display", "meeting", "days", "days_display", "start_time", "end_time", "times_display", "room",
        "credits", "enrollment", "note"
    };
    FILE *out = open_in_dir(dir, "classes.csv");
    int f;

    if (out == NULL) {
        return SCHED_ERR_IO;
    }
    write_row(out, header, sizeof(header) / sizeof(header[0]));
    /* f == -1 writes sections whose instructor is not in the faculty file, last. */
    for (f = 0; f <= (int)faculty->count; f++) {
        int fidx = (f == (int)faculty->count) ? -1 : f;
        size_t n = 0;
        size_t i;
        const course_t **mine = courses_for(courses, fidx, &n);

        if (mine == NULL) {
            fclose(out);
            return SCHED_ERR_MEM;
        }
        for (i = 0; i < n; i++) {
            write_class_row(out, fidx >= 0 ? &faculty->items[fidx] : NULL, mine[i]);
        }
        free(mine);
    }
    return fclose(out) == 0 ? SCHED_OK : SCHED_ERR_IO;
}

static sched_status write_office_hours(const char *dir, const course_list *courses, const faculty_list *faculty,
                                       const oh_list *hours)
{
    static const char *const header[] = {
        "faculty_id", "faculty_name", "days", "days_display", "start_time", "end_time", "hours_display",
        "times_display", "location", "source"
    };
    FILE *out = open_in_dir(dir, "office_hours.csv");
    size_t f;

    if (out == NULL) {
        return SCHED_ERR_IO;
    }
    write_row(out, header, sizeof(header) / sizeof(header[0]));
    for (f = 0; f < faculty->count; f++) {
        const faculty_t *fac = &faculty->items[f];
        oh_row rows[REPORT_MAX_OH_ROWS];
        char location[SCHED_ROOM_LEN + 32];
        size_t n = report_merge_hours(hours, f, rows, sizeof(rows) / sizeof(rows[0]));
        size_t i;

        sched_office_location(fac, f, courses, location, sizeof(location));
        for (i = 0; i < n; i++) {
            char days_short[16];
            char days_long[80];
            char start_txt[16];
            char end_txt[16];
            char range_txt[48];
            char times_txt[SCHED_DISPLAY_LEN];
            const char *fields[10];

            tu_format_days_short(rows[i].days, days_short, sizeof(days_short));
            tu_format_days_long(rows[i].days, days_long, sizeof(days_long));
            tu_format_time(rows[i].start, start_txt, sizeof(start_txt));
            tu_format_time(rows[i].end, end_txt, sizeof(end_txt));
            tu_format_range(rows[i].start, rows[i].end, range_txt, sizeof(range_txt));
            snprintf(times_txt, sizeof(times_txt), "%s %s", days_long, range_txt);
            fields[0] = fac->id;
            fields[1] = fac->name;
            fields[2] = days_short;
            fields[3] = days_long;
            fields[4] = start_txt;
            fields[5] = end_txt;
            fields[6] = range_txt;
            fields[7] = times_txt;
            fields[8] = location;
            fields[9] = source_label(rows[i].source);
            write_row(out, fields, 10);
        }
    }
    return fclose(out) == 0 ? SCHED_OK : SCHED_ERR_IO;
}

sched_status report_write_csv(const char *dir, const course_list *courses, const faculty_list *faculty,
                              const oh_list *hours)
{
    sched_status st = write_faculty_summary(dir, courses, faculty, hours);

    if (st == SCHED_OK) {
        st = write_classes(dir, courses, faculty);
    }
    if (st == SCHED_OK) {
        st = write_office_hours(dir, courses, faculty, hours);
    }
    return st;
}

sched_status report_write_diagnostics(const char *path, const diag_list *diags)
{
    FILE *out = fopen(path, "w");

    if (out == NULL) {
        return SCHED_ERR_IO;
    }
    if (diags->count == 0) {
        fputs("No diagnostics: input files are consistent.\n", out);
    } else {
        diag_print(diags, out, DIAG_INFO);
    }
    return fclose(out) == 0 ? SCHED_OK : SCHED_ERR_IO;
}
