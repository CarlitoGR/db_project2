/*
 * main.c - Command-line driver for the Faculty Schedule Generator.
 *
 * Usage: schedgen [-o OUTDIR] [-b BUFFER_MIN] [-q] <class_schedule.csv> <faculty.csv>
 *
 * The program takes exactly two input files, validates them, assigns office hours and writes:
 *   OUTDIR/schedule.txt         formatted per-professor schedules
 *   OUTDIR/faculty_summary.csv  one row per professor
 *   OUTDIR/classes.csv          one row per class meeting pattern
 *   OUTDIR/office_hours.csv     one row per office-hours block (merged across days)
 *   OUTDIR/diagnostics.txt      every error / warning / info message
 *
 * Exit status: 0 success, 1 usage error, 2 input file error, 3 schedule written but contains errors,
 *              4 output/memory error.
 */
#include "schedule.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    EXIT_OK = 0,
    EXIT_USAGE = 1,
    EXIT_INPUT = 2,
    EXIT_CONFLICTS = 3,
    EXIT_OUTPUT = 4
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [-o OUTDIR] [-b BUFFER_MIN] [-q] <class_schedule.csv> <faculty.csv>\n"
            "       %s -h | -V\n\n"
            "Generates professor teaching and office-hours schedules from exactly two input files.\n\n"
            "Options:\n"
            "  -o OUTDIR      output directory (default: output)\n"
            "  -b BUFFER_MIN  protected minutes before/after each class, 0-60 (default: 15)\n"
            "  -q             quiet: print only errors to stderr\n"
            "  -h             show this help\n"
            "  -V             show version\n",
            prog, prog);
}

static int ensure_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(path, 0755);
}

static int build_path(char *buf, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(buf, cap, "%s/%s", dir, name);

    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

int main(int argc, char **argv)
{
    const char *outdir = "output";
    int quiet = 0;
    int opt;
    sched_config cfg;
    course_list courses;
    faculty_list faculty;
    oh_list hours;
    diag_list diags;
    sched_status st_courses;
    sched_status st_faculty;
    char path_txt[1024];
    char path_diag[1024];
    int exit_code = EXIT_OK;

    sched_config_default(&cfg);
    while ((opt = getopt(argc, argv, "o:b:qhV")) != -1) {
        switch (opt) {
        case 'o':
            outdir = optarg;
            break;
        case 'b': {
            char *endp;
            long v = strtol(optarg, &endp, 10);

            if (*endp != '\0' || v < 0 || v > 60) {
                fprintf(stderr, "error: -b expects 0-60 minutes, got '%s'\n", optarg);
                return EXIT_USAGE;
            }
            cfg.buffer_min = (int)v;
            break;
        }
        case 'q':
            quiet = 1;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return EXIT_OK;
        case 'V':
            printf("schedgen %s\n", SCHED_VERSION);
            return EXIT_OK;
        default:
            usage(stderr, argv[0]);
            return EXIT_USAGE;
        }
    }
    if (argc - optind != 2) {
        fprintf(stderr, "error: exactly two input files are required (class schedule, faculty)\n\n");
        usage(stderr, argv[0]);
        return EXIT_USAGE;
    }

    course_list_init(&courses);
    faculty_list_init(&faculty);
    oh_list_init(&hours);
    diag_init(&diags);

    /* Load both files before stopping so the user sees every input problem in one run. */
    st_courses = load_courses(argv[optind], &courses, &diags);
    st_faculty = load_faculty(argv[optind + 1], &faculty, &diags);
    if (st_courses != SCHED_OK || st_faculty != SCHED_OK) {
        diag_print(&diags, stderr, DIAG_ERROR);
        exit_code = EXIT_INPUT;
        goto cleanup;
    }

    link_instructors(&courses, &faculty, &diags);
    sched_check_conflicts(&courses, &faculty, &diags);
    if (sched_assign_office_hours(&courses, &faculty, &cfg, &hours, &diags) != SCHED_OK) {
        fprintf(stderr, "error: out of memory while assigning office hours\n");
        exit_code = EXIT_OUTPUT;
        goto cleanup;
    }

    if (ensure_dir(outdir) != 0) {
        fprintf(stderr, "error: cannot create output directory '%s': %s\n", outdir, strerror(errno));
        exit_code = EXIT_OUTPUT;
        goto cleanup;
    }
    if (build_path(path_txt, sizeof(path_txt), outdir, "schedule.txt") != 0
        || build_path(path_diag, sizeof(path_diag), outdir, "diagnostics.txt") != 0) {
        fprintf(stderr, "error: output directory path is too long\n");
        exit_code = EXIT_OUTPUT;
        goto cleanup;
    }
    {
        report_context ctx;

        ctx.syllabus_path = argv[optind];
        ctx.faculty_path = argv[optind + 1];
        ctx.config = &cfg;
        if (report_write_text(path_txt, &ctx, &courses, &faculty, &hours, &diags) != SCHED_OK
            || report_write_csv(outdir, &courses, &faculty, &hours) != SCHED_OK
            || report_write_diagnostics(path_diag, &diags) != SCHED_OK) {
            fprintf(stderr, "error: failed writing output files in '%s': %s\n", outdir, strerror(errno));
            exit_code = EXIT_OUTPUT;
            goto cleanup;
        }
    }

    diag_print(&diags, stderr, quiet ? DIAG_ERROR : DIAG_WARN);
    if (!quiet) {
        printf("schedgen: %zu faculty, %zu sections, %zu office-hour blocks\n", faculty.count, courses.count,
               hours.count);
        printf("schedgen: wrote %s/{schedule.txt,faculty_summary.csv,classes.csv,office_hours.csv,diagnostics.txt}\n",
               outdir);
        printf("schedgen: %zu error(s), %zu warning(s)\n", diag_count(&diags, DIAG_ERROR),
               diag_count(&diags, DIAG_WARN));
    }
    if (diag_count(&diags, DIAG_ERROR) > 0) {
        exit_code = EXIT_CONFLICTS;
    }

cleanup:
    course_list_free(&courses);
    faculty_list_free(&faculty);
    oh_list_free(&hours);
    diag_free(&diags);
    return exit_code;
}
