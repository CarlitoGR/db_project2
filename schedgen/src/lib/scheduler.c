/*
 * scheduler.c - Conflict detection and office-hours assignment.
 *
 * Each faculty member gets a weekly grid of 15-minute slots. Class meetings are marked CLASS,
 * a configurable protection buffer around each class is marked BUFFER, and office hours are
 * marked OFFICE. Generated office hours may only occupy FREE slots, so they can never collide
 * with a class ("Instructors are not to be interrupted during class times").
 *
 * Part-of-term sessions (7R1 = first 7 weeks, 7R2 = second 7 weeks):
 *   - Conflicts are reported only between sections in the same session (or when a session is unknown).
 *   - The office-hours grid uses the UNION of both sessions' classes, so one office-hours schedule is
 *     valid for the whole semester.
 */
#include "scheduler.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    CELL_FREE = 0,
    CELL_BUFFER,
    CELL_CLASS,
    CELL_OFFICE
};

typedef struct {
    unsigned char cell[DAYS_PER_WEEK][SLOTS_PER_DAY];
} week_grid;

/* Scoring weights for candidate office-hour blocks (higher is better). */
#define SCORE_TEACHING_DAY    30    /* professor is already on campus that day                  */
#define SCORE_ADJACENT        20    /* block touches a class buffer or an existing office block */
#define SCORE_SAME_DAY_BLOCK  (-50) /* per block already on that day (spread the week)          */
#define SCORE_PER_10MIN_GAP   (-1)  /* distance from the nearest class that day                 */
#define SCORE_NON_PREFERRED   (-100)

void sched_config_default(sched_config *cfg)
{
    cfg->buffer_min = 15;
    cfg->min_block_min = 30;
}

void oh_list_init(oh_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void oh_list_free(oh_list *list)
{
    free(list->items);
    oh_list_init(list);
}

/* ---------------------------------------------------------------------------------------------- */
/* Conflict detection                                                                             */
/* ---------------------------------------------------------------------------------------------- */

int sched_sessions_overlap(const course_t *a, const course_t *b)
{
    return a->session[0] == '\0' || b->session[0] == '\0' || sched_strcasecmp(a->session, b->session) == 0;
}

static void describe_meeting(const course_t *c, char *buf, size_t cap)
{
    char days[16];
    char range[48];

    tu_format_days_short(c->days, days, sizeof(days));
    tu_format_range(c->start, c->end, range, sizeof(range));
    if (c->session[0] != '\0') {
        snprintf(buf, cap, "%s [%s] %s %s", c->id, c->session, days, range);
    } else {
        snprintf(buf, cap, "%s %s %s", c->id, days, range);
    }
}

/* 1 if two room strings share a physical room ("201/112" shares "112" with "112"); copies it to shared. */
static int rooms_share(const char *a, const char *b, char *shared, size_t cap)
{
    char wa[SCHED_ROOM_LEN];
    char *ta;
    char *sa = NULL;

    if (!room_is_physical(a) || !room_is_physical(b)) {
        return 0;
    }
    sched_copy(wa, sizeof(wa), a);
    for (ta = strtok_r(wa, "/,;&", &sa); ta != NULL; ta = strtok_r(NULL, "/,;&", &sa)) {
        char wb[SCHED_ROOM_LEN];
        char *tb;
        char *sb = NULL;

        sched_copy(wb, sizeof(wb), b);
        for (tb = strtok_r(wb, "/,;&", &sb); tb != NULL; tb = strtok_r(NULL, "/,;&", &sb)) {
            if (sched_code_equal(ta, tb)) {
                sched_copy(shared, cap, ta);
                return 1;
            }
        }
    }
    return 0;
}

static int id_listed(const char *list_text, const char *id)
{
    char work[SCHED_TEXT_LEN];
    char *tok;
    char *save = NULL;

    sched_copy(work, sizeof(work), list_text);
    for (tok = strtok_r(work, ";,", &save); tok != NULL; tok = strtok_r(NULL, ";,", &save)) {
        if (sched_code_equal(tok, id)) {
            return 1;
        }
    }
    return 0;
}

static void check_taught_lists(const course_list *courses, const faculty_list *faculty, diag_list *diags)
{
    size_t f;

    for (f = 0; f < faculty->count; f++) {
        const faculty_t *fac = &faculty->items[f];
        char work[SCHED_TEXT_LEN];
        char *tok;
        char *save = NULL;
        size_t c;

        if (fac->courses_taught[0] == '\0') {
            continue;
        }
        /* Every section the professor claims must appear in the schedule under that professor. */
        sched_copy(work, sizeof(work), fac->courses_taught);
        for (tok = strtok_r(work, ";,", &save); tok != NULL; tok = strtok_r(NULL, ";,", &save)) {
            char id[SCHED_CODE_LEN];
            int found_any = 0;
            int found_mine = 0;

            sched_copy(id, sizeof(id), tok);
            if (id[0] == '\0') {
                continue;
            }
            for (c = 0; c < courses->count; c++) {
                if (sched_code_equal(courses->items[c].id, id)) {
                    found_any = 1;
                    found_mine = courses->items[c].faculty_idx == (int)f;
                }
            }
            if (!found_any) {
                diag_add(diags, DIAG_WARN, "%s (%s) lists %s, which is not in the class schedule", fac->name,
                         fac->id, id);
            } else if (!found_mine) {
                diag_add(diags, DIAG_WARN, "%s (%s) lists %s, but the class schedule assigns it to someone else",
                         fac->name, fac->id, id);
            }
        }
        /* Every section assigned to the professor should be in their list. */
        for (c = 0; c < courses->count; c++) {
            const course_t *crs = &courses->items[c];

            if (crs->faculty_idx == (int)f && !id_listed(fac->courses_taught, crs->id)) {
                diag_add(diags, DIAG_WARN, "%s (%s) teaches %s per the class schedule but does not list it",
                         fac->name, fac->id, crs->id);
            }
        }
    }
}

void sched_check_conflicts(const course_list *courses, const faculty_list *faculty, diag_list *diags)
{
    size_t i;
    size_t j;

    for (i = 0; i < courses->count; i++) {
        for (j = i + 1; j < courses->count; j++) {
            const course_t *a = &courses->items[i];
            const course_t *b = &courses->items[j];
            char da[128];
            char db[128];
            char room[SCHED_ROOM_LEN];

            if (a->meeting != MEET_SCHEDULED || b->meeting != MEET_SCHEDULED) {
                continue;
            }
            if ((a->days & b->days) == 0 || !tu_overlaps(a->start, a->end, b->start, b->end)
                || !sched_sessions_overlap(a, b)) {
                continue;
            }
            describe_meeting(a, da, sizeof(da));
            describe_meeting(b, db, sizeof(db));
            if (a->faculty_idx >= 0 && a->faculty_idx == b->faculty_idx) {
                diag_add(diags, DIAG_ERROR, "Instructor %s is double-booked: %s overlaps %s",
                         faculty->items[a->faculty_idx].name, da, db);
            }
            if (rooms_share(a->room, b->room, room, sizeof(room))) {
                diag_add(diags, DIAG_ERROR, "Room %s is double-booked: %s (room %s) overlaps %s (room %s)", room, da,
                         a->room, db, b->room);
            }
        }
    }
    check_taught_lists(courses, faculty, diags);
}

/* ---------------------------------------------------------------------------------------------- */
/* Weekly grid helpers                                                                            */
/* ---------------------------------------------------------------------------------------------- */

static int slot_floor(int minutes)
{
    int s = minutes / SLOT_MIN;

    return s < 0 ? 0 : (s > SLOTS_PER_DAY ? SLOTS_PER_DAY : s);
}

static int slot_ceil(int minutes)
{
    int s = (minutes + SLOT_MIN - 1) / SLOT_MIN;

    return s < 0 ? 0 : (s > SLOTS_PER_DAY ? SLOTS_PER_DAY : s);
}

static void grid_mark(week_grid *g, int day, int start, int end, unsigned char value, int only_free)
{
    int s;

    for (s = slot_floor(start); s < slot_ceil(end); s++) {
        if (!only_free || g->cell[day][s] == CELL_FREE) {
            g->cell[day][s] = value;
        }
    }
}

static int grid_contains(const week_grid *g, int day, int start, int end, unsigned char value)
{
    int s;

    for (s = slot_floor(start); s < slot_ceil(end); s++) {
        if (g->cell[day][s] == value) {
            return 1;
        }
    }
    return 0;
}

/* Mark all of one professor's scheduled meetings (every session). Returns the days they teach. */
static daymask_t build_grid(week_grid *g, size_t fidx, const course_list *courses, int buffer_min)
{
    size_t c;
    int day;
    daymask_t teaching = 0;

    memset(g, 0, sizeof(*g));
    for (c = 0; c < courses->count; c++) {
        const course_t *crs = &courses->items[c];

        if (crs->faculty_idx != (int)fidx || crs->meeting != MEET_SCHEDULED) {
            continue;
        }
        teaching |= crs->days;
        for (day = 0; day < DAYS_PER_WEEK; day++) {
            if (crs->days & DAYMASK(day)) {
                grid_mark(g, day, crs->start, crs->end, CELL_CLASS, 0);
            }
        }
    }
    if (buffer_min > 0) {
        for (c = 0; c < courses->count; c++) {
            const course_t *crs = &courses->items[c];

            if (crs->faculty_idx != (int)fidx || crs->meeting != MEET_SCHEDULED) {
                continue;
            }
            for (day = 0; day < DAYS_PER_WEEK; day++) {
                if (crs->days & DAYMASK(day)) {
                    grid_mark(g, day, crs->start - buffer_min, crs->start, CELL_BUFFER, 1);
                    grid_mark(g, day, crs->end, crs->end + buffer_min, CELL_BUFFER, 1);
                }
            }
        }
    }
    return teaching;
}

static int is_class_like(unsigned char cell)
{
    return cell == CELL_CLASS || cell == CELL_BUFFER;
}

/* Minutes between [s0,s1) slots and the nearest CLASS/BUFFER slot that day; -1 if the day has none. */
static int gap_to_class(const week_grid *g, int day, int s0, int s1)
{
    int best = -1;
    int s;

    for (s = 0; s < SLOTS_PER_DAY; s++) {
        int d;

        if (!is_class_like(g->cell[day][s])) {
            continue;
        }
        d = (s < s0) ? (s0 - 1 - s) : (s >= s1 ? s - s1 : 0);
        if (best < 0 || d < best) {
            best = d;
        }
    }
    return best < 0 ? -1 : best * SLOT_MIN;
}

typedef struct {
    int found;
    int day;
    int start;
    int score;
} candidate;

/* Scan the week for the best FREE window of `len` minutes. */
static candidate find_best_block(const week_grid *g, const faculty_t *fac, daymask_t teaching, daymask_t preferred,
                                 const int *blocks_per_day, int len, int allow_non_preferred)
{
    candidate best = { 0, 0, 0, INT_MIN };
    int first = slot_ceil(fac->avail_start) * SLOT_MIN;
    int day;

    for (day = 0; day < DAYS_PER_WEEK; day++) {
        int is_pref = (preferred & DAYMASK(day)) != 0;
        int start;

        if (!is_pref && (!allow_non_preferred || day >= DAY_SAT)) {
            continue;
        }
        for (start = first; start + len <= fac->avail_end; start += SLOT_MIN) {
            int s0 = start / SLOT_MIN;
            int s1 = (start + len) / SLOT_MIN;
            int s;
            int is_free = 1;
            int adjacent;
            int gap;
            int score = 0;
            unsigned char before;
            unsigned char after;

            for (s = s0; s < s1; s++) {
                if (g->cell[day][s] != CELL_FREE) {
                    is_free = 0;
                    break;
                }
            }
            if (!is_free) {
                continue;
            }
            before = s0 > 0 ? g->cell[day][s0 - 1] : CELL_FREE;
            after = s1 < SLOTS_PER_DAY ? g->cell[day][s1] : CELL_FREE;
            adjacent = before != CELL_FREE || after != CELL_FREE;
            /* Free-standing blocks start on the hour or half hour for readability. */
            if (!adjacent && start % 30 != 0) {
                continue;
            }
            if (teaching & DAYMASK(day)) {
                score += SCORE_TEACHING_DAY;
            }
            if (adjacent) {
                score += SCORE_ADJACENT;
            }
            score += SCORE_SAME_DAY_BLOCK * blocks_per_day[day];
            gap = gap_to_class(g, day, s0, s1);
            if (gap > 0) {
                score += SCORE_PER_10MIN_GAP * (gap / 10);
            }
            if (!is_pref) {
                score += SCORE_NON_PREFERRED;
            }
            if (score > best.score) {
                best.found = 1;
                best.day = day;
                best.start = start;
                best.score = score;
            }
        }
    }
    return best;
}

static int push_block(oh_list *out, size_t fidx, int day, int start, int end, oh_source source)
{
    oh_block_t *b;
    size_t i;

    /* Extend a touching generated block on the same day instead of creating a second row. */
    if (source == OH_SOURCE_GENERATED) {
        for (i = 0; i < out->count; i++) {
            b = &out->items[i];
            if (b->faculty_idx != fidx || b->day != day || b->source != OH_SOURCE_GENERATED) {
                continue;
            }
            if (b->end == start) {
                b->end = end;
                return 0;
            }
            if (b->start == end) {
                b->start = start;
                return 0;
            }
        }
    }
    if (sched_reserve((void **)&out->items, &out->cap, out->count + 1, sizeof(oh_block_t)) != 0) {
        return -1;
    }
    b = &out->items[out->count++];
    b->faculty_idx = fidx;
    b->day = day;
    b->start = start;
    b->end = end;
    b->source = source;
    return 0;
}

static int compare_blocks(const void *pa, const void *pb)
{
    const oh_block_t *a = pa;
    const oh_block_t *b = pb;

    if (a->faculty_idx != b->faculty_idx) {
        return a->faculty_idx < b->faculty_idx ? -1 : 1;
    }
    if (a->day != b->day) {
        return a->day - b->day;
    }
    return a->start - b->start;
}

/* ---------------------------------------------------------------------------------------------- */
/* Public API                                                                                     */
/* ---------------------------------------------------------------------------------------------- */

int sched_section_count(const course_list *courses, size_t fidx)
{
    size_t c;
    int n = 0;

    for (c = 0; c < courses->count; c++) {
        if (courses->items[c].faculty_idx == (int)fidx) {
            n++;
        }
    }
    return n;
}

int sched_required_minutes(const faculty_t *fac, size_t fidx, const course_list *courses)
{
    int by_course;

    if (fac->oh_required_min >= 0) {
        return fac->oh_required_min;
    }
    by_course = sched_section_count(courses, fidx) * DEFAULT_OH_PER_COURSE_MIN;
    return by_course > DEFAULT_OH_MIN_WEEKLY_MIN ? by_course : DEFAULT_OH_MIN_WEEKLY_MIN;
}

int sched_assigned_minutes(const oh_list *hours, size_t faculty_idx)
{
    size_t i;
    int total = 0;

    for (i = 0; i < hours->count; i++) {
        if (hours->items[i].faculty_idx == faculty_idx) {
            total += hours->items[i].end - hours->items[i].start;
        }
    }
    return total;
}

int sched_teaches_only_online(const course_list *courses, size_t fidx)
{
    size_t c;
    int any = 0;

    for (c = 0; c < courses->count; c++) {
        const course_t *crs = &courses->items[c];

        if (crs->faculty_idx != (int)fidx) {
            continue;
        }
        any = 1;
        if (room_is_physical(crs->room)) {
            return 0;
        }
    }
    return any;
}

void sched_office_location(const faculty_t *fac, size_t fidx, const course_list *courses, char *buf, size_t cap)
{
    if (fac->office[0] != '\0') {
        snprintf(buf, cap, "%s", fac->office);
    } else if (sched_teaches_only_online(courses, fidx)) {
        snprintf(buf, cap, "Online (virtual)");
    } else {
        snprintf(buf, cap, "Office TBA");
    }
}

static sched_status assign_one(size_t fidx, const faculty_t *fac, const course_list *courses,
                               const sched_config *cfg, oh_list *out, diag_list *diags)
{
    week_grid grid;
    daymask_t teaching = build_grid(&grid, fidx, courses, cfg->buffer_min);
    daymask_t preferred = fac->pref_days != 0 ? fac->pref_days : (teaching != 0 ? teaching : DAYMASK_WEEKDAYS);
    int blocks_per_day[DAYS_PER_WEEK] = { 0 };
    int required = sched_required_minutes(fac, fidx, courses);
    int assigned = 0;
    size_t i;

    /* 1. Lecturer-supplied office hours: keep them unless they collide with a class. */
    for (i = 0; i < fac->fixed_count; i++) {
        const time_block_t *b = &fac->fixed[i];
        char when[64];

        tu_format_range(b->start, b->end, when, sizeof(when));
        if (grid_contains(&grid, b->day, b->start, b->end, CELL_CLASS)) {
            diag_add(diags, DIAG_WARN, "%s (%s): requested office hours %s %s overlap a class; dropped",
                     fac->name, fac->id, tu_day_name(b->day), when);
            continue;
        }
        if (grid_contains(&grid, b->day, b->start, b->end, CELL_OFFICE)) {
            diag_add(diags, DIAG_WARN, "%s (%s): requested office hours %s %s overlap another request; dropped",
                     fac->name, fac->id, tu_day_name(b->day), when);
            continue;
        }
        if (grid_contains(&grid, b->day, b->start, b->end, CELL_BUFFER)) {
            diag_add(diags, DIAG_INFO, "%s (%s): requested office hours %s %s are inside the %d-minute class "
                     "buffer; kept as requested", fac->name, fac->id, tu_day_name(b->day), when, cfg->buffer_min);
        }
        grid_mark(&grid, b->day, b->start, b->end, CELL_OFFICE, 0);
        if (push_block(out, fidx, b->day, b->start, b->end, OH_SOURCE_LECTURER) != 0) {
            return SCHED_ERR_MEM;
        }
        blocks_per_day[b->day]++;
        assigned += b->end - b->start;
    }

    /* 2. Generate blocks until the weekly requirement is met. */
    while (assigned < required) {
        int remaining = required - assigned;
        int len = remaining < fac->oh_block_min ? remaining : fac->oh_block_min;
        candidate best = { 0, 0, 0, INT_MIN };
        int pass;
        int found_len = 0;

        if (len < cfg->min_block_min) {
            len = cfg->min_block_min;
        }
        len = ((len + SLOT_MIN - 1) / SLOT_MIN) * SLOT_MIN;
        /* Pass 0: preferred days only. Pass 1: any weekday. Shrink the block if nothing fits. */
        for (pass = 0; pass < 2 && !best.found; pass++) {
            int try_len;

            for (try_len = len; try_len >= cfg->min_block_min; try_len -= SLOT_MIN) {
                best = find_best_block(&grid, fac, teaching, preferred, blocks_per_day, try_len, pass);
                if (best.found) {
                    found_len = try_len;
                    break;
                }
            }
        }
        if (!best.found) {
            diag_add(diags, DIAG_WARN, "%s (%s): only %d of %d required office-hour minutes fit the "
                     "availability window", fac->name, fac->id, assigned, required);
            break;
        }
        if ((preferred & DAYMASK(best.day)) == 0) {
            diag_add(diags, DIAG_INFO, "%s (%s): preferred days are full; placed office hours on %s", fac->name,
                     fac->id, tu_day_name(best.day));
        }
        grid_mark(&grid, best.day, best.start, best.start + found_len, CELL_OFFICE, 0);
        if (push_block(out, fidx, best.day, best.start, best.start + found_len, OH_SOURCE_GENERATED) != 0) {
            return SCHED_ERR_MEM;
        }
        blocks_per_day[best.day]++;
        assigned += found_len;
    }
    return SCHED_OK;
}

sched_status sched_assign_office_hours(const course_list *courses, const faculty_list *faculty,
                                       const sched_config *cfg, oh_list *out, diag_list *diags)
{
    size_t f;

    for (f = 0; f < faculty->count; f++) {
        sched_status st;

        if (sched_section_count(courses, f) == 0) {
            diag_add(diags, DIAG_INFO, "%s (%s) has no sections in the class schedule; no office hours assigned",
                     faculty->items[f].name, faculty->items[f].id);
            continue;
        }
        st = assign_one(f, &faculty->items[f], courses, cfg, out, diags);
        if (st != SCHED_OK) {
            return st;
        }
    }
    if (out->count > 1) {
        qsort(out->items, out->count, sizeof(oh_block_t), compare_blocks);
    }
    return SCHED_OK;
}
