/*
 * didimdwiki - line diff.
 * Plain LCS over lines: page-sized inputs, so O(n*m) is fine, with a cap that
 * degrades to a whole-file replacement rather than allocating unboundedly.
 */

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "xmalloc.h"
#include "models.h"
#include "helpers.h"

#define DIFF_MAX_LINES	4000

struct lines {
	char		**data;
	size_t		*len;
	size_t		count;
};

static void	lines_split(const char *, size_t, struct lines *);
static void	lines_free(struct lines *);
static void	diff_emit(struct wbuf *, const char *, const char *,
		    size_t);

static void
lines_split(const char *src, size_t len, struct lines *out)
{
	size_t		i, start, cap;

	cap = 128;
	out->data = xcalloc(cap, sizeof(*out->data));
	out->len = xcalloc(cap, sizeof(*out->len));
	out->count = 0;

	start = 0;

	for (i = 0; i <= len; i++) {
		if (i != len && src[i] != '\n')
			continue;

		if (out->count == cap) {
			cap *= 2;
			out->data = xrealloc(out->data,
			    cap * sizeof(*out->data));
			out->len = xrealloc(out->len,
			    cap * sizeof(*out->len));
		}

		out->data[out->count] = (char *)src + start;
		out->len[out->count] = i - start;

		/* Ignore a trailing CR so CRLF files diff cleanly. */
		if (out->len[out->count] > 0 &&
		    out->data[out->count][out->len[out->count] - 1] == '\r')
			out->len[out->count]--;

		out->count++;
		start = i + 1;

		if (i == len)
			break;
	}

	/* A trailing newline produces a final empty line; drop it. */
	if (out->count > 0 && out->len[out->count - 1] == 0)
		out->count--;
}

static void
lines_free(struct lines *l)
{
	free(l->data);
	free(l->len);
	l->data = NULL;
	l->len = NULL;
	l->count = 0;
}

static int
line_eq(const struct lines *a, size_t i, const struct lines *b, size_t j)
{
	if (a->len[i] != b->len[j])
		return (0);

	return (!memcmp(a->data[i], b->data[j], a->len[i]));
}

static void
diff_emit(struct wbuf *out, const char *cls, const char *text, size_t len)
{
	wbuf_appendf(out, "<span class=\"%s\">", cls);

	if (len == 0)
		buf_lit(out, "&nbsp;");
	else
		html_escape(out, text, len);

	buf_lit(out, "</span>");
}

void
diff_render_html(const char *olds, size_t oldlen, const char *news,
    size_t newlen, struct wbuf *out)
{
	struct lines	a, b;
	size_t		i, j, w;
	unsigned int	*lcs;

	lines_split(olds, oldlen, &a);
	lines_split(news, newlen, &b);

	buf_lit(out, "<pre>");

	if (a.count > DIFF_MAX_LINES || b.count > DIFF_MAX_LINES) {
		/* Too large to diff cheaply: show it as a wholesale change. */
		for (i = 0; i < a.count; i++)
			diff_emit(out, "del", a.data[i], a.len[i]);
		for (j = 0; j < b.count; j++)
			diff_emit(out, "add", b.data[j], b.len[j]);
		goto done;
	}

	/* lcs[i][j] = length of the LCS of a[i..] and b[j..]. */
	w = b.count + 1;
	lcs = xcalloc((a.count + 1) * w, sizeof(*lcs));

	for (i = a.count; i-- > 0;) {
		for (j = b.count; j-- > 0;) {
			if (line_eq(&a, i, &b, j)) {
				lcs[i * w + j] = lcs[(i + 1) * w + j + 1] + 1;
			} else {
				unsigned int	down = lcs[(i + 1) * w + j];
				unsigned int	right = lcs[i * w + j + 1];

				lcs[i * w + j] = (down > right) ? down : right;
			}
		}
	}

	i = 0;
	j = 0;

	while (i < a.count && j < b.count) {
		if (line_eq(&a, i, &b, j)) {
			diff_emit(out, "ctx", a.data[i], a.len[i]);
			i++;
			j++;
		} else if (lcs[(i + 1) * w + j] >= lcs[i * w + j + 1]) {
			diff_emit(out, "del", a.data[i], a.len[i]);
			i++;
		} else {
			diff_emit(out, "add", b.data[j], b.len[j]);
			j++;
		}
	}

	while (i < a.count) {
		diff_emit(out, "del", a.data[i], a.len[i]);
		i++;
	}

	while (j < b.count) {
		diff_emit(out, "add", b.data[j], b.len[j]);
		j++;
	}

	free(lcs);

done:
	buf_lit(out, "</pre>");

	lines_free(&a);
	lines_free(&b);
}
