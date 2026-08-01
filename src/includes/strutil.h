/*
 * didimdwiki - small string helpers used by the model layer.
 */

#ifndef DIDIMDWIKI_STRUTIL_H
#define DIDIMDWIKI_STRUTIL_H

#include <stddef.h>

#define STR_OK		1
#define STR_ERROR	0

/*
 * Copies src into dst, always NUL-terminating, and returns strlen(src).
 * Callers detect truncation with `>= sizeof(dst)`. Provided rather than
 * using the platform strlcpy so the semantics are the same everywhere
 * (glibc only grew one in 2.38).
 */
size_t	str_lcpy(char *, const char *, size_t);

/*
 * Field splitter. Empty fields are skipped rather than stored, and `out` is
 * NULL-terminated; the TSV readers in models/ depend on both. Destructive:
 * separators in `input` are overwritten. Returns the number stored.
 */
int	str_split(char *, const char *, char **, size_t);

/* strtoll with range checking. *err is STR_OK or STR_ERROR. */
long long str_tonum(const char *, int, long long, long long, int *);

/* Case-insensitive substring search. */
char	*str_casestr(const char *, const char *);

/* Hex-encodes len bytes into out, which must hold (len * 2) + 1 bytes. */
void	str_hex(const unsigned char *, size_t, char *, size_t);

/* Length-independent comparison; both strings must be NUL-terminated. */
int	str_timingsafe_equal(const char *, const char *);

#endif /* !DIDIMDWIKI_STRUTIL_H */
