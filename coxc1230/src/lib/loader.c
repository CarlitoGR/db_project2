/*
 * loader.c - Reads the two input files (class schedule and faculty) into memory.
 *
 * Columns are located by header name (case, spacing and punctuation ignored), so the department
 * spreadsheet can be saved as CSV and read directly: column order does not matter and extra
 * columns are ignored.
 */
#include "model.h"

#include "csv.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------- */
/* List management                                                                                */
/* ---------------------------------------------------------------------------------------------- */

void course_list_init(course_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void course_list_free(course_list *list)
{
    free(list->items);
    course_list_init(list);
}

void faculty_list_init(faculty_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void faculty_list_free(faculty_list *list)
{
    free(list->items);
    faculty_list_init(list);
}

/* ---------------------------------------------------------------------------------------------- */
/* Helpers                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

typedef struct {
    const char *names[4]; /* candidate header names; trailing '*' = prefix match */
    int required;
} column_spec;

static size_t name_count(const column_spec *spec)
{
    size_t n = 0;

    while (n < 4 && spec->names[n] != NULL) {
        n++;
    }
    return n;
}

/* Resolve header names to column indexes. Returns 0 if every required column is present. */
static int map_columns(const csv_row *header, const column_spec *specs, int *idx, size_t n, const char *path,
                       diag_list *diags)
{
    size_t i;
    int missing = 0;

    for (i = 0; i < n; i++) {
        idx[i] = csv_find_column_any(header, specs[i].names, name_count(&specs[i]));
        if (idx[i] < 0 && specs[i].required) {
            diag_add(diags, DIAG_ERROR, "%s: missing required column '%s'", path, specs[i].names[0]);
            missing = 1;
        }
    }
    return missing ? -1 : 0;
}

static int has_letter(const char *s)
{
    while (*s != '\0') {
        if (isalpha((unsigned char)*s)) {
            return 1;
        }
        s++;
    }
    return 0;
}

static int contains_icase(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);

    for (; *haystack != '\0'; haystack++) {
        size_t i = 0;

        while (i < n && haystack[i] != '\0'
               && tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/* Lowercase letters/digits; every run of other characters becomes one space. "Dr. Stone, D. B." -> "dr stone d b" */
static void normalize_person(const char *text, char *out, size_t cap)
{
    size_t n = 0;
    int pending_space = 0;

    if (cap == 0) {
        return;
    }
    for (; *text != '\0' && n + 1 < cap; text++) {
        unsigned char c = (unsigned char)*text;

        if (isalnum(c)) {
            if (pending_space && n > 0 && n + 2 < cap) {
                out[n++] = ' ';
            }
            pending_space = 0;
            out[n++] = (char)tolower(c);
        } else {
            pending_space = 1;
        }
    }
    out[n] = '\0';
}

/*
 * Parse "START-END". When only END carries AM/PM ("1:00-2:00 PM"), START inherits it
 * if that keeps START before END.
 */
static int parse_range(const char *text, int *start, int *end)
{
    char buf[SCHED_TEXT_LEN];
    char *dash;

    sched_copy(buf, sizeof(buf), text);
    dash = strchr(buf, '-');
    if (dash == NULL) {
        return -1;
    }
    *dash = '\0';
    if (tu_parse_time(buf, start) != 0 || tu_parse_time(dash + 1, end) != 0) {
        return -1;
    }
    if (!has_letter(buf) && has_letter(dash + 1) && *end >= 12 * 60 && *start < 12 * 60
        && *start + 12 * 60 < *end) {
        *start += 12 * 60;
    }
    return (*end > *start) ? 0 : -1;
}

int parse_fixed_hours(const char *text, time_block_t *blocks, size_t max_blocks, char *err, size_t err_cap)
{
    char work[SCHED_TEXT_LEN * 2];
    char *entry;
    char *save = NULL;
    size_t n = 0;

    sched_copy(work, sizeof(work), text);
    if (work[0] == '\0') {
        return 0;
    }
    for (entry = strtok_r(work, ";", &save); entry != NULL; entry = strtok_r(NULL, ";", &save)) {
        char days_txt[SCHED_TEXT_LEN];
        char *p = entry;
        char *range;
        daymask_t mask;
        int start;
        int end;
        int day;
        size_t len;

        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            continue;
        }
        /* Day token ends at the first digit (the start time). */
        range = p;
        while (*range != '\0' && !isdigit((unsigned char)*range)) {
            range++;
        }
        if (*range == '\0' || range == p) {
            snprintf(err, err_cap, "entry '%s' needs DAYS START-END", p);
            return -1;
        }
        len = (size_t)(range - p);
        if (len >= sizeof(days_txt)) {
            len = sizeof(days_txt) - 1;
        }
        memcpy(days_txt, p, len);
        days_txt[len] = '\0';
        if (tu_parse_days(days_txt, &mask) != 0) {
            snprintf(err, err_cap, "bad days '%s'", days_txt);
            return -1;
        }
        if (parse_range(range, &start, &end) != 0) {
            snprintf(err, err_cap, "bad time range '%s'", range);
            return -1;
        }
        for (day = 0; day < DAYS_PER_WEEK; day++) {
            if ((mask & DAYMASK(day)) == 0) {
                continue;
            }
            if (n >= max_blocks) {
                snprintf(err, err_cap, "more than %zu office-hour blocks", max_blocks);
                return -1;
            }
            blocks[n].day = day;
            blocks[n].start = start;
            blocks[n].end = end;
            n++;
        }
    }
    return (int)n;
}

int room_is_physical(const char *room)
{
    static const char *const virtual_rooms[] = { "online", "dept", "tba", "tbd", "virtual", "zoom", "na", "none" };
    char norm[SCHED_ROOM_LEN];
    size_t i;

    normalize_person(room, norm, sizeof(norm));
    if (norm[0] == '\0' || contains_icase(norm, "online")) {
        return 0;
    }
    for (i = 0; i < sizeof(virtual_rooms) / sizeof(virtual_rooms[0]); i++) {
        if (strcmp(norm, virtual_rooms[i]) == 0) {
            return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------------- */
/* Class-schedule file                                                                            */
/* ---------------------------------------------------------------------------------------------- */

enum {
    C_ID, C_TITLE, C_CREDITS, C_DAYS, C_BEGIN, C_END, C_ROOM, C_INSTRUCTOR, C_ENROLL, C_NCOLS
};

static const column_spec COURSE_COLUMNS[C_NCOLS] = {
    { { "ID", "course_id", "course_code", NULL }, 1 },
    { { "Course Title", "title", "course_title", NULL }, 1 },
    { { "CR", "credits", NULL, NULL }, 0 },
    { { "Days", NULL, NULL, NULL }, 1 },
    { { "Begin", "start", "start_time", NULL }, 1 },
    { { "End", "end_time", NULL, NULL }, 1 },
    { { "Room", NULL, NULL, NULL }, 1 },
    { { "Instructor*", "lecturer*", "faculty*", NULL }, 1 },
    { { "Enroll #", "enrollment", NULL, NULL }, 0 }
};

/* "CTEC 350.170" -> course "CTEC 350", section "170". */
static void split_id(course_t *c)
{
    const char *dot = strrchr(c->id, '.');

    if (dot == NULL) {
        sched_copy(c->course, sizeof(c->course), c->id);
        c->section[0] = '\0';
        return;
    }
    {
        size_t len = (size_t)(dot - c->id);
        char head[SCHED_CODE_LEN];

        if (len >= sizeof(head)) {
            len = sizeof(head) - 1;
        }
        memcpy(head, c->id, len);
        head[len] = '\0';
        sched_copy(c->course, sizeof(c->course), head);
    }
    sched_copy(c->section, sizeof(c->section), dot + 1);
}

/* Trailing "(7R1)" in the title -> session "7R1" (a single token of at most 15 characters). */
static void extract_session(course_t *c)
{
    size_t len = strlen(c->title);
    const char *open;
    char inner[SCHED_SESSION_LEN];
    size_t n;
    size_t i;

    c->session[0] = '\0';
    if (len < 3 || c->title[len - 1] != ')') {
        return;
    }
    open = strrchr(c->title, '(');
    if (open == NULL) {
        return;
    }
    n = (size_t)(&c->title[len - 1] - (open + 1));
    if (n == 0 || n >= sizeof(inner)) {
        return;
    }
    memcpy(inner, open + 1, n);
    inner[n] = '\0';
    for (i = 0; i < n; i++) {
        if (!isalnum((unsigned char)inner[i])) {
            return;
        }
    }
    sched_copy(c->session, sizeof(c->session), inner);
}

static int parse_int_field(const char *text, int lo, int hi, int *out)
{
    char buf[32];
    char *endp;
    long v;

    sched_copy(buf, sizeof(buf), text);
    if (buf[0] == '\0') {
        return 1; /* blank */
    }
    v = strtol(buf, &endp, 10);
    if (*endp != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

/* Fill days/start/end/meeting from the raw text. Always succeeds; problems downgrade to MEET_TBA. */
static void parse_meeting(const csv_row *row, const int *idx, const char *path, course_t *c, diag_list *diags)
{
    char days_txt[SCHED_TEXT_LEN];
    char begin_txt[SCHED_TEXT_LEN];
    char end_txt[SCHED_TEXT_LEN];

    sched_copy(days_txt, sizeof(days_txt), csv_field(row, idx[C_DAYS]));
    sched_copy(begin_txt, sizeof(begin_txt), csv_field(row, idx[C_BEGIN]));
    sched_copy(end_txt, sizeof(end_txt), csv_field(row, idx[C_END]));

    c->meeting = MEET_TBA;
    c->days = 0;
    c->start = 0;
    c->end = 0;

    if (days_txt[0] == '\0') {
        if (contains_icase(begin_txt, "async") || contains_icase(begin_txt, "online")) {
            c->meeting = MEET_ASYNC;
            sched_copy(c->note, sizeof(c->note), "Online asynchronous - no scheduled meeting time");
        } else {
            diag_add(diags, DIAG_WARN, "%s:%ld: %s has no meeting days; listed as TBA", path, row->line, c->id);
            sched_copy(c->note, sizeof(c->note), "Meeting days not given in source");
        }
        return;
    }
    if (tu_parse_days(days_txt, &c->days) != 0) {
        diag_add(diags, DIAG_ERROR, "%s:%ld: %s has invalid days '%s'; listed as TBA", path, row->line, c->id,
                 days_txt);
        snprintf(c->note, sizeof(c->note), "Invalid days in source: '%.40s'", days_txt);
        c->days = 0;
        return;
    }
    if (tu_parse_time(begin_txt, &c->start) != 0 || tu_parse_time(end_txt, &c->end) != 0) {
        diag_add(diags, DIAG_ERROR, "%s:%ld: %s has invalid time '%s'-'%s'; listed as TBA", path, row->line, c->id,
                 begin_txt, end_txt);
        snprintf(c->note, sizeof(c->note), "Invalid time in source: '%.24s'-'%.24s'", begin_txt,
                 end_txt);
        c->days = 0;
        return;
    }
    if (c->end <= c->start) {
        char s_txt[16];
        char e_txt[16];

        tu_format_time(c->start, s_txt, sizeof(s_txt));
        tu_format_time(c->end, e_txt, sizeof(e_txt));
        if (c->end < 12 * 60 && c->end + 12 * 60 > c->start) {
            char fixed[16];

            c->end += 12 * 60;
            tu_format_time(c->end, fixed, sizeof(fixed));
            diag_add(diags, DIAG_WARN, "%s:%ld: %s ends before it starts (%s-%s); end time read as %s", path,
                     row->line, c->id, s_txt, e_txt, fixed);
            snprintf(c->note, sizeof(c->note), "End time corrected from %s to %s", e_txt, fixed);
        } else {
            diag_add(diags, DIAG_ERROR, "%s:%ld: %s has an impossible meeting time (%s-%s); listed as TBA", path,
                     row->line, c->id, s_txt, e_txt);
            snprintf(c->note, sizeof(c->note), "Invalid time in source: %s-%s", s_txt, e_txt);
            c->days = 0;
            return;
        }
    }
    c->meeting = MEET_SCHEDULED;
}

static int parse_course_row(const csv_row *row, const int *idx, const char *path, course_t *c, diag_list *diags)
{
    int rc;

    memset(c, 0, sizeof(*c));
    c->source_line = row->line;
    c->faculty_idx = -1;
    c->enrollment = -1;
    sched_copy(c->id, sizeof(c->id), csv_field(row, idx[C_ID]));
    sched_copy(c->title, sizeof(c->title), csv_field(row, idx[C_TITLE]));
    sched_copy(c->instructor, sizeof(c->instructor), csv_field(row, idx[C_INSTRUCTOR]));
    sched_copy(c->room, sizeof(c->room), csv_field(row, idx[C_ROOM]));

    if (c->id[0] == '\0') {
        diag_add(diags, DIAG_ERROR, "%s:%ld: course ID is blank; row skipped", path, row->line);
        return -1;
    }
    if (c->instructor[0] == '\0') {
        diag_add(diags, DIAG_ERROR, "%s:%ld: %s has no instructor; row skipped", path, row->line, c->id);
        return -1;
    }
    if (c->room[0] == '\0') {
        sched_copy(c->room, sizeof(c->room), "TBA");
    }
    split_id(c);
    extract_session(c);
    parse_meeting(row, idx, path, c, diags);

    rc = parse_int_field(csv_field(row, idx[C_CREDITS]), 0, 12, &c->credits);
    if (rc < 0) {
        diag_add(diags, DIAG_WARN, "%s:%ld: %s has invalid credits '%s'; using 0", path, row->line, c->id,
                 csv_field(row, idx[C_CREDITS]));
        c->credits = 0;
    }
    rc = parse_int_field(csv_field(row, idx[C_ENROLL]), 0, 9999, &c->enrollment);
    if (rc != 0) {
        if (rc < 0) {
            diag_add(diags, DIAG_WARN, "%s:%ld: %s has invalid enrollment '%s'", path, row->line, c->id,
                     csv_field(row, idx[C_ENROLL]));
        }
        c->enrollment = -1;
    }
    return 0;
}

sched_status load_courses(const char *path, course_list *out, diag_list *diags)
{
    csv_reader reader;
    csv_row row;
    int idx[C_NCOLS];
    int rc;
    sched_status status = SCHED_OK;

    if (csv_open(&reader, path) != 0) {
        diag_add(diags, DIAG_ERROR, "%s: cannot open file (%s)", path, strerror(errno));
        return SCHED_ERR_IO;
    }
    csv_row_init(&row);
    rc = csv_next(&reader, &row);
    if (rc <= 0) {
        diag_add(diags, DIAG_ERROR, "%s: file is empty or unreadable", path);
        status = SCHED_ERR_PARSE;
        goto done;
    }
    if (map_columns(&row, COURSE_COLUMNS, idx, C_NCOLS, path, diags) != 0) {
        status = SCHED_ERR_PARSE;
        goto done;
    }
    while ((rc = csv_next(&reader, &row)) == 1) {
        course_t course;
        size_t i;
        int duplicate = 0;

        if (parse_course_row(&row, idx, path, &course, diags) != 0) {
            continue;
        }
        for (i = 0; i < out->count; i++) {
            if (sched_code_equal(out->items[i].id, course.id)) {
                diag_add(diags, DIAG_ERROR, "%s:%ld: duplicate section %s (first on line %ld); skipped", path,
                         row.line, course.id, out->items[i].source_line);
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (sched_reserve((void **)&out->items, &out->cap, out->count + 1, sizeof(course_t)) != 0) {
            status = SCHED_ERR_MEM;
            goto done;
        }
        out->items[out->count++] = course;
    }
    if (rc < 0) {
        diag_add(diags, DIAG_ERROR, "%s: malformed CSV near line %ld (unterminated quote?)", path, reader.line);
        status = SCHED_ERR_PARSE;
    }
done:
    csv_row_free(&row);
    csv_close(&reader);
    return status;
}

/* ---------------------------------------------------------------------------------------------- */
/* Faculty file                                                                                   */
/* ---------------------------------------------------------------------------------------------- */

enum {
    F_ID, F_NAME, F_ALIASES, F_DEPT, F_OFFICE, F_EMAIL, F_PHONE, F_COURSES, F_OH_HOURS, F_OH_BLOCK,
    F_PREF_DAYS, F_AVAIL_START, F_AVAIL_END, F_FIXED, F_NCOLS
};

static const column_spec FACULTY_COLUMNS[F_NCOLS] = {
    { { "faculty_id", NULL, NULL, NULL }, 1 },
    { { "name", NULL, NULL, NULL }, 1 },
    { { "aliases", NULL, NULL, NULL }, 0 },
    { { "department", NULL, NULL, NULL }, 0 },
    { { "office", NULL, NULL, NULL }, 0 },
    { { "email", NULL, NULL, NULL }, 0 },
    { { "phone", NULL, NULL, NULL }, 0 },
    { { "courses_taught", NULL, NULL, NULL }, 0 },
    { { "oh_hours_per_week", NULL, NULL, NULL }, 0 },
    { { "oh_block_minutes", NULL, NULL, NULL }, 0 },
    { { "preferred_days", NULL, NULL, NULL }, 0 },
    { { "available_start", NULL, NULL, NULL }, 0 },
    { { "available_end", NULL, NULL, NULL }, 0 },
    { { "fixed_office_hours", NULL, NULL, NULL }, 0 }
};

/* Round minutes to the nearest 15-minute slot. */
static int round_quarter(double minutes)
{
    return ((int)(minutes + 7.5) / 15) * 15;
}

static void parse_oh_policy(const csv_row *row, const int *idx, const char *path, faculty_t *f, diag_list *diags)
{
    char trimmed[SCHED_TEXT_LEN];

    f->oh_required_min = -1;
    sched_copy(trimmed, sizeof(trimmed), csv_field(row, idx[F_OH_HOURS]));
    if (trimmed[0] != '\0') {
        char *endp;
        double hours = strtod(trimmed, &endp);

        if (*endp != '\0' || hours < 0.0 || hours > 40.0) {
            diag_add(diags, DIAG_WARN, "%s:%ld: %s has invalid oh_hours_per_week '%s'; using policy default",
                     path, row->line, f->id, trimmed);
        } else {
            f->oh_required_min = round_quarter(hours * 60.0);
        }
    }

    f->oh_block_min = DEFAULT_OH_BLOCK_MIN;
    sched_copy(trimmed, sizeof(trimmed), csv_field(row, idx[F_OH_BLOCK]));
    if (trimmed[0] != '\0') {
        char *endp;
        long v = strtol(trimmed, &endp, 10);

        if (*endp != '\0' || v < 30 || v > 240) {
            diag_add(diags, DIAG_WARN, "%s:%ld: %s oh_block_minutes must be 30-240; using %d", path, row->line,
                     f->id, DEFAULT_OH_BLOCK_MIN);
        } else {
            f->oh_block_min = round_quarter((double)v);
        }
    }

    f->pref_days = 0; /* 0 = the days the professor teaches */
    sched_copy(trimmed, sizeof(trimmed), csv_field(row, idx[F_PREF_DAYS]));
    if (trimmed[0] != '\0' && tu_parse_days(trimmed, &f->pref_days) != 0) {
        diag_add(diags, DIAG_WARN, "%s:%ld: %s has invalid preferred_days '%s'; using teaching days", path,
                 row->line, f->id, trimmed);
        f->pref_days = 0;
    }

    f->avail_start = DEFAULT_OH_AVAIL_START;
    f->avail_end = DEFAULT_OH_AVAIL_END;
    sched_copy(trimmed, sizeof(trimmed), csv_field(row, idx[F_AVAIL_START]));
    if (trimmed[0] != '\0' && tu_parse_time(trimmed, &f->avail_start) != 0) {
        diag_add(diags, DIAG_WARN, "%s:%ld: %s has invalid available_start '%s'; using default", path, row->line,
                 f->id, trimmed);
        f->avail_start = DEFAULT_OH_AVAIL_START;
    }
    sched_copy(trimmed, sizeof(trimmed), csv_field(row, idx[F_AVAIL_END]));
    if (trimmed[0] != '\0' && tu_parse_time(trimmed, &f->avail_end) != 0) {
        diag_add(diags, DIAG_WARN, "%s:%ld: %s has invalid available_end '%s'; using default", path, row->line,
                 f->id, trimmed);
        f->avail_end = DEFAULT_OH_AVAIL_END;
    }
    if (f->avail_end <= f->avail_start) {
        diag_add(diags, DIAG_WARN, "%s:%ld: %s availability window is empty; using default", path, row->line,
                 f->id);
        f->avail_start = DEFAULT_OH_AVAIL_START;
        f->avail_end = DEFAULT_OH_AVAIL_END;
    }
}

static int parse_faculty_row(const csv_row *row, const int *idx, const char *path, faculty_t *f, diag_list *diags)
{
    char err[SCHED_TEXT_LEN];
    int n;

    memset(f, 0, sizeof(*f));
    f->source_line = row->line;
    sched_copy(f->id, sizeof(f->id), csv_field(row, idx[F_ID]));
    sched_copy(f->name, sizeof(f->name), csv_field(row, idx[F_NAME]));
    sched_copy(f->aliases, sizeof(f->aliases), csv_field(row, idx[F_ALIASES]));
    sched_copy(f->department, sizeof(f->department), csv_field(row, idx[F_DEPT]));
    sched_copy(f->office, sizeof(f->office), csv_field(row, idx[F_OFFICE]));
    sched_copy(f->email, sizeof(f->email), csv_field(row, idx[F_EMAIL]));
    sched_copy(f->phone, sizeof(f->phone), csv_field(row, idx[F_PHONE]));
    sched_copy(f->courses_taught, sizeof(f->courses_taught), csv_field(row, idx[F_COURSES]));

    if (f->id[0] == '\0' || f->name[0] == '\0') {
        diag_add(diags, DIAG_ERROR, "%s:%ld: faculty_id and name are required; row skipped", path, row->line);
        return -1;
    }
    parse_oh_policy(row, idx, path, f, diags);

    n = parse_fixed_hours(csv_field(row, idx[F_FIXED]), f->fixed, SCHED_MAX_FIXED_BLOCKS, err, sizeof(err));
    if (n < 0) {
        diag_add(diags, DIAG_WARN, "%s:%ld: %s fixed_office_hours ignored: %s", path, row->line, f->id, err);
        f->fixed_count = 0;
    } else {
        f->fixed_count = (size_t)n;
    }
    return 0;
}

sched_status load_faculty(const char *path, faculty_list *out, diag_list *diags)
{
    csv_reader reader;
    csv_row row;
    int idx[F_NCOLS];
    int rc;
    sched_status status = SCHED_OK;

    if (csv_open(&reader, path) != 0) {
        diag_add(diags, DIAG_ERROR, "%s: cannot open file (%s)", path, strerror(errno));
        return SCHED_ERR_IO;
    }
    csv_row_init(&row);
    rc = csv_next(&reader, &row);
    if (rc <= 0) {
        diag_add(diags, DIAG_ERROR, "%s: file is empty or unreadable", path);
        status = SCHED_ERR_PARSE;
        goto done;
    }
    if (map_columns(&row, FACULTY_COLUMNS, idx, F_NCOLS, path, diags) != 0) {
        status = SCHED_ERR_PARSE;
        goto done;
    }
    while ((rc = csv_next(&reader, &row)) == 1) {
        faculty_t fac;
        size_t i;
        int duplicate = 0;

        if (parse_faculty_row(&row, idx, path, &fac, diags) != 0) {
            continue;
        }
        for (i = 0; i < out->count; i++) {
            if (sched_strcasecmp(out->items[i].id, fac.id) == 0) {
                diag_add(diags, DIAG_ERROR, "%s:%ld: duplicate faculty_id '%s'; row skipped", path, row.line,
                         fac.id);
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (sched_reserve((void **)&out->items, &out->cap, out->count + 1, sizeof(faculty_t)) != 0) {
            status = SCHED_ERR_MEM;
            goto done;
        }
        out->items[out->count++] = fac;
    }
    if (rc < 0) {
        diag_add(diags, DIAG_ERROR, "%s: malformed CSV near line %ld (unterminated quote?)", path, reader.line);
        status = SCHED_ERR_PARSE;
    }
done:
    csv_row_free(&row);
    csv_close(&reader);
    return status;
}

/* ---------------------------------------------------------------------------------------------- */
/* Instructor resolution                                                                          */
/* ---------------------------------------------------------------------------------------------- */

static int person_equal(const char *a, const char *b)
{
    char na[SCHED_NAME_LEN];
    char nb[SCHED_NAME_LEN];

    normalize_person(a, na, sizeof(na));
    normalize_person(b, nb, sizeof(nb));
    return na[0] != '\0' && strcmp(na, nb) == 0;
}

int faculty_match(const faculty_list *list, const char *text)
{
    size_t i;

    for (i = 0; i < list->count; i++) {
        const faculty_t *f = &list->items[i];
        char work[SCHED_TEXT_LEN];
        char *tok;
        char *save = NULL;

        if (person_equal(text, f->id) || person_equal(text, f->name)) {
            return (int)i;
        }
        sched_copy(work, sizeof(work), f->aliases);
        for (tok = strtok_r(work, ";", &save); tok != NULL; tok = strtok_r(NULL, ";", &save)) {
            if (person_equal(text, tok)) {
                return (int)i;
            }
        }
    }
    return -1;
}

void link_instructors(course_list *courses, const faculty_list *faculty, diag_list *diags)
{
    size_t i;

    for (i = 0; i < courses->count; i++) {
        course_t *c = &courses->items[i];

        c->faculty_idx = faculty_match(faculty, c->instructor);
        if (c->faculty_idx < 0) {
            diag_add(diags, DIAG_ERROR, "%s (line %ld): instructor '%s' is not in the faculty file (add it as a "
                     "name or alias)", c->id, c->source_line, c->instructor);
        }
    }
}
