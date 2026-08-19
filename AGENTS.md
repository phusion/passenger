This is the codebase for the Phusion Passenger application server. Mostly C++14 and Ruby.

CRITICALLY IMPORTANT, MUST READ FIRST:
- @./doc/DevHandbook.md — know what other development docs can be consulted
- @./doc/BasicArchitectureOverview.md — foundational knowledge that aids you in determining research directions

## Coding guidelines

### General

- Make the smallest changes to existing interfaces as possible. Be averse to rewriting things.
- Keep code readable and focused by splitting smaller or side concerns into functions.

### For C++

- Prefer using internal utility library.
  - Prefer oxt/system_calls.hpp wrappers (e.g. oxt::open) over direct syscalls. If no wrapper available, loop until no EINTR.
  - Consult Utils.h (general utils), IOUtils.h (I/O), FileTools/*.h (file operations).
  - Prefer FileDescriptor or safelyClose() over close().
- Mind security: see SecureTempFileHandling.md; prefer safeReadFile() for reading files.
