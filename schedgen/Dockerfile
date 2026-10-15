# =================================================================================================
# schedgen in a container: GCC, make, Python 3.12, SWI-Prolog and pyswip, preinstalled and matched.
#
#   docker build -t schedgen .                                   (from this directory)
#   docker run --rm schedgen "What classes does Dr Yang teach?"  one question
#   docker run --rm -it schedgen                                 interactive questions
#   docker run --rm schedgen make test test-kb                   any make target
#   docker run --name sg schedgen make dist                      submission zip; then copy it out:
#   docker cp sg:/app/dist/Carlson_Cox_COSC631_Midterm.zip . && docker rm sg
#
# Ubuntu 24.04 is the same base as the CI runners: GCC 13, Python 3.12 and SWI-Prolog 9.0 from apt.
# =================================================================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    LANG=C.UTF-8 \
    VIRTUAL_ENV=/opt/venv \
    PATH=/opt/venv/bin:$PATH

# Toolchain + the Prolog interpreter. poppler-utils gives pdftotext for the syllabus reader (pypdf is the
# fallback, so it is optional, but it is the extractor the tests were written against).
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential make python3 python3-venv swi-prolog-nox poppler-utils \
 && rm -rf /var/lib/apt/lists/*

# Python packages in a virtual environment (Ubuntu 24.04 blocks pip into the system Python).
# requirements.txt is copied on its own first so this layer is cached until the requirements change.
RUN python3 -m venv "$VIRTUAL_ENV"
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

# Run as the image's unprivileged 'ubuntu' user (UID 1000); it owns /app so make can write build/, output/
# and dist/.
WORKDIR /app
COPY --chown=ubuntu:ubuntu . .
RUN chown ubuntu:ubuntu /app 
USER ubuntu

# Build the static library and program, run them, and generate the knowledge base at image build time,
# so the first question answers instantly. Exit status 3 (conflicts in the data) is handled by the Makefile.
RUN make && make kb

ENTRYPOINT ["/app/docker-entrypoint.sh"]
CMD []
