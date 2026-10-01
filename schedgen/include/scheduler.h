/*
 * scheduler.h - Conflict detection and office-hours assignment.
 */
#ifndef SCHED_SCHEDULER_H
#define SCHED_SCHEDULER_H

#include "diag.h"
#include "model.h"

#include <stddef.h>

#define SLOT_MIN      15
#define SLOTS_PER_DAY (MINUTES_PER_DAY / SLOT_MIN)

typedef enum {
    OH_SOURCE_GENERATED = 0, /* placed by the scheduler                      */
    OH_SOURCE_LECTURER       /* supplied by the lecturer in the faculty file */
} oh_source;

/* One office-hours block assigned to a faculty member. */
typedef struct {
    size_t faculty_idx;
    int day;
    int start;
    int end;
    oh_source source;
} oh_block_t;

typedef struct {
    oh_block_t *items;
    size_t count;
    size_t cap;
} oh_list;

typedef struct {
    int buffer_min;    /* protected minutes before and after each class (no office hours) */
    int min_block_min; /* smallest block the generator will place                         */
} sched_config;

void sched_config_default(sched_config *cfg);

void oh_list_init(oh_list *list);
void oh_list_free(oh_list *list);

/* 1 if the two sections run in the same part of term (or either session is unknown). */
int sched_sessions_overlap(const course_t *a, const course_t *b);

/*
 * Validate the loaded data (call link_instructors first):
 *   ERROR - instructor double-booked, physical room double-booked (same session only).
 *   WARN  - faculty courses_taught list disagrees with the class schedule.
 */
void sched_check_conflicts(const course_list *courses, const faculty_list *faculty, diag_list *diags);

/* Number of sections assigned to faculty member fidx. */
int sched_section_count(const course_list *courses, size_t fidx);

/* Weekly office-hour minutes required: explicit value, else max(3 h, 1 h per section). */
int sched_required_minutes(const faculty_t *fac, size_t fidx, const course_list *courses);

/* 1 if every section the faculty member teaches is online (no physical room). */
int sched_teaches_only_online(const course_list *courses, size_t fidx);

/* Office-hours location: office from the faculty file, else "Online (virtual)" or "Office TBA". */
void sched_office_location(const faculty_t *fac, size_t fidx, const course_list *courses, char *buf, size_t cap);

/*
 * Build every faculty member's office hours:
 *   1. Accept lecturer-supplied blocks that do not overlap a class.
 *   2. Greedily add generated blocks until the weekly requirement is met.
 * Never places office hours during a class. Result is sorted by faculty, day, start time.
 */
sched_status sched_assign_office_hours(const course_list *courses, const faculty_list *faculty,
                                       const sched_config *cfg, oh_list *out, diag_list *diags);

/* Total office-hour minutes assigned to one faculty member. */
int sched_assigned_minutes(const oh_list *hours, size_t faculty_idx);

#endif /* SCHED_SCHEDULER_H */
