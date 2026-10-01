/*
 * timeutil.h - Parsing and formatting of clock times and weekday sets.
 *
 * Times are stored as minutes after midnight (0..1439).
 * Weekday sets are bit masks: bit 0 = Monday ... bit 6 = Sunday.
 */
#ifndef SCHED_TIMEUTIL_H
#define SCHED_TIMEUTIL_H

#include <stddef.h>

typedef unsigned int daymask_t;

enum {
    DAY_MON = 0,
    DAY_TUE,
    DAY_WED,
    DAY_THU,
    DAY_FRI,
    DAY_SAT,
    DAY_SUN,
    DAYS_PER_WEEK
};

#define DAYMASK(day)       (1u << (unsigned int)(day))
#define DAYMASK_WEEKDAYS   0x1Fu
#define MINUTES_PER_DAY    1440

/*
 * Parse "9:30 AM", "9:30am", "4:00:00 PM", "5 PM", "17:00" or "0930" into minutes after midnight.
 * Returns 0 on success, -1 on malformed input.
 */
int tu_parse_time(const char *text, int *minutes);

/*
 * Parse a weekday set: "MWF", "TR", "TTh", "Mon/Wed", "Tuesday & Thursday", "M,W".
 * Returns 0 on success (mask non-empty), -1 on malformed input.
 */
int tu_parse_days(const char *text, daymask_t *mask);

/* "9:30 AM" */
void tu_format_time(int minutes, char *buf, size_t cap);

/* "9:30 AM to 10:45 AM" */
void tu_format_range(int start, int end, char *buf, size_t cap);

/* "Tuesday & Thursday", "Monday, Wednesday & Friday" */
void tu_format_days_long(daymask_t mask, char *buf, size_t cap);

/* "TR", "MWF" */
void tu_format_days_short(daymask_t mask, char *buf, size_t cap);

const char *tu_day_name(int day);
const char *tu_day_abbr3(int day);

/* Number of days set in the mask. */
int tu_day_count(daymask_t mask);

/* Index of the first day in the mask, or DAYS_PER_WEEK if empty. */
int tu_first_day(daymask_t mask);

/* 1 if half-open intervals [s1,e1) and [s2,e2) intersect. */
int tu_overlaps(int s1, int e1, int s2, int e2);

#endif /* SCHED_TIMEUTIL_H */
