/*
 * timeutil.c - Time and weekday parsing/formatting.
 */
#include "timeutil.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *const DAY_LONG[DAYS_PER_WEEK] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};

static const char *const DAY_ABBR3[DAYS_PER_WEEK] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

static const char DAY_LETTER[DAYS_PER_WEEK] = { 'M', 'T', 'W', 'R', 'F', 'S', 'U' };

/* Day tokens, longest first so "Thursday" wins over "Th" and "Th" wins over "T". */
typedef struct {
    const char *token;
    int day;
} day_token;

static const day_token DAY_TOKENS[] = {
    { "wednesday", DAY_WED }, { "thursday", DAY_THU }, { "saturday", DAY_SAT },
    { "tuesday", DAY_TUE },   { "monday", DAY_MON },   { "friday", DAY_FRI },
    { "sunday", DAY_SUN },    { "thurs", DAY_THU },    { "tues", DAY_TUE },
    { "thur", DAY_THU },      { "mon", DAY_MON },      { "tue", DAY_TUE },
    { "wed", DAY_WED },       { "thu", DAY_THU },      { "fri", DAY_FRI },
    { "sat", DAY_SAT },       { "sun", DAY_SUN },      { "th", DAY_THU },
    { "tu", DAY_TUE },        { "sa", DAY_SAT },       { "su", DAY_SUN },
    { "m", DAY_MON },         { "t", DAY_TUE },        { "w", DAY_WED },
    { "r", DAY_THU },         { "f", DAY_FRI },        { "s", DAY_SAT },
    { "u", DAY_SUN }
};

static int prefix_icase(const char *text, const char *token)
{
    while (*token != '\0') {
        if (tolower((unsigned char)*text) != *token) {
            return 0;
        }
        text++;
        token++;
    }
    return 1;
}

int tu_parse_time(const char *text, int *minutes)
{
    const char *p = text;
    int hour = 0;
    int minute = 0;
    int digits = 0;
    int meridiem = 0; /* 0 = none, 1 = AM, 2 = PM */

    if (text == NULL || minutes == NULL) {
        return -1;
    }
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    while (isdigit((unsigned char)*p) && digits < 4) {
        hour = hour * 10 + (*p - '0');
        digits++;
        p++;
    }
    if (isdigit((unsigned char)*p)) {
        return -1;
    }
    if (digits > 2) {
        /* Compact military form: "0930" or "930". */
        minute = hour % 100;
        hour /= 100;
    } else if (*p == ':' || *p == '.') {
        p++;
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) {
            return -1;
        }
        minute = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        /* Optional seconds, as written by Excel CSV export ("4:00:00 PM"); must be :00. */
        if (*p == ':') {
            if (!isdigit((unsigned char)p[1]) || !isdigit((unsigned char)p[2])) {
                return -1;
            }
            if (p[1] != '0' || p[2] != '0') {
                return -1;
            }
            p += 3;
        }
    }
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        char c = (char)tolower((unsigned char)*p);

        if (c == 'a') {
            meridiem = 1;
        } else if (c == 'p') {
            meridiem = 2;
        } else {
            return -1;
        }
        p++;
        if (*p == '.') {
            p++;
        }
        if (tolower((unsigned char)*p) == 'm') {
            p++;
        }
        if (*p == '.') {
            p++;
        }
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '\0') {
            return -1;
        }
    }
    if (minute < 0 || minute > 59) {
        return -1;
    }
    if (meridiem != 0) {
        if (hour < 1 || hour > 12) {
            return -1;
        }
        hour %= 12;
        if (meridiem == 2) {
            hour += 12;
        }
    } else if (hour > 23) {
        return -1;
    }
    *minutes = hour * 60 + minute;
    return 0;
}

int tu_parse_days(const char *text, daymask_t *mask)
{
    const char *p = text;
    daymask_t result = 0;

    if (text == NULL || mask == NULL) {
        return -1;
    }
    while (*p != '\0') {
        size_t i;
        int matched = 0;

        if (isspace((unsigned char)*p) || *p == '/' || *p == ',' || *p == '&' || *p == '-' || *p == '+'
            || *p == ';') {
            p++;
            continue;
        }
        if (prefix_icase(p, "and") && !isalpha((unsigned char)p[3])) {
            p += 3;
            continue;
        }
        for (i = 0; i < sizeof(DAY_TOKENS) / sizeof(DAY_TOKENS[0]); i++) {
            if (prefix_icase(p, DAY_TOKENS[i].token)) {
                result |= DAYMASK(DAY_TOKENS[i].day);
                p += strlen(DAY_TOKENS[i].token);
                matched = 1;
                break;
            }
        }
        if (!matched) {
            return -1;
        }
    }
    if (result == 0) {
        return -1;
    }
    *mask = result;
    return 0;
}

void tu_format_time(int minutes, char *buf, size_t cap)
{
    int hour = minutes / 60;
    int minute = minutes % 60;
    int hour12 = hour % 12;

    if (hour12 == 0) {
        hour12 = 12;
    }
    snprintf(buf, cap, "%d:%02d %s", hour12, minute, hour < 12 ? "AM" : "PM");
}

void tu_format_range(int start, int end, char *buf, size_t cap)
{
    char s[16];
    char e[16];

    tu_format_time(start, s, sizeof(s));
    tu_format_time(end, e, sizeof(e));
    snprintf(buf, cap, "%s to %s", s, e);
}

void tu_format_days_long(daymask_t mask, char *buf, size_t cap)
{
    int total = tu_day_count(mask);
    int written = 0;
    int day;
    size_t used = 0;

    if (cap == 0) {
        return;
    }
    buf[0] = '\0';
    for (day = 0; day < DAYS_PER_WEEK; day++) {
        const char *sep = "";
        int n;

        if ((mask & DAYMASK(day)) == 0) {
            continue;
        }
        if (written > 0) {
            sep = (written == total - 1) ? " & " : ", ";
        }
        n = snprintf(buf + used, cap - used, "%s%s", sep, DAY_LONG[day]);
        if (n < 0 || (size_t)n >= cap - used) {
            return; /* truncated; buffer remains NUL-terminated */
        }
        used += (size_t)n;
        written++;
    }
}

void tu_format_days_short(daymask_t mask, char *buf, size_t cap)
{
    int day;
    size_t used = 0;

    if (cap == 0) {
        return;
    }
    for (day = 0; day < DAYS_PER_WEEK && used + 1 < cap; day++) {
        if (mask & DAYMASK(day)) {
            buf[used++] = DAY_LETTER[day];
        }
    }
    buf[used] = '\0';
}

const char *tu_day_name(int day)
{
    return (day >= 0 && day < DAYS_PER_WEEK) ? DAY_LONG[day] : "?";
}

const char *tu_day_abbr3(int day)
{
    return (day >= 0 && day < DAYS_PER_WEEK) ? DAY_ABBR3[day] : "?";
}

int tu_day_count(daymask_t mask)
{
    int n = 0;
    int day;

    for (day = 0; day < DAYS_PER_WEEK; day++) {
        if (mask & DAYMASK(day)) {
            n++;
        }
    }
    return n;
}

int tu_first_day(daymask_t mask)
{
    int day;

    for (day = 0; day < DAYS_PER_WEEK; day++) {
        if (mask & DAYMASK(day)) {
            return day;
        }
    }
    return DAYS_PER_WEEK;
}

int tu_overlaps(int s1, int e1, int s2, int e2)
{
    return s1 < e2 && s2 < e1;
}
