/*
 * test_schedule.c - Unit tests for libschedule.a (links against the static library only).
 */
#include "schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_run = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        g_run++;                                                                                    \
        if (!(cond)) {                                                                              \
            g_failed++;                                                                             \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                           \
    } while (0)

#define CHECK_STR(actual, expected)                                                                 \
    do {                                                                                            \
        g_run++;                                                                                    \
        if (strcmp((actual), (expected)) != 0) {                                                    \
            g_failed++;                                                                             \
            fprintf(stderr, "  FAIL %s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__,      \
                    (actual), (expected));                                                          \
        }                                                                                           \
    } while (0)

#define TMP_DIR "build"

static int write_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    return fclose(fp);
}

/* ---------------------------------------------------------------------------------------------- */
/* timeutil                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

static void test_parse_time(void)
{
    int m = -1;

    CHECK(tu_parse_time("9:30 AM", &m) == 0 && m == 570);
    CHECK(tu_parse_time("5:00 PM", &m) == 0 && m == 1020);
    CHECK(tu_parse_time("12:00 PM", &m) == 0 && m == 720);
    CHECK(tu_parse_time("12:15 am", &m) == 0 && m == 15);
    CHECK(tu_parse_time("17:45", &m) == 0 && m == 1065);
    CHECK(tu_parse_time("0930", &m) == 0 && m == 570);
    CHECK(tu_parse_time("5 p.m.", &m) == 0 && m == 1020);
    CHECK(tu_parse_time("4:00:00 PM", &m) == 0 && m == 960); /* Excel CSV export */
    CHECK(tu_parse_time("16:00:00", &m) == 0 && m == 960);
    CHECK(tu_parse_time("4:00:30 PM", &m) != 0);
    CHECK(tu_parse_time("13:00 PM", &m) != 0);
    CHECK(tu_parse_time("9:75", &m) != 0);
    CHECK(tu_parse_time("On-line Asynchronous Course", &m) != 0);
    CHECK(tu_parse_time("", &m) != 0);
}

static void test_parse_days(void)
{
    daymask_t d = 0;
    char buf[80];

    CHECK(tu_parse_days("MWF", &d) == 0 && d == (DAYMASK(DAY_MON) | DAYMASK(DAY_WED) | DAYMASK(DAY_FRI)));
    CHECK(tu_parse_days("TR", &d) == 0 && d == (DAYMASK(DAY_TUE) | DAYMASK(DAY_THU)));
    CHECK(tu_parse_days("TTh", &d) == 0 && d == (DAYMASK(DAY_TUE) | DAYMASK(DAY_THU)));
    CHECK(tu_parse_days("Tuesday & Thursday", &d) == 0 && d == (DAYMASK(DAY_TUE) | DAYMASK(DAY_THU)));
    CHECK(tu_parse_days("Mon/Wed", &d) == 0 && d == (DAYMASK(DAY_MON) | DAYMASK(DAY_WED)));
    CHECK(tu_parse_days("X", &d) != 0);
    CHECK(tu_parse_days("", &d) != 0);

    tu_format_days_long(DAYMASK(DAY_TUE) | DAYMASK(DAY_THU), buf, sizeof(buf));
    CHECK_STR(buf, "Tuesday & Thursday");
    tu_format_days_long(DAYMASK(DAY_MON) | DAYMASK(DAY_WED) | DAYMASK(DAY_FRI), buf, sizeof(buf));
    CHECK_STR(buf, "Monday, Wednesday & Friday");
    tu_format_days_short(DAYMASK(DAY_MON) | DAYMASK(DAY_THU), buf, sizeof(buf));
    CHECK_STR(buf, "MR");
    tu_format_range(16 * 60, 18 * 60 + 50, buf, sizeof(buf));
    CHECK_STR(buf, "4:00 PM to 6:50 PM");
}

static void test_fixed_hours(void)
{
    time_block_t blocks[8];
    char err[128];
    int n;

    n = parse_fixed_hours("MW 9:00 AM-10:00 AM; F 1:00-2:00 PM", blocks, 8, err, sizeof(err));
    CHECK(n == 3);
    CHECK(n == 3 && blocks[0].day == DAY_MON && blocks[0].start == 540 && blocks[0].end == 600);
    CHECK(n == 3 && blocks[1].day == DAY_WED);
    /* "1:00-2:00 PM": start inherits PM from the end time. */
    CHECK(n == 3 && blocks[2].day == DAY_FRI && blocks[2].start == 780 && blocks[2].end == 840);
    CHECK(parse_fixed_hours("", blocks, 8, err, sizeof(err)) == 0);
    CHECK(parse_fixed_hours("M 3:00 PM-1:00 PM", blocks, 8, err, sizeof(err)) == -1);
    CHECK(parse_fixed_hours("Q 9:00-10:00", blocks, 8, err, sizeof(err)) == -1);
}

/* ---------------------------------------------------------------------------------------------- */
/* csv                                                                                            */
/* ---------------------------------------------------------------------------------------------- */

static void test_csv_reader(void)
{
    const char *path = TMP_DIR "/test_quoted.csv";
    csv_reader r;
    csv_row row;

    /* BOM, comment with a stray quote, CRLF, quoted comma, escaped quote, embedded newline, no final EOL. */
    CHECK(write_file(path, "\xEF\xBB\xBF# comment with \"unbalanced quote\r\n"
                           "ID,CR ,End ,Instructor 2025,Enroll,Enroll #\r\n"
                           "\"x, y\",\"say \"\"hi\"\"\",\"line1\nline2\"\r\n"
                           "\r\n"
                           "last,row,noeol") == 0);
    CHECK(csv_open(&r, path) == 0);
    csv_row_init(&row);
    CHECK(csv_next(&r, &row) == 1 && row.count == 6);
    CHECK(csv_find_column(&row, "cr") == 1);          /* trailing space in header */
    CHECK(csv_find_column(&row, "End") == 2);
    CHECK(csv_find_column(&row, "instructor*") == 3); /* prefix match */
    CHECK(csv_find_column(&row, "Enroll") == 4);      /* "Enroll" and "Enroll #" stay distinct */
    CHECK(csv_find_column(&row, "enroll #") == 5);
    CHECK(csv_find_column(&row, "room") == -1);
    CHECK(csv_next(&r, &row) == 1 && row.count == 3);
    CHECK_STR(csv_field(&row, 0), "x, y");
    CHECK_STR(csv_field(&row, 1), "say \"hi\"");
    CHECK_STR(csv_field(&row, 2), "line1\nline2");
    CHECK_STR(csv_field(&row, 7), "");
    CHECK(csv_next(&r, &row) == 1 && row.count == 3);
    CHECK_STR(csv_field(&row, 2), "noeol");
    CHECK(csv_next(&r, &row) == 0);
    csv_row_free(&row);
    csv_close(&r);
    remove(path);
}

/* ---------------------------------------------------------------------------------------------- */
/* Loader: the department schedule format                                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_load_department_format(void)
{
    const char *path = TMP_DIR "/test_ctec.csv";
    course_list courses;
    diag_list diags;
    const course_t *c;

    CHECK(write_file(path,
                     "ID,Course Title,CR ,Days,Begin,End ,Room,Instructor 2025,Enroll,Enroll #\n"
                     "CTEC 350.170,Prin & Meth (7R1),3,MW,4:00 PM,6:50 PM,108,\"Adedoyin, Anthony\",,22\n"
                     "CTEC 294.170 ,PC Architecture (7R1),3,MW,16:00:00,18:50:00,112,\"Dixon, Carl\",,16\n"
                     "CTEC 120.170,Secure Coding (7R1),4,MW,12:00 PM,3:15 AM,116,\"Dr. Stone, D. B.\",,2\n"
                     "CTEC 711.555,IoT Capstone I (7R2),3,MWF,5:00 PM,5:00 PM,Online,Ruth Agada,,4\n"
                     "CTEC 125.555,Intro to Python (7R1) ,3,,On-line Asynchronous Course,,Online,\"Dr. Stone, D. B.\",,22\n"
                     "CTEC 335.170,Network Protocols (TCP/IP) (7R1),3,TR,4:00 PM,6:50 PM,116,\"Roberts, Curtis\",,11\n")
          == 0);
    course_list_init(&courses);
    diag_init(&diags);
    CHECK(load_courses(path, &courses, &diags) == SCHED_OK);
    CHECK(courses.count == 6);
    if (courses.count == 6) {
        c = &courses.items[0];
        CHECK_STR(c->id, "CTEC 350.170");
        CHECK_STR(c->course, "CTEC 350");
        CHECK_STR(c->section, "170");
        CHECK_STR(c->session, "7R1");
        CHECK_STR(c->instructor, "Adedoyin, Anthony");
        CHECK(c->meeting == MEET_SCHEDULED && c->start == 960 && c->end == 1130 && c->enrollment == 22);

        c = &courses.items[1]; /* trailing space in ID, seconds in times */
        CHECK_STR(c->id, "CTEC 294.170");
        CHECK(c->meeting == MEET_SCHEDULED && c->start == 960);

        c = &courses.items[2]; /* 3:15 AM end -> 3:15 PM */
        CHECK(c->meeting == MEET_SCHEDULED && c->end == 15 * 60 + 15 && c->note[0] != '\0');

        c = &courses.items[3]; /* zero-length meeting -> TBA */
        CHECK(c->meeting == MEET_TBA && c->days == 0);

        c = &courses.items[4]; /* asynchronous */
        CHECK(c->meeting == MEET_ASYNC && c->days == 0);
        CHECK_STR(c->session, "7R1");

        c = &courses.items[5]; /* title with two parenthetical groups */
        CHECK_STR(c->session, "7R1");
    }
    CHECK(diag_count(&diags, DIAG_ERROR) == 1); /* zero-length meeting */
    CHECK(diag_count(&diags, DIAG_WARN) == 1);  /* PM correction      */
    course_list_free(&courses);
    diag_free(&diags);
    remove(path);
}

static void test_instructor_aliases(void)
{
    faculty_t items[2];
    faculty_list faculty;

    memset(items, 0, sizeof(items));
    sched_copy(items[0].id, sizeof(items[0].id), "CT05");
    sched_copy(items[0].name, sizeof(items[0].name), "Jesse Bemley");
    sched_copy(items[0].aliases, sizeof(items[0].aliases), "Bemley;Bemley, J;Bemley, Jesse");
    sched_copy(items[1].id, sizeof(items[1].id), "CT22");
    sched_copy(items[1].name, sizeof(items[1].name), "Dr. D. B. Stone");
    sched_copy(items[1].aliases, sizeof(items[1].aliases), "Dr. Stone, D. B.");
    faculty.items = items;
    faculty.count = 2;
    faculty.cap = 2;

    CHECK(faculty_match(&faculty, "Bemley") == 0);
    CHECK(faculty_match(&faculty, "Bemley, J") == 0);
    CHECK(faculty_match(&faculty, "BEMLEY,  J.") == 0); /* case, spacing, punctuation ignored */
    CHECK(faculty_match(&faculty, "Jesse Bemley") == 0);
    CHECK(faculty_match(&faculty, "ct05") == 0);
    CHECK(faculty_match(&faculty, "Dr Stone D B") == 1);
    CHECK(faculty_match(&faculty, "Stone") == -1);
    CHECK(faculty_match(&faculty, "") == -1);

    CHECK(room_is_physical("108"));
    CHECK(room_is_physical("201/112"));
    CHECK(!room_is_physical("Online"));
    CHECK(!room_is_physical("online"));
    CHECK(!room_is_physical("Dept"));
    CHECK(!room_is_physical(""));
}

/* ---------------------------------------------------------------------------------------------- */
/* Scheduler                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

static course_t make_course(const char *id, const char *session, int fidx, daymask_t days, int start, int end,
                            const char *room)
{
    course_t c;

    memset(&c, 0, sizeof(c));
    sched_copy(c.id, sizeof(c.id), id);
    sched_copy(c.title, sizeof(c.title), id);
    sched_copy(c.session, sizeof(c.session), session);
    sched_copy(c.room, sizeof(c.room), room);
    c.faculty_idx = fidx;
    c.meeting = MEET_SCHEDULED;
    c.days = days;
    c.start = start;
    c.end = end;
    c.enrollment = -1;
    return c;
}

static faculty_t make_faculty(const char *id)
{
    faculty_t f;

    memset(&f, 0, sizeof(f));
    sched_copy(f.id, sizeof(f.id), id);
    sched_copy(f.name, sizeof(f.name), id);
    f.oh_required_min = -1;
    f.oh_block_min = 60;
    f.pref_days = 0;
    f.avail_start = 9 * 60;
    f.avail_end = 21 * 60;
    return f;
}

static void test_session_aware_conflicts(void)
{
    course_t items[6];
    faculty_t fac[2];
    course_list courses;
    faculty_list faculty;
    diag_list diags;
    daymask_t mw = DAYMASK(DAY_MON) | DAYMASK(DAY_WED);

    items[0] = make_course("A 350.170", "7R1", 0, mw, 960, 1130, "108");
    items[1] = make_course("A 350.180", "7R2", 0, mw, 960, 1130, "108");    /* other session: OK   */
    items[2] = make_course("A 402.180", "7R2", 0, mw, 1140, 1310, "Online");
    items[3] = make_course("A 435.180", "7R2", 0, mw, 1140, 1310, "114");   /* instructor clash    */
    items[4] = make_course("B 120.171", "7R1", 1, mw, 1020, 1140, "201/112");
    items[5] = make_course("B 294.170", "7R1", 1, DAYMASK(DAY_MON), 960, 1130, "112"); /* room + instr. */
    courses.items = items;
    courses.count = 6;
    courses.cap = 6;
    fac[0] = make_faculty("F0");
    fac[1] = make_faculty("F1");
    faculty.items = fac;
    faculty.count = 2;
    faculty.cap = 2;

    CHECK(!sched_sessions_overlap(&items[0], &items[1]));
    CHECK(sched_sessions_overlap(&items[2], &items[3]));

    diag_init(&diags);
    sched_check_conflicts(&courses, &faculty, &diags);
    /* 402.180 x 435.180 instructor; 120.171 x 294.170 instructor + room 112 */
    CHECK(diag_count(&diags, DIAG_ERROR) == 3);
    diag_free(&diags);
}

static void test_scheduler_invariants(void)
{
    course_t items[4];
    faculty_t fac[1];
    course_list courses;
    faculty_list faculty;
    oh_list hours;
    diag_list diags;
    sched_config cfg;
    daymask_t mw = DAYMASK(DAY_MON) | DAYMASK(DAY_WED);
    size_t i;
    size_t c;

    items[0] = make_course("A 350.170", "7R1", 0, mw, 960, 1130, "108");
    items[1] = make_course("A 435.170", "7R1", 0, mw, 1140, 1310, "108");
    items[2] = make_course("A 350.180", "7R2", 0, mw, 960, 1130, "108");
    items[3] = make_course("A 125.555", "7R1", 0, 0, 0, 0, "Online");
    items[3].meeting = MEET_ASYNC;
    courses.items = items;
    courses.count = 4;
    courses.cap = 4;
    fac[0] = make_faculty("F0");
    faculty.items = fac;
    faculty.count = 1;
    faculty.cap = 1;

    sched_config_default(&cfg);
    oh_list_init(&hours);
    diag_init(&diags);

    /* Policy default: 4 sections (async counts) -> max(3 h, 4 x 1 h) = 240 minutes. */
    CHECK(sched_required_minutes(&fac[0], 0, &courses) == 240);
    CHECK(sched_assign_office_hours(&courses, &faculty, &cfg, &hours, &diags) == SCHED_OK);
    CHECK(sched_assigned_minutes(&hours, 0) == 240);

    for (i = 0; i < hours.count; i++) {
        const oh_block_t *b = &hours.items[i];

        /* Preferred days default to teaching days (MW). */
        CHECK(b->day == DAY_MON || b->day == DAY_WED);
        CHECK(b->start >= 9 * 60 && b->end <= 21 * 60);
        /* Never overlaps any session's class or its buffer. */
        for (c = 0; c < courses.count; c++) {
            const course_t *crs = &courses.items[c];

            if (crs->meeting == MEET_SCHEDULED && (crs->days & DAYMASK(b->day))) {
                CHECK(!tu_overlaps(b->start, b->end, crs->start - cfg.buffer_min, crs->end + cfg.buffer_min));
            }
        }
        /* Touching generated blocks on the same day are merged into one. */
        for (c = i + 1; c < hours.count; c++) {
            CHECK(!(hours.items[c].day == b->day && (hours.items[c].start == b->end || hours.items[c].end == b->start)));
        }
    }
    oh_list_free(&hours);
    diag_free(&diags);
}

static void test_fixed_hours_dropped_on_clash(void)
{
    course_t items[1];
    faculty_t fac[1];
    course_list courses;
    faculty_list faculty;
    oh_list hours;
    diag_list diags;
    sched_config cfg;
    char err[64];
    char loc[64];
    size_t i;

    items[0] = make_course("B 100.170", "", 0, DAYMASK(DAY_TUE), 14 * 60, 15 * 60 + 15, "Online");
    courses.items = items;
    courses.count = 1;
    courses.cap = 1;
    fac[0] = make_faculty("F0");
    fac[0].oh_required_min = 120;
    fac[0].fixed_count =
        (size_t)parse_fixed_hours("T 2:30 PM-3:30 PM; R 10:00-11:00 AM", fac[0].fixed, 4, err, sizeof(err));
    faculty.items = fac;
    faculty.count = 1;
    faculty.cap = 1;

    sched_config_default(&cfg);
    oh_list_init(&hours);
    diag_init(&diags);
    CHECK(sched_assign_office_hours(&courses, &faculty, &cfg, &hours, &diags) == SCHED_OK);
    CHECK(diag_count(&diags, DIAG_WARN) == 1);
    CHECK(sched_assigned_minutes(&hours, 0) == 120);
    for (i = 0; i < hours.count; i++) {
        CHECK(!(hours.items[i].day == DAY_TUE && tu_overlaps(hours.items[i].start, hours.items[i].end, 840, 915)));
    }
    /* Online-only instructor with no office -> virtual office hours. */
    sched_office_location(&fac[0], 0, &courses, loc, sizeof(loc));
    CHECK_STR(loc, "Online (virtual)");
    oh_list_free(&hours);
    diag_free(&diags);
}

int main(void)
{
    test_parse_time();
    test_parse_days();
    test_fixed_hours();
    test_csv_reader();
    test_load_department_format();
    test_instructor_aliases();
    test_session_aware_conflicts();
    test_scheduler_invariants();
    test_fixed_hours_dropped_on_clash();

    printf("unit tests: %d checks, %d failed\n", g_run, g_failed);
    if (g_failed == 0) {
        puts("PASS: unit tests");
    }
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
