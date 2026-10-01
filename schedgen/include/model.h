/*
 * model.h - Domain data types (courses, faculty) and the input-file loaders.
 *
 * Input file 1 is the department class schedule in its native column layout
 * (ID, Course Title, CR, Days, Begin, End, Room, Instructor ..., Enroll #).
 * Input file 2 is the faculty information file, which also maps every spelling of an instructor's
 * name used in the schedule ("Bemley", "Bemley, J", "Bemley, Jesse") to one faculty record.
 */
#ifndef SCHED_MODEL_H
#define SCHED_MODEL_H

#include "common.h"
#include "diag.h"
#include "timeutil.h"

#include <stddef.h>

#define SCHED_MAX_FIXED_BLOCKS 21
#define SCHED_SESSION_LEN      16
#define SCHED_NOTE_LEN         96

/* Policy defaults applied when a faculty record leaves a field blank. */
#define DEFAULT_OH_BLOCK_MIN        60
#define DEFAULT_OH_AVAIL_START      (9 * 60)
#define DEFAULT_OH_AVAIL_END        (21 * 60)
#define DEFAULT_OH_MIN_WEEKLY_MIN   180 /* at least 3 hours per week ...        */
#define DEFAULT_OH_PER_COURSE_MIN   60  /* ... or 1 hour per section, if larger */

/* How a course section meets. Only MEET_SCHEDULED sections occupy time on the weekly grid. */
typedef enum {
    MEET_SCHEDULED = 0, /* fixed days and times                              */
    MEET_ASYNC,         /* online asynchronous: no meeting time              */
    MEET_TBA            /* times missing or invalid in the source file       */
} meeting_type;

/* One course section from the class-schedule file. */
typedef struct {
    char id[SCHED_CODE_LEN];             /* "CTEC 350.170" (course number + section)    */
    char course[SCHED_CODE_LEN];         /* "CTEC 350"                                  */
    char section[SCHED_SECTION_LEN];     /* "170"                                       */
    char title[SCHED_TITLE_LEN];         /* "Prin & Meth of Intru Det & Pre (7R1)"      */
    char session[SCHED_SESSION_LEN];     /* "7R1" - part of term; "" if not given       */
    char instructor[SCHED_NAME_LEN];     /* instructor text exactly as in the schedule  */
    int faculty_idx;                     /* resolved faculty record, -1 if unknown      */
    meeting_type meeting;
    daymask_t days;                      /* 0 unless MEET_SCHEDULED                     */
    int start;                           /* minutes after midnight                      */
    int end;
    char room[SCHED_ROOM_LEN];           /* "108", "201/112", "Online", "Dept"          */
    int credits;
    int enrollment;                      /* -1 if not given                             */
    char note[SCHED_NOTE_LEN];           /* data-quality note shown in the report       */
    long source_line;
} course_t;

/* A single-day time block (used for lecturer-supplied office hours). */
typedef struct {
    int day;
    int start;
    int end;
} time_block_t;

/* One faculty member from the professor information file. */
typedef struct {
    char id[SCHED_ID_LEN];
    char name[SCHED_NAME_LEN];
    char aliases[SCHED_TEXT_LEN];        /* ';'-separated schedule spellings of the name */
    char department[SCHED_NAME_LEN];
    char office[SCHED_ROOM_LEN];         /* "" = not assigned                            */
    char email[SCHED_EMAIL_LEN];
    char phone[SCHED_PHONE_LEN];
    char courses_taught[SCHED_TEXT_LEN]; /* ';'-separated section IDs                    */
    int oh_required_min;                 /* -1 = apply policy default                    */
    int oh_block_min;
    daymask_t pref_days;                 /* 0 = the days the professor teaches           */
    int avail_start;
    int avail_end;
    time_block_t fixed[SCHED_MAX_FIXED_BLOCKS];
    size_t fixed_count;
    long source_line;
} faculty_t;

typedef struct {
    course_t *items;
    size_t count;
    size_t cap;
} course_list;

typedef struct {
    faculty_t *items;
    size_t count;
    size_t cap;
} faculty_list;

void course_list_init(course_list *list);
void course_list_free(course_list *list);
void faculty_list_init(faculty_list *list);
void faculty_list_free(faculty_list *list);

/*
 * Load the class-schedule CSV (the department spreadsheet saved as CSV).
 * Columns are found by header name, case/space-insensitive:
 *   required  ID | Course Title | Days | Begin | End | Room | Instructor*
 *   optional  CR | Enroll #
 * Data repairs (each reported as a diagnostic):
 *   - end earlier than start with an AM end time ("12:00"-"3:15") -> end moved to PM   (WARN)
 *   - no days and "Asynchronous"/"Online" in Begin                -> MEET_ASYNC        (none)
 *   - unusable days/times                                         -> MEET_TBA, kept    (ERROR)
 * Returns SCHED_OK, SCHED_ERR_IO (cannot open), SCHED_ERR_PARSE (bad header / CSV) or SCHED_ERR_MEM.
 */
sched_status load_courses(const char *path, course_list *out, diag_list *diags);

/*
 * Load the faculty information CSV.
 * Required columns: faculty_id, name.
 * Optional columns: aliases, department, office, email, phone, courses_taught, oh_hours_per_week,
 *                   oh_block_minutes, preferred_days, available_start, available_end, fixed_office_hours.
 */
sched_status load_faculty(const char *path, faculty_list *out, diag_list *diags);

/*
 * Resolve every course's instructor text to a faculty record (by faculty_id, name, or any alias;
 * case, spacing and punctuation are ignored). Unresolved instructors are reported as ERRORs.
 */
void link_instructors(course_list *courses, const faculty_list *faculty, diag_list *diags);

/* Index of the faculty member whose id, name or alias matches text, or -1. */
int faculty_match(const faculty_list *list, const char *text);

/* 1 if the room is a real classroom (not blank, "Online", "Dept", "TBA", ...). */
int room_is_physical(const char *room);

/*
 * Parse a lecturer-supplied office-hours string: "M 10:00 AM-11:00 AM; TR 2:00-3:00 PM".
 * Each entry expands to one block per day. Returns number of blocks, or -1 on a malformed entry
 * (err receives a short description).
 */
int parse_fixed_hours(const char *text, time_block_t *blocks, size_t max_blocks, char *err, size_t err_cap);

#endif /* SCHED_MODEL_H */
