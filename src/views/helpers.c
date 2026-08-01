/*
 * didimdwiki - escaping and formatting helpers.
 */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "strutil.h"
#include "xmalloc.h"
#include "helpers.h"

static const char	hexdigits[] = "0123456789ABCDEF";

void
html_escape(struct wbuf *buf, const char *src, size_t len)
{
	size_t		i;

	for (i = 0; i < len; i++) {
		switch (src[i]) {
		case '<':
			buf_lit(buf, "&lt;");
			break;
		case '>':
			buf_lit(buf, "&gt;");
			break;
		case '&':
			buf_lit(buf, "&amp;");
			break;
		case '"':
			buf_lit(buf, "&quot;");
			break;
		case '\'':
			buf_lit(buf, "&#39;");
			break;
		default:
			wbuf_append(buf, &src[i], 1);
			break;
		}
	}
}

/* Same as html_escape; kept separate so attribute contexts read clearly. */

void
xml_escape(struct wbuf *buf, const char *src, size_t len)
{
	size_t		i;

	for (i = 0; i < len; i++) {
		/* Strip control bytes: they are illegal in XML 1.0. */
		if ((unsigned char)src[i] < 0x20 && src[i] != '\n' &&
		    src[i] != '\t' && src[i] != '\r')
			continue;

		switch (src[i]) {
		case '<':
			buf_lit(buf, "&lt;");
			break;
		case '>':
			buf_lit(buf, "&gt;");
			break;
		case '&':
			buf_lit(buf, "&amp;");
			break;
		case '"':
			buf_lit(buf, "&quot;");
			break;
		default:
			wbuf_append(buf, &src[i], 1);
			break;
		}
	}
}

void
url_encode(struct wbuf *buf, const char *src, size_t len)
{
	size_t		i;
	unsigned char	c;
	char		esc[3];

	for (i = 0; i < len; i++) {
		c = (unsigned char)src[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~') {
			wbuf_append(buf, &src[i], 1);
			continue;
		}

		esc[0] = '%';
		esc[1] = hexdigits[(c >> 4) & 0x0f];
		esc[2] = hexdigits[c & 0x0f];
		wbuf_append(buf, esc, sizeof(esc));
	}
}

char *
html_escape_str(const char *src)
{
	char		*out;
	struct wbuf	buf;

	wbuf_init(&buf, strlen(src) + 16);
	html_escape(&buf, src, strlen(src));
	out = wbuf_stringify(&buf, NULL);

	return (out);	/* buf.data is handed to the caller, freed with free() */
}

char *
url_encode_str(const char *src)
{
	char		*out;
	struct wbuf	buf;

	wbuf_init(&buf, strlen(src) * 3 + 1);
	url_encode(&buf, src, strlen(src));
	out = wbuf_stringify(&buf, NULL);

	return (out);
}

void
fmt_date(time_t when, char *out, size_t len)
{
	struct tm	tm;

	if (localtime_r(&when, &tm) == NULL) {
		str_lcpy(out, "-", len);
		return;
	}

	if (strftime(out, len, "%Y-%m-%d %H:%M", &tm) == 0)
		str_lcpy(out, "-", len);
}

void
fmt_rfc822(time_t when, char *out, size_t len)
{
	struct tm	tm;

	if (gmtime_r(&when, &tm) == NULL) {
		str_lcpy(out, "-", len);
		return;
	}

	/* RSS wants an English locale, so the day and month are not localised. */
	if (strftime(out, len, "%a, %d %b %Y %H:%M:%S GMT", &tm) == 0)
		str_lcpy(out, "-", len);
}

void
fmt_size(off_t size, char *out, size_t len)
{
	double		v = (double)size;

	if (size < 1024) {
		snprintf(out, len, "%lld B", (long long)size);
	} else if (size < 1024 * 1024) {
		snprintf(out, len, "%.1f KB", v / 1024.0);
	} else if (size < 1024LL * 1024 * 1024) {
		snprintf(out, len, "%.1f MB", v / (1024.0 * 1024.0));
	} else {
		snprintf(out, len, "%.1f GB", v / (1024.0 * 1024.0 * 1024.0));
	}
}
