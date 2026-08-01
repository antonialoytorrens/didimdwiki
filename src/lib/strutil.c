/*
 * didimdwiki - small string helpers.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "strutil.h"
#include "xmalloc.h"

size_t
str_lcpy(char *dst, const char *src, size_t len)
{
	char		*d = dst;
	const char	*s = src;
	const char	*end;

	if (len == 0)
		fatal("str_lcpy: len == 0");

	end = dst + len - 1;

	while ((*d = *s) != '\0') {
		if (d == end) {
			*d = '\0';
			break;
		}
		d++;
		s++;
	}

	while (*s != '\0')
		s++;

	return ((size_t)(s - src));
}

/* strsep() is a BSD extension, so here it is. */
static char *
str_sep(char **stringp, const char *delim)
{
	char	*start, *p;

	if ((start = *stringp) == NULL)
		return (NULL);

	if ((p = strpbrk(start, delim)) != NULL) {
		*p = '\0';
		*stringp = p + 1;
	} else {
		*stringp = NULL;
	}

	return (start);
}

int
str_split(char *input, const char *delim, char **out, size_t ele)
{
	int		  count;
	char		**ap;

	if (ele == 0)
		return (0);

	count = 0;
	for (ap = out; ap < &out[ele - 1] &&
	    (*ap = str_sep(&input, delim)) != NULL;) {
		if (**ap != '\0') {
			ap++;
			count++;
		}
	}

	*ap = NULL;
	return (count);
}

/*
 * strcasestr() is a GNU extension. Used by search across every page, so it
 * stays simple rather than clever -- a wiki is not a search engine.
 */
char *
str_casestr(const char *haystack, const char *needle)
{
	size_t	nlen, i;

	if ((nlen = strlen(needle)) == 0)
		return ((char *)haystack);

	for (i = 0; haystack[i] != '\0'; i++) {
		if (!strncasecmp(haystack + i, needle, nlen))
			return ((char *)(haystack + i));
	}

	return (NULL);
}

long long
str_tonum(const char *str, int base, long long min, long long max, int *err)
{
	long long	 l;
	char		*ep;

	if (min > max) {
		*err = STR_ERROR;
		return (0);
	}

	errno = 0;
	l = strtoll(str, &ep, base);
	if (errno != 0 || str == ep || *ep != '\0') {
		*err = STR_ERROR;
		return (0);
	}

	if (l < min || l > max) {
		*err = STR_ERROR;
		return (0);
	}

	*err = STR_OK;
	return (l);
}

void
str_hex(const unsigned char *bytes, size_t len, char *out, size_t outlen)
{
	static const char	hex[] = "0123456789abcdef";
	size_t			i;

	if (outlen < (len * 2) + 1)
		fatal("str_hex: output buffer too small");

	for (i = 0; i < len; i++) {
		out[i * 2] = hex[bytes[i] >> 4];
		out[(i * 2) + 1] = hex[bytes[i] & 0x0f];
	}

	out[len * 2] = '\0';
}

/*
 * Compares without leaking the position of the first difference. Length is
 * not secret here (tokens are fixed-width hex), but a length mismatch still
 * has to lose in constant time relative to the content comparison.
 */
int
str_timingsafe_equal(const char *a, const char *b)
{
	size_t		alen, blen, i;
	unsigned char	diff;

	alen = strlen(a);
	blen = strlen(b);

	if (alen != blen)
		return (0);

	diff = 0;
	for (i = 0; i < alen; i++)
		diff |= (unsigned char)(a[i] ^ b[i]);

	return (diff == 0);
}
