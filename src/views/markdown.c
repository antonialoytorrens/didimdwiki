/*
 * didimdwiki - markdown rendering on top of md4c.
 */

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "strutil.h"
#include "xmalloc.h"
#include "helpers.h"

#include <md4c-html.h>

/* MD_FLAG_NOHTML escapes raw HTML in page source instead of executing it. */
#define MD_PARSER_FLAGS							\
	(MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |	\
	 MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_WIKILINKS |		\
	 MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_NOHTML)

struct md_sink {
	struct wbuf	*buf;
	int		flags;
};

static void	md_output(const MD_CHAR *, MD_SIZE, void *);
static void	md_rewrite_wikilinks(struct wbuf *, const char *, size_t);
static void	md_wikilink_href(struct wbuf *, const char *, size_t);
static void	md_camelcase(struct wbuf *, const char *, size_t);
static int	md_is_camelcase(const char *, size_t);

static void
md_output(const MD_CHAR *text, MD_SIZE size, void *udata)
{
	struct md_sink	*sink = udata;

	wbuf_append(sink->buf, text, size);
}

void
markdown_render(const void *src, size_t len, struct wbuf *out, int flags)
{
	struct md_sink	sink;
	struct wbuf	*raw, *linked;

	raw = wbuf_alloc(len + 512);
	sink.buf = raw;
	sink.flags = flags;

	if (md_html(src, (MD_SIZE)len, md_output, &sink,
	    MD_PARSER_FLAGS, 0) != 0) {
		wbuf_free(raw);
		wbuf_append(out,
		    "<p class=\"error\">Failed to render markdown.</p>", 46);
		return;
	}

	if (flags & MD_RENDER_CAMELCASE) {
		linked = wbuf_alloc(raw->offset + 512);
		md_rewrite_wikilinks(linked,
		    (const char *)raw->data, raw->offset);
		wbuf_free(raw);
		md_camelcase(out, (const char *)linked->data, linked->offset);
		wbuf_free(linked);
	} else {
		md_rewrite_wikilinks(out, (const char *)raw->data, raw->offset);
		wbuf_free(raw);
	}
}

/*
 * Upstream md4c-html renders MD_SPAN_WIKILINK as an inert
 * <x-wikilink data-target="TARGET">...</x-wikilink> - no browser resolves
 * that. The parser (unpatched) is what decides a span is a real wikilink
 * rather than literal text (e.g. `[[Foo]]` inside a code span never becomes
 * one), so rewriting the tag here, after rendering, is exactly as safe as
 * doing it inside the renderer, and lets md4c itself stay pristine upstream.
 */
static void
md_rewrite_wikilinks(struct wbuf *out, const char *html, size_t len)
{
	static const char	open_tag[] = "<x-wikilink data-target=\"";
	static const char	close_tag[] = "</x-wikilink>";
	static const char	anchor_open[] = "<a class=\"wikilink\" href=\"/wiki/";
	size_t			open_len, close_len;
	size_t			i, tstart, tend;

	open_len = sizeof(open_tag) - 1;
	close_len = sizeof(close_tag) - 1;
	i = 0;

	while (i < len) {
		if (i + open_len <= len &&
		    !memcmp(html + i, open_tag, open_len)) {
			tstart = i + open_len;
			tend = tstart;
			while (tend < len && html[tend] != '"')
				tend++;

			wbuf_append(out, anchor_open,
			    sizeof(anchor_open) - 1);
			md_wikilink_href(out, html + tstart, tend - tstart);
			wbuf_append(out, "\">", 2);

			i = tend;
			if (i < len && html[i] == '"')
				i++;
			if (i < len && html[i] == '>')
				i++;
			continue;
		}

		if (i + close_len <= len &&
		    !memcmp(html + i, close_tag, close_len)) {
			wbuf_append(out, "</a>", 4);
			i += close_len;
			continue;
		}

		wbuf_append(out, html + i, 1);
		i++;
	}
}

/*
 * The captured target is HTML-escaped; undo that, then percent-escape it
 * the way md4c-html.c's render_url_escaped() does for other span hrefs.
 */
static void
md_wikilink_href(struct wbuf *out, const char *target, size_t len)
{
	static const char	safe[] = "~-_.+!*(),%#@?=;:/,+$";
	static const char	hex[] = "0123456789ABCDEF";
	size_t			i;
	unsigned char		c;
	char			enc[3];

	for (i = 0; i < len; i++) {
		c = (unsigned char)target[i];

		if (c == '&') {
			if (!strncmp(target + i, "&amp;", 5)) {
				c = '&';
				i += 4;
			} else if (!strncmp(target + i, "&lt;", 4)) {
				c = '<';
				i += 3;
			} else if (!strncmp(target + i, "&gt;", 4)) {
				c = '>';
				i += 3;
			} else if (!strncmp(target + i, "&quot;", 6)) {
				c = '"';
				i += 5;
			}
		}

		if (isalnum(c) || strchr(safe, c) != NULL) {
			wbuf_append(out, &c, 1);
		} else if (c == '&') {
			wbuf_append(out, "&amp;", 5);
		} else {
			enc[0] = '%';
			enc[1] = hex[(c >> 4) & 0xf];
			enc[2] = hex[c & 0xf];
			wbuf_append(out, enc, 3);
		}
	}
}

/*
 * CamelCase auto-linking, off by default. Runs over rendered HTML and skips
 * tags plus the contents of <a>, <code> and <pre>, so it never rewrites a
 * link target or a code sample.
 */
static void
md_camelcase(struct wbuf *out, const char *html, size_t len)
{
	size_t		i, start, wlen;
	int		skip;
	const char	*p;

	i = 0;
	skip = 0;

	while (i < len) {
		if (html[i] == '<') {
			p = html + i;

			if (!strncasecmp(p, "<a", 2) ||
			    !strncasecmp(p, "<code", 5) ||
			    !strncasecmp(p, "<pre", 4))
				skip++;
			else if (!strncasecmp(p, "</a", 3) ||
			    !strncasecmp(p, "</code", 6) ||
			    !strncasecmp(p, "</pre", 5)) {
				if (skip > 0)
					skip--;
			}

			start = i;
			while (i < len && html[i] != '>')
				i++;
			if (i < len)
				i++;

			wbuf_append(out, html + start, i - start);
			continue;
		}

		if (skip > 0 || !isupper((unsigned char)html[i])) {
			wbuf_append(out, html + i, 1);
			i++;
			continue;
		}

		start = i;
		while (i < len && (isalnum((unsigned char)html[i])))
			i++;
		wlen = i - start;

		if (md_is_camelcase(html + start, wlen)) {
			wbuf_appendf(out, "<a class=\"wikilink\" "
			    "href=\"/wiki/%.*s\">%.*s</a>",
			    (int)wlen, html + start, (int)wlen, html + start);
		} else {
			wbuf_append(out, html + start, wlen);
		}
	}
}

/* Two or more capitals with lowercase between, and not all-caps. */
static int
md_is_camelcase(const char *word, size_t len)
{
	size_t		i;
	int		caps, lower, inner_cap;

	if (len < 3 || len >= NAME_MAX_LEN)
		return (0);

	caps = 0;
	lower = 0;
	inner_cap = 0;

	for (i = 0; i < len; i++) {
		if (isupper((unsigned char)word[i])) {
			caps++;
			if (i > 0 && lower > 0)
				inner_cap = 1;
		} else if (islower((unsigned char)word[i])) {
			lower++;
		} else {
			return (0);
		}
	}

	return (caps >= 2 && lower > 0 && inner_cap);
}

/*
 * Plain-text excerpt for search results and RSS: strips markdown punctuation
 * rather than rendering, which is cheaper and safe to escape afterwards.
 */
void
markdown_excerpt(const char *src, size_t len, char *out, size_t outlen)
{
	size_t		i, o;
	int		space;
	unsigned char	c;

	o = 0;
	space = 1;

	for (i = 0; i < len && o < outlen - 1; i++) {
		c = (unsigned char)src[i];

		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';

		if (c == ' ') {
			/* Also collapses runs left behind by stripped markup. */
			if (space)
				continue;
			space = 1;
			out[o++] = ' ';
			continue;
		}

		if (strchr("#*_`>[]()|~", c) != NULL)
			continue;

		if (c < 0x20 || c == 0x7f)
			continue;

		space = 0;
		out[o++] = (char)c;
	}

	while (o > 0 && out[o - 1] == ' ')
		o--;

	out[o] = '\0';
}
