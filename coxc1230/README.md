# Faculty Schedule Generator (`schedgen`)

COSC 631 – Software Engineering, Second Project.

A C program that reads **exactly two input files**, the department class schedule and a faculty information
file, and generates each professor's **teaching schedule** and **office-hours schedule**. All scheduling logic
lives in a **static library** (`libschedule.a`). A **Makefile** builds the library, links it with the main
program, runs it, and calls Python scripts to (1) export the department's Excel schedule to CSV and
(2) convert the C output into **Word (.docx)** and **Excel (.xlsx)** documents.

Input data: **CTEC Fall 2026 schedule**, 56 sections, 25 instructors, 7-week sessions 7R1 / 7R2.

---

## 1. Requirements

| Tool | Version tested | Needed for |
|------|----------------|------------|
| GCC (or Clang) | gcc 13 / clang 18 | compiling C11 |
| GNU make | 4.x (3.81+ works) | build automation |
| `ar` (binutils) | 2.42 | creating the static library |
| Python 3 | 3.10+ | `.xlsx` → CSV export and `make docs` |
| `python-docx`, `openpyxl` | see `requirements.txt` | Python scripts |

Linux / macOS / WSL. The program uses POSIX `getopt`, `mkdir` and `strtok_r`.

## 2. Quick start

```bash
python3 -m pip install -r requirements.txt   # once (or: make venv && make docs PYTHON=.venv/bin/python)
make            # compile -> build/libschedule.a -> link build/schedgen
make run        # export .xlsx to CSV if it changed, then generate output/schedule.txt + CSV files
make docs       # convert output to output/Faculty_Schedules.docx and .xlsx
make test       # unit tests + end-to-end conflict test
```

`data/class_schedule.csv` is shipped already exported, so `make` and `make run` work without Python. Python is
needed only when the `.xlsx` changes or for `make docs`.

> **The department schedule contains real conflicts** (2 double-booked rooms, 1 double-booked instructor, 1
> impossible meeting time). The program still writes every schedule, lists the problems in
> `output/diagnostics.txt` and on the first page of the Word document, and exits with status 3. `make run`
> treats status 3 as "written with data issues" and continues. Use `make run STRICT=1` to make it fail instead.

## 3. Makefile targets

| Target | What it does |
|--------|--------------|
| `all` (default) | Compiles `src/lib/*.c` to objects, archives them into `build/libschedule.a`, compiles `src/main.c`, links `build/schedgen` against the library |
| `lib` | Builds only the static library |
| `csv` | Exports `data/CTEC_Fall_2026_Schedule.xlsx` → `data/class_schedule.csv` (only if the workbook changed) |
| `run` | Runs `build/schedgen` on the two input files (only if the program or an input changed) |
| `docs` | Runs `scripts/export_docs.py` to produce `.docx` and `.xlsx` (only if the output changed) |
| `test` | Builds `build/test_schedule` (linked against the same library) and runs it, then runs the program on deliberately broken inputs in `tests/data/` and checks each problem is caught |
| `inspect` | Lists the object files inside the library (`ar -t`) and its exported functions (`nm`) |
| `fetch` | **Optional.** Downloads the inputs from URLs: `make fetch SCHEDULE_URL=... FACULTY_URL=...` |
| `clean` / `distclean` | Remove `build/` / also `output/` and `.venv/` |

Variables: `make run BUFFER=10`, `make run STRICT=1`,
`make run SCHEDULE_XLSX=data/Spring_2027.xlsx`, `make run FACULTY=other/faculty.csv`.

## 4. Running the program directly

```
build/schedgen [-o OUTDIR] [-b BUFFER_MIN] [-q] <class_schedule.csv> <faculty.csv>

  -o OUTDIR      output directory (default: output)
  -b BUFFER_MIN  protected minutes before/after each class, 0-60 (default: 15)
  -q             quiet: print only errors
  -h / -V        help / version
```

Exit status: `0` success · `1` usage error (e.g. not exactly two files) · `2` input file cannot be read ·
`3` schedule written but the data contains conflicts · `4` output error.

## 5. Input files (stored locally in `data/`)

### 5.1 Input 1: class schedule, the department spreadsheet

`data/CTEC_Fall_2026_Schedule.xlsx` is used exactly as the department distributes it. `make` exports it to
`data/class_schedule.csv` with `scripts/xlsx_to_csv.py`. The export is faithful (the same as Excel's
"Save As → CSV") and changes no values. The C program reads that CSV in the department's native column layout:

| Column (header, any order) | Used as |
|---|---|
| `ID` | Section ID, e.g. `CTEC 350.170` → course `CTEC 350`, section `170` |
| `Course Title` | Title; a trailing `(7R1)` / `(7R2)` is read as the **session** (first / second 7 weeks) |
| `CR` | Credits |
| `Days` | `MW`, `TR`, `MWF` (R = Thursday); blank = asynchronous or TBA |
| `Begin`, `End` | `4:00 PM`, `16:00` or `4:00:00 PM` |
| `Room` | `108`, `201/112` (split room), `Online`, `Dept` (the last two are not physical rooms) |
| `Instructor*` | Any header starting with "Instructor" (e.g. `Instructor 2025`), matched against the faculty file |
| `Enroll #` | Enrollment |

Headers are matched ignoring case, spaces and punctuation, so `CR ` and `End ` (trailing spaces) work.

### 5.2 Input 2: `data/faculty.csv`, professor information

| Column | Meaning / default |
|--------|-------------------|
| `faculty_id`, `name` | Required |
| `aliases` | Every spelling of the name used in the schedule, `;`-separated, e.g. `Bemley;Bemley, J;Bemley, Jesse` |
| `department`, `office`, `email`, `phone` | Printed on the schedule. Blank office → `Office TBA` (or `Online (virtual)` if all sections are online) |
| `courses_taught` | Section IDs, cross-checked against the schedule |
| `oh_hours_per_week` | Blank → policy default: max(3 h, 1 h per section) |
| `oh_block_minutes` | Length of each generated block, default 60 |
| `preferred_days` | Blank → the days the professor teaches |
| `available_start`, `available_end` | Window for generated office hours, default 9:00 AM–9:00 PM |
| `fixed_office_hours` | Hours the lecturer supplies, e.g. `"MW 3:00 PM-4:00 PM; T 1:00-2:00 PM"` |

**Office, email and phone are blank in the shipped file.** They are not in the department schedule; fill them in
to replace "Office TBA".

## 6. How the source data is handled

| Situation in the CTEC schedule | What the program does |
|---|---|
| Same instructor, same time, **different sessions** (e.g. CTEC 350.170 7R1 and 350.180 7R2) | Not a conflict. Conflicts are checked only within one session |
| Office hours across sessions | One schedule for the whole semester that avoids **both** sessions' classes |
| Name spelled differently (`Bemley` / `Bemley, J` / `Bemley, Jesse`; `Eugene Harris` / `Eugene T. Harris`) | Resolved through `aliases` in the faculty file |
| End before start with an AM end time (`12:00 PM`–`3:15 AM`, `7:00 PM`–`9:50 AM`) | End moved to PM, **WARNING**, marked `*` in the schedule |
| Impossible time (`5:00 PM`–`5:00 PM`, CTEC 711.555) | Listed as **TBA**, **ERROR** |
| No days and "On-line Asynchronous Course" | Asynchronous section, listed with no meeting time |
| `201/112` | Checked against both rooms 201 and 112 |
| `Online`, `Dept` | Not physical rooms, excluded from room-conflict checks |

## 7. Output (`output/`)

| File | Produced by | Contents |
|------|-------------|----------|
| `schedule.txt` | C program | Per professor: CLASS SCHEDULE (with session), OFFICE HOURS, the class-time note, weekly grid |
| `faculty_summary.csv`, `classes.csv`, `office_hours.csv` | C program | Data for the Python exporter |
| `diagnostics.txt` | C program | Every error / warning / info message |
| `Faculty_Schedules.docx` | Python | Page 1: data issues. Then one page per professor, in the sample's table style |
| `Faculty_Schedules.xlsx` | Python | Summary, Classes, Office Hours, Data Issues, one sheet per professor |

## 8. How office hours are assigned

1. Each professor's week is a grid of 15-minute slots. Classes from **both** sessions are marked CLASS, and
   `BUFFER` minutes on each side are protected. Office hours never touch class time.
2. Lecturer-supplied hours are accepted unless they overlap a class. Conflicting hours are dropped with a
   warning.
3. The remaining hours are placed greedily. Each candidate block is scored: +30 on a teaching day, +20 when
   touching a class or an existing office-hours block, −50 per block already on that day, −1 per 10 minutes
   away from the day's classes, −100 on a non-preferred day. Touching blocks merge, so the result is
   "Monday 1:45–3:45 PM", not two separate rows.

## 9. Project layout

```
schedgen/
├── Makefile  README.md  requirements.txt
├── include/            public headers (schedule.h = umbrella header)
├── src/main.c          command-line driver (the only C file not in the library)
├── src/lib/            common.c csv.c diag.c loader.c report.c scheduler.c timeutil.c -> libschedule.a
├── data/               CTEC_Fall_2026_Schedule.xlsx, class_schedule.csv (export), faculty.csv
├── scripts/            xlsx_to_csv.py, export_docs.py
├── tests/              unit tests + deliberately broken inputs
├── sample_output/      output from a reference run
└── docs/               project report (PDF + Markdown source)
```
