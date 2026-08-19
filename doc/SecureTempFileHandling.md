# Secure handling of temp files

Always use unpredictable filenames for temp files. Failing to do so makes us vulnerable to symlink attacks, TOCTOU race conditions, file squatting, or even information disclosure and privilege escalation.

Always use a tempfile creation strategy that atomically:
1. Finds a free filename, *and*,
2. Reserves that filename (failing if it already exists), *and*,
3. Restrict permissions to only the intended user (e.g. mode 0600),
Doing these non-atomically makes us vulnerable to TOCTU race conditions.

Implementation tips:
- Use getSystemTempDir()
- Use mkstemp() for single, regular files. Don't use it for non-regular files such as Unix sockets; use a temp dir instead.
- Use mkdtemp() for creating a temp dir or for storing temp non-regular files.
- mktemp() is bad.
