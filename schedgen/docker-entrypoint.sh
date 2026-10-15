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
        exec make ask Q="$*"
        ;;
esac
