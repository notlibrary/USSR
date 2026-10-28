#!/bin/sh
# The quick test set CI runs on every pull request and push to main: the
# ussr pass of tests/run_all.sh with its main suites only. Left out, and
# run by tests/run_all.sh in the release workflow: the russia.su parity
# pass, the GUI tests, and the Mbl editor and UUASM suites.
#
# Run from the repository root:
#
#     sh tests/run_default.sh [ussr]
#
# It runs the suite runners for the language (.ru bytecode, vectors,
# errors, exit statuses, REPL, shell) and every standalone script against
# tests/expected/. Exit status: 0 when all of them pass.
USSR_SUITES=${USSR_SUITES-"ru vectors errors exit repl shell"}
USSR_SKIP_GUI=1
export USSR_SUITES USSR_SKIP_GUI
exec sh tests/run_all.sh --native "$@"
