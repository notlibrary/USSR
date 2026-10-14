# Ural tests

Planned integration cases:

1. Initialize an empty `ussr.boxes` database.
2. Add/remove trusted server URLs.
3. Install a local package fixture.
4. Reject an invalid `box.uno` name/version.
5. Reject archive hash mismatch.
6. Reject archive path traversal.
7. Uninstall only files recorded as package-owned.
8. Update transaction rollback when extraction fails.
9. Preserve package DB on interrupted update.
10. Verify `install(code): "ukraine russia"` reports per-package results.

These are test specifications; the native command bridge is not yet integrated.
