# Faculty Schedule Generator (`schedgen`)

**COSC 631 – Software Engineering · Midterm** · Carlson Cox

A C program reads **exactly two input files** (the department class schedule and a faculty information file) and
generates each professor's **teaching schedule** and **office-hours schedule**. All scheduling logic is in a
**static library**, `libschedule.a`, and a **Makefile** builds it, links it with the main program and generates
the output. Python scripts export the department spreadsheet to CSV and turn the C output into Word and Excel
documents.

**Midterm addition:** the system answers questions about the schedule and the course syllabi.

```
$ make ask Q="What classes does Dr Tankeh teach?"
```

| Part | Role |
|------|------|
| `scripts/gen_prolog.py` | **Python reads** the schedule data and the **course syllabi** (PDF, via `scripts/syllabus.py`) and **generates the facts and rules** for Prolog: `output/knowledge_base.pl` |
| `output/knowledge_base.pl` | **Prolog stores the knowledge**: part 1 rules (from `prolog/rules.pl`), part 2 facts |
| `scripts/ask.py` | Recognises the question, builds the Prolog query, prints the answer |
| SWI-Prolog via `pyswip` | **The interpreter**: `consult("knowledge_base.pl")`, then answers each query |

The full explanation of each part is in **`docs/report.pdf`** (§4 static libraries, §5 Makefile, §8 local vs.
Internet input files, §11 Python and Prolog).

---

## 1. Requirements

| Tool | Tested | Used for |
|------|--------|----------|
| GCC or Clang, GNU make, `ar` | gcc 13, clang 18, make 4.3 | Compiling, the static library, build automation |
| Python 3 | 3.12 | All Python scripts |
| `python-docx`, `openpyxl`, `pyswip`, `pypdf` | see `requirements.txt` | Word/Excel output, the Python ↔ Prolog bridge, reading the syllabus PDFs |
| SWI-Prolog | 9.0 (8.4.2 or newer) | The Prolog interpreter |

Runs on Linux, macOS or Windows (WSL). Python and SWI-Prolog must both be 64-bit.

## 2. Setup (once)

```bash
sudo apt-get install build-essential swi-prolog-nox   # macOS: brew install swi-prolog; Windows: use WSL
python3 -m pip install -r requirements.txt
```

If `pyswip` reports that it cannot find SWI-Prolog, set `SWI_HOME_DIR` to the `PLBASE` value printed by
`swipl --dump-runtime-variables`.

### Or use Docker (no local setup)

The image contains GCC, make, Python, SWI-Prolog and pyswip, already matched, and is built with the knowledge
base ready. Run these from this directory:

```bash
docker build -t schedgen .
docker run --rm schedgen "What classes does Dr Yang teach?"     # one question
docker run --rm -it schedgen                                    # interactive ('help', 'quit')
docker run --rm schedgen make test test-kb                      # every test
docker run --name sg schedgen make dist                         # the submission zip, then copy it out:
docker cp sg:/app/dist/Carlson_Cox_COSC631_Midterm.zip . && docker rm sg
```

Any make target works the same way (`docker run --rm schedgen make demo`). To use your own input files, mount the
workbook and/or the faculty file over the ones in the image and add `-B` so make rebuilds everything from them:

```bash
docker run --rm \
  -v "$PWD/data/CTEC_Fall_2026_Schedule.xlsx:/app/data/CTEC_Fall_2026_Schedule.xlsx:ro" \
  -v "$PWD/data/faculty.csv:/app/data/faculty.csv:ro" \
  schedgen make -B ask Q="Who teaches CTEC 350?"
```

## 3. Compile and run

Run from this directory. Each step builds whatever the previous one needs, so `make ask` alone also works.

```bash
make            # 1. compile src/lib/*.c -> build/libschedule.a, compile main.c, link build/schedgen
make run        # 2. run the C program on the two input files -> output/schedule.txt + CSV files
make docs       # 3. Python: output -> output/Faculty_Schedules.docx and .xlsx
make kb         # 4. Python: output + syllabi -> Prolog knowledge base output/knowledge_base.pl
make ask Q="What is Dr Stone's schedule?"   # 5. ask a question (make ask alone = interactive)
```

`make run` reports **"conflicts found in the input data"**. That is expected: the CTEC schedule has two
double-booked rooms, one double-booked instructor and one impossible meeting time. Every schedule is still
written and the problems are listed in `output/diagnostics.txt` (the program exits with status 3; `make run
STRICT=1` turns that into a failure).

To check everything:

```bash
make test       # C: 104 unit checks on the library + end-to-end tests on broken input files
make test-kb    # 57 Prolog rule tests, 31 Python tests (incl. both syllabi), the "Dr Tankeh" question end to end
make demo       # answers every question in data/questions.txt -> output/qa_demo.txt
```

To build the submission package:

```bash
make dist       # clean build + every test + all output -> dist/Carlson_Cox_COSC631_Midterm.zip
```

## 4. Asking questions

```bash
make ask Q="What classes does Dr Stone teach?"     # one question
make ask                                           # interactive: ask> ...  ('help' for examples, 'quit')
python3 scripts/ask.py "Who teaches CTEC 350?"     # without make (after make kb)
swipl output/knowledge_base.pl                     # Prolog directly: ?- lecturer_of('CTEC 350', Name).
```

Names, course codes, titles, days and rooms are looked up in the data; none is built into the program.

| Question type | Examples |
|---------------|----------|
| Full schedule (sample layout) | What is Dr Stone's schedule? · Schedule for Ruth Agada |
| A lecturer's classes | What classes does Dr Stone teach? · What does Agada teach on Monday? |
| Who teaches a course | Who teaches CTEC 350? · Who teaches Python? · Who is the instructor for UNIX System Administration? |
| Office hours | What are Ruth Agada's office hours? · Which office hours are on Wednesday? |
| Days and rooms | Which classes meet on Tuesday? · Which classes are in room 108? · Who teaches on Friday? |
| Time and place of a course | Where does CTEC 435 meet? · When does CTEC 410.170 meet? |
| Availability | Is Dr Stone available on Monday at 3 PM? |
| Workload, conflicts | How many sections does Carl Dixon teach? · Are there any conflicts? · List all lecturers |
| Any Prolog query | `?- teaches_course(Id, 'CTEC 302').` |

From the **course syllabi** (`data/syllabi/`):

| Question type | Examples |
|---------------|----------|
| Course, instructor, contact | What is COSC 631 about? · Who teaches COSC 631? · How do I contact Dr Yang? |
| Prerequisites | What are the prerequisites for COSC 528? · Can I take COSC 631 if I completed COSC 531? |
| Grading | How is COSC 528 graded? · How much is the final worth in COSC 631? · What grade is 85 in COSC 631? |
| Texts | What textbook does COSC 528 use? |
| Weekly outline | What is the outline for COSC 631? · What is covered in week 6 of COSC 528? · When is quicksort covered? · When is the midterm in COSC 631? |
| Dates, office hours | When is the last day to add/drop? · What are the office hours for COSC 631? · Which syllabi do you have? |

Syllabus instructors also join the schedule questions: "Which office hours are on Thursday?" lists Dr. Bo Yang
(Thu 3:00 – 5:00 PM, from the COSC 631 syllabus), and "What is Jim Gil de Lamadrid's schedule?" shows his
office hours labelled with COSC 428/528.

A misspelled name gets a suggestion ("Did you mean: Dr. D. B. Stone?"), and a name that is not in the data gets
a clear "no lecturer named ..." answer.

### The Dr Tankeh example

The CTEC Fall 2026 schedule has no Dr Tankeh. To demonstrate the question from the assignment,
`tests/data/cs_class_schedule.csv` and `tests/data/cs_faculty.csv` describe a small Computer Science department
whose Dr Tankeh entries (COSC 631, 369 and 330, and the office hours) come from the sample in the
specification. They are **input data**; the program is unchanged. Run the system on them:

```bash
make
build/schedgen -o out_cs tests/data/cs_class_schedule.csv tests/data/cs_faculty.csv
python3 scripts/gen_prolog.py --input out_cs --faculty tests/data/cs_faculty.csv -o out_cs/knowledge_base.pl
python3 scripts/ask.py --kb out_cs/knowledge_base.pl "What is Dr Tankeh's schedule?"
```

```
Dr. Tankeh (CS01) | Computer Science (COSC) | Office: CS 214

CLASS SCHEDULE
  Course                             Times                                   Location
  COSC 330 (Systems Programming I)   Monday & Wednesday 9:30 AM to 10:45 AM  CS 311
  COSC 369 (Software Engineering I)  Tuesday & Thursday 9:30 AM to 10:45 AM  CS 108
  COSC 631 (Database Management)     Wednesday 5:00 PM to 7:30 PM            CS 313

OFFICE HOURS
  Monday:    10:45 AM – 1:00 PM (COSC 330)
  Tuesday:   10:45 AM – 1:00 PM (COSC 369)
  Wednesday: 10:45 AM – 1:00 PM (COSC 330)
             & 3:00 – 5:00 PM (COSC 631)
  Thursday:  10:45 AM – 1:00 PM (COSC 369)
  Friday:
NOTE: Instructors are not to be interrupted during class times.
```

More answers: `sample_output/qa_demo_cs.txt` (CS department) and `sample_output/qa_demo.txt` (CTEC schedule and
the syllabi).

## 5. Input files

Both input files are **stored locally** in `data/`, so the system runs on a lab computer without network
access and always produces the same output from the same files (report §8 explains the choice).

| File | Contents |
|------|----------|
| `data/CTEC_Fall_2026_Schedule.xlsx` | The department's class schedule, exactly as distributed |
| `data/class_schedule.csv` | **Input 1**: CSV export of the workbook (`make csv` refreshes it when the `.xlsx` changes) |
| `data/faculty.csv` | **Input 2**: one row per lecturer: name spellings, department, office, courses, office-hours policy, lecturer-supplied office hours |
| `data/syllabi/*.pdf` | **Course syllabi** read by Python for the knowledge base: COSC 428/528 (Fall 2026) and COSC 631 (Spring 2022) |

The schedule has 56 sections and 25 instructors in two 7-week sessions (7R1, 7R2). Office, email and phone are
blank in `faculty.csv` because the department data does not include them; filling them in replaces "Office TBA".
`make fetch SCHEDULE_URL=... FACULTY_URL=...` can download newer copies, but it is not used by default.

**Syllabi.** The C program's two input files are unchanged. The syllabi feed the knowledge base only: they hold
course facts (instructor, office hours, prerequisites, texts, grading, weekly outline, dates) but no meeting
days, times or rooms, so there is nothing in them to schedule. To add a course, drop its syllabus PDF into
`data/syllabi/` and run `make kb`. The reader finds fields by the labels syllabi commonly use ("Instructor",
"Office Hours", "Prerequisite", "Outline", ...) in any order or layout; it needs a text PDF, not a scan.

## 6. Output (`output/`)

| File | Made by | Contents |
|------|---------|----------|
| `schedule.txt` | C | Per professor: CLASS SCHEDULE, OFFICE HOURS, the class-time note, weekly grid |
| `classes.csv`, `office_hours.csv`, `faculty_summary.csv` | C | The schedule as data, read by the Python scripts |
| `diagnostics.txt` | C | Every error, warning and note about the input data |
| `Faculty_Schedules.docx`, `.xlsx` | Python | The schedules as Word and Excel documents |
| `knowledge_base.pl` | Python | The Prolog knowledge base: rules + 759 facts (602 from the schedule, 157 from the syllabi) |
| `qa_demo.txt` | Python + Prolog | Answers to `data/questions.txt` |

A copy of every output from a reference run is in `sample_output/`.

## 7. Makefile targets

| Target | Does |
|--------|------|
| `make` (`all`) | Compile the library sources, archive `build/libschedule.a`, compile and link `build/schedgen` |
| `lib` | Build only the static library |
| `inspect` | Show the library's members (`ar -t`) and exported functions (`nm`) |
| `csv` | Export the `.xlsx` to `data/class_schedule.csv` (only if the workbook changed) |
| `run` | Run the program on the two input files (only if the program or an input changed) |
| `docs` | Word and Excel documents |
| `kb` | Prolog knowledge base `output/knowledge_base.pl` (schedule + syllabi) |
| `ask` | Answer `Q="..."`, or interactive without `Q` |
| `demo` | Answer every question in `data/questions.txt` |
| `test`, `test-kb` | C tests; Prolog + Python tests and the end-to-end question |
| `dist` | Clean build, every test, all output, then the submission zip `dist/Carlson_Cox_COSC631_Midterm.zip` |
| `clean`, `distclean` | Remove `build/`; also `output/`, `.venv/` and `dist/` |

`make` rebuilds only what is out of date: editing `data/faculty.csv` and running `make ask` re-runs the C
program, regenerates the knowledge base and answers from the new data.

Running the C program by itself:

```
build/schedgen [-o OUTDIR] [-b BUFFER_MIN] [-q] <class_schedule.csv> <faculty.csv>
```

Exit status: `0` success, `1` not exactly two input files, `2` input file unreadable, `3` schedule written but
the data has conflicts, `4` output error.

## 8. Project layout

```
schedgen/
├── Makefile  README.md  requirements.txt  Dockerfile  docker-entrypoint.sh
├── include/         8 headers (schedule.h includes the rest)
├── src/main.c       command-line driver, the only C file outside the library
├── src/lib/         common.c csv.c diag.c loader.c report.c scheduler.c timeutil.c  -> build/libschedule.a
├── data/            the two input files (+ the source .xlsx), syllabi/ (course syllabus PDFs), questions.txt
├── scripts/         xlsx_to_csv.py  export_docs.py  gen_prolog.py  syllabus.py  ask.py
├── prolog/          rules.pl: the Prolog rules included in the knowledge base
├── tests/           C unit tests, Prolog tests (test_rules.pl), Python tests (test_kb.py), test input files
├── sample_output/   every output file from a reference run
└── docs/            report.pdf (the report) and report.md (its source)
```

The static library `build/libschedule.a` is produced by `make`.
