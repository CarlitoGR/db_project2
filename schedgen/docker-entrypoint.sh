#!/bin/sh
# Container entry point.
#   (no arguments)              interactive questions
#   make <targets> / bash / ... run that command
#   anything else               answer it as one question: docker run --rm schedgen "Who teaches CTEC 350?"
set -e

if [ "$#" -eq 0 ]; then
    exec make ask
fi

case "$1" in
    make | bash | sh | python3 | swipl | ./build/schedgen | build/schedgen)
        exec "$@"
        ;;
    *)
        # Bring the knowledge base up to date (a no-op unless mounted inputs changed), then pass the question
        # to ask.py as a single argument. Going around make keeps quotes and '$' in the question intact.
        make --no-print-directory -s kb >/dev/null
        exec python3 scripts/ask.py --kb output/knowledge_base.pl "$*"
        ;;
esac
