# Faculty Schedule Generator (`schedgen`)

**COSC 631 – Software Engineering · Midterm Project** · Carlson Cox

A C program that generates each professor's teaching and office-hours schedule from two input files, built
around a static library and a Makefile. The midterm adds a Prolog knowledge base: Python reads the schedule and
the course syllabi and generates Prolog facts and rules, and SWI-Prolog answers questions such as
*"What classes does Dr Tankeh teach?"* or *"Which office hours are on Wednesday?"*.

| What | Where |
|------|-------|
| The project: source, Makefile, inputs, scripts, Prolog rules, tests | [`schedgen/`](schedgen/) |
| How to compile, run and ask questions | [`schedgen/README.md`](schedgen/README.md) |
| Report (static libraries, Makefile, input files, Python + Prolog) | [`schedgen/docs/report.pdf`](schedgen/docs/report.pdf) |
| Output from a reference run | [`schedgen/sample_output/`](schedgen/sample_output/) |
| Project 2 report and assignment (earlier submission) | [`archive/project2/`](archive/project2/) |

## Quick start

```bash
cd schedgen
python3 -m pip install -r requirements.txt     # plus SWI-Prolog: sudo apt-get install swi-prolog-nox
make                                           # build the static library and the program
make ask Q="What classes does Dr Stone teach?"
```

## Submission package

```bash
cd schedgen
make dist        # clean build, every test, all output regenerated -> dist/Carlson_Cox_COSC631_Midterm.zip
```

The zip contains the C source and headers, the static library `build/libschedule.a` built from that source, the
Makefile, both input files and the two syllabi, the Python scripts, the Prolog rules, the tests, the sample
output from the same run, `README.md` and `docs/report.pdf`.
