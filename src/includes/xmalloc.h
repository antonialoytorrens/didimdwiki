/*
 * didimdwiki - allocation and logging shims.
 *
 * These abort on out-of-memory rather than returning NULL. Every call site
 * in the model layer relies on that, which is what keeps those files free of
 * allocation error handling.
 */

#ifndef DIDIMDWIKI_XMALLOC_H
#define DIDIMDWIKI_XMALLOC_H

#include <stddef.h>

void	*xmalloc(size_t);
void	*xcalloc(size_t, size_t);
void	*xrealloc(void *, size_t);
char	*xstrdup(const char *);

/*
 * The C11 keyword where available, the GCC attribute otherwise.
 *
 * Worth spelling out because the obvious `__attribute__((noreturn))` is a
 * trap under C11: <stdnoreturn.h> defines `noreturn` as a macro, so in any
 * translation unit that has included it -- Unity does -- the attribute name
 * expands to `_Noreturn`, GCC does not recognise that as an attribute, and
 * the whole thing is silently dropped.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define APP_NORETURN	_Noreturn
#elif defined(__GNUC__)
#define APP_NORETURN	__attribute__((noreturn))
#else
#define APP_NORETURN
#endif

APP_NORETURN void	 fatal(const char *, ...)
			    __attribute__((format (printf, 1, 2)));

/* Severity levels mirror syslog's. */
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_INFO	6

void	 app_log(int, const char *, ...)
	    __attribute__((format (printf, 2, 3)));

#endif /* !DIDIMDWIKI_XMALLOC_H */
