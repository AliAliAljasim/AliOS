#pragma once

/* ── path_resolve ────────────────────────────────────────────────────────────
 *
 * Compute the absolute path obtained by resolving `input` relative to `base`.
 * Writes the result into `out` (must be at least 64 bytes).
 *
 * Rules:
 *   - If input starts with '/', it is used as-is (absolute).
 *   - Otherwise input is joined onto base.
 *   - "." components are discarded.
 *   - ".." components remove the last path element.
 *   - The result always starts with '/' and never has a trailing '/'.
 *   - The root is represented as "/".
 *
 * Example:
 *   path_resolve("/bin",  "echo",    out) → "/bin/echo"
 *   path_resolve("/home", "../bin",  out) → "/bin"
 *   path_resolve("/",     "/etc/x",  out) → "/etc/x"
 */
static inline void path_resolve(const char *base, const char *input, char *out)
{
    /* Component stack — up to 16 path segments. */
    char  comp[16][32];
    int   nc = 0;

    /* Helper: push a component string of known length. */
    #define PUSH(s, n)                                           \
        do {                                                     \
            if (nc < 16) {                                       \
                int _i = 0;                                      \
                while (_i < (n) && _i < 31) { comp[nc][_i] = (s)[_i]; _i++; } \
                comp[nc][_i] = '\0'; nc++;                       \
            }                                                    \
        } while (0)

    /* Walk a '/' separated string and update the component stack. */
    #define WALK(s)                                              \
        do {                                                     \
            const char *_p = (s);                               \
            while (*_p) {                                        \
                while (*_p == '/') _p++;                         \
                if (!*_p) break;                                 \
                const char *_c = _p;                             \
                while (*_p && *_p != '/') _p++;                  \
                int _n = (int)(_p - _c);                         \
                if (_n == 1 && _c[0] == '.') { /* skip */ }      \
                else if (_n == 2 && _c[0] == '.' && _c[1] == '.') \
                    { if (nc > 0) nc--; }                        \
                else PUSH(_c, _n);                               \
            }                                                    \
        } while (0)

    if (input[0] == '/') {
        /* Absolute: ignore base. */
        WALK(input);
    } else {
        /* Relative: start from base, then apply input. */
        WALK(base);
        WALK(input);
    }

    #undef PUSH
    #undef WALK

    if (nc == 0) {
        out[0] = '/'; out[1] = '\0';
        return;
    }

    int oi = 0;
    for (int i = 0; i < nc; i++) {
        out[oi++] = '/';
        for (int j = 0; comp[i][j] && oi < 63; j++)
            out[oi++] = comp[i][j];
    }
    out[oi] = '\0';
}

/* path_parent — write the parent directory of `path` into `out` (64 bytes). */
static inline void path_parent(const char *path, char *out)
{
    int last = 0;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last = i;
    if (last == 0) { out[0] = '/'; out[1] = '\0'; return; }
    for (int i = 0; i < last; i++) out[i] = path[i];
    out[last] = '\0';
}

/* path_basename — return pointer to the last component of path. */
static inline const char *path_basename(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') last = p + 1;
    return last;
}
