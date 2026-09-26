#ifndef OPENOS_PATH_H
#define OPENOS_PATH_H

// Tiny absolute-path helpers for the unified filesystem. Paths look like
// "/", "/docs", "/docs/readme.txt". No allocation, callers pass buffers.

#define PATH_MAX 64

// Turn (cwd, arg) into an absolute path in out. Handles "/", "..", "." and
// relative names. arg may be "" (out = cwd).
void path_resolve(const char *cwd, const char *arg, char *out);

// Parent directory of path ("/" is its own parent).
void path_parent(const char *path, char *out);

// Last component ("/docs/a.txt" -> "a.txt", "/" -> "").
const char *path_basename(const char *path);

// Length of the string, kernel-style (no libc).
int path_len(const char *s);

#endif
