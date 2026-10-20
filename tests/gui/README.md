# GUI tests

These scripts need a GLFW-enabled `ussr` and a display. They create Petersburg
windows (most never show them), feed input through `inject`, and print one
`PASS` or `FAIL` line per check. Run them from the repository root:

    ussr tests/gui/petersburg_timer_test.su
    ussr tests/gui/tver_test.su
    ussr tests/gui/tula_test.su

A run is clean when no line starts with `FAIL`.
