ukraine.su cross-platform socket/scheduler alpha

This package contains a separate socket API, POSIX and Windows implementations,
and scheduler/event-queue integration for USSR.

Apply by copying the included src/, tests/, docs/, and ussr-ubuntu.mk paths over
the corresponding paths in your USSR checkout. Back up your tree first.

Build on Linux with the project's normal command, adding src/socket_posix.c to
the link inputs. The supplied ussr-ubuntu.mk selects socket_posix.c on POSIX and
socket_win32.c plus ws2_32 and process_win32.c on Windows.

Tests:
  ussr tests/socket_smoke.su
  ussr tests/socket_echo.su
Then connect to 127.0.0.1:18991 and send text. The server should echo it.

Verified in this environment: Linux/POSIX build, socket smoke test, and TCP echo
integration test. The Windows backend is implemented but was not compiled here
because a Windows/MinGW toolchain is unavailable. Please run the Windows build
before treating this as release-ready.

The socket API returns small integer handles managed by the library, not native
OS descriptors. No fork/exec is used to service network connections; readiness
waits are integrated with the existing scheduler. Existing external-process
support remains untouched apart from selecting the platform's process backend
in ussr-ubuntu.mk.
