/*
 * didimdwiki - allocation and logging shims.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "xmalloc.h"

void *
xmalloc(size_t len)
{
	void		*ptr;

	if (len == 0)
		fatal("xmalloc: zero-sized allocation");

	if ((ptr = malloc(len)) == NULL)
		fatal("xmalloc: out of memory (%zu bytes)", len);

	return (ptr);
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void		*ptr;

	if (nmemb == 0 || size == 0)
		fatal("xcalloc: zero-sized allocation");

	if ((ptr = calloc(nmemb, size)) == NULL)
		fatal("xcalloc: out of memory (%zu * %zu)", nmemb, size);

	return (ptr);
}

void *
xrealloc(void *ptr, size_t len)
{
	void		*nptr;

	if (len == 0)
		fatal("xrealloc: zero-sized allocation");

	if ((nptr = realloc(ptr, len)) == NULL)
		fatal("xrealloc: out of memory (%zu bytes)", len);

	return (nptr);
}

char *
xstrdup(const char *str)
{
	char		*copy;

	if ((copy = strdup(str)) == NULL)
		fatal("xstrdup: out of memory");

	return (copy);
}

APP_NORETURN void
fatal(const char *fmt, ...)
{
	va_list		args;

	fflush(stdout);
	fprintf(stderr, "%s: fatal: ", APP_NAME);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
	exit(1);
}

void
app_log(int level, const char *fmt, ...)
{
	va_list		 args;
	const char	*tag;
	char		 stamp[32];
	struct tm	 tm;
	time_t		 now;

	switch (level) {
	case LOG_ERR:
		tag = "error";
		break;
	case LOG_WARNING:
		tag = "warn";
		break;
	default:
		tag = "info";
		break;
	}

	now = time(NULL);
	if (gmtime_r(&now, &tm) != NULL &&
	    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm) > 0)
		fprintf(stderr, "%s %s [%s] ", stamp, APP_NAME, tag);
	else
		fprintf(stderr, "%s [%s] ", APP_NAME, tag);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}
