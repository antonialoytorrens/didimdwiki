/*
 * index, search, recent changes and the RSS feed.
 */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <stdlib.h>

#include "ecewo.h"

#include "app.h"
#include "middleware.h"
#include "models.h"
#include "routes.h"
#include "strutil.h"
#include "validate.h"
#include "views.h"
#include "xmalloc.h"

#ifndef MIN
#define MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif

#define SEARCH_MAX_HITS		100
#define EXCERPT_LEN		200

static void	index_rows(struct wbuf *, struct store_list *);
static int	search_matches(const char *, const char *, int, char *,
		    size_t);

void
route_index(ecewo_request_t *req, ecewo_response_t *res)
{
	struct view		*v;
	struct wbuf		 rows;
	struct store_list	 pages;
	const char		*sortarg;
	int			 sort;

	if (!require_login(req, res))
		return;

	sort = STORE_SORT_NAME;
	if ((sortarg = ecewo_query(req, "sort")) != NULL && v_sort(sortarg) &&
	    !strcmp(sortarg, "mtime"))
		sort = STORE_SORT_MTIME;

	if (page_list(sort, &pages) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not list the pages.");
		return;
	}

	wbuf_init(&rows, 2048);
	index_rows(&rows, &pages);

	v = view_new("index.html");

	view_title(v, "All pages");
	view_setf(v, "COUNT", "%zu", pages.count);
	view_set_raw(v, "ROWS", rows.data, rows.offset);

	if (pages.count == 0) {
		view_set_lit(v, "EMPTY", "<p class=\"meta\">No pages yet.</p>");
	} else {
		view_set_lit(v, "EMPTY", "");
	}

	wbuf_cleanup(&rows);
	store_list_free(&pages);

	view_render(req, res, ECEWO_OK, v);

}

static void
index_rows(struct wbuf *rows, struct store_list *pages)
{
	size_t		i;
	char		date[64], size[32], *encoded;

	for (i = 0; i < pages->count; i++) {
		fmt_date(pages->ents[i].mtime, date, sizeof(date));
		fmt_size(pages->ents[i].size, size, sizeof(size));
		encoded = url_encode_str(pages->ents[i].name);

		wbuf_appendf(rows, "<tr><td><a href=\"/wiki/%s\">",
		    encoded);
		html_escape(rows, pages->ents[i].name,
		    strlen(pages->ents[i].name));
		wbuf_appendf(rows, "</a></td><td>%s</td><td>%s</td></tr>",
		    date, size);

		free(encoded);
	}
}

void
route_changes(ecewo_request_t *req, ecewo_response_t *res)
{
	struct view		*v;
	struct wbuf		 rows;
	struct store_list	 pages;
	struct revision_list	 revs;
	size_t			 i;
	char			 date[64], *encoded;

	if (!require_login(req, res))
		return;

	if (page_list(STORE_SORT_MTIME, &pages) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not list the pages.");
		return;
	}

	wbuf_init(&rows, 2048);

	for (i = 0; i < pages.count; i++) {
		fmt_date(pages.ents[i].mtime, date, sizeof(date));
		encoded = url_encode_str(pages.ents[i].name);

		if (revision_list(pages.ents[i].name, &revs) == -1)
			revs.count = 0;

		wbuf_appendf(&rows, "<tr><td><a href=\"/wiki/%s\">",
		    encoded);
		html_escape(&rows, pages.ents[i].name,
		    strlen(pages.ents[i].name));
		wbuf_appendf(&rows, "</a></td><td>%s</td>"
		    "<td><a href=\"/wiki/%s?history=1\">%zu</a></td></tr>",
		    date, encoded, revs.count);

		revision_list_free(&revs);
		free(encoded);
	}

	v = view_new("changes.html");

	view_title(v, "Recent changes");
	view_setf(v, "COUNT", "%zu", pages.count);
	view_set_raw(v, "ROWS", rows.data, rows.offset);

	if (pages.count == 0) {
		view_set_lit(v, "EMPTY", "<p class=\"meta\">No pages yet.</p>");
	} else {
		view_set_lit(v, "EMPTY", "");
	}

	wbuf_cleanup(&rows);
	store_list_free(&pages);

	view_render(req, res, ECEWO_OK, v);

}

void
route_changes_rss(ecewo_request_t *req, ecewo_response_t *res)
{
	struct view		*v;
	struct wbuf		 items, src;
	struct store_list	 pages;
	size_t			 i;
	char			 date[64], excerpt[EXCERPT_LEN];
	const char		*host;
	char			 base[512];

	if (!require_login(req, res))
		return;

	if (!ctx_authed(req) && user_count() > 0) {
		ecewo_send(res, ECEWO_FORBIDDEN, NULL, 0);
		return;
	}

	if ((host = ecewo_header_get(req, "host")) == NULL)
		host = "localhost";

	(void)snprintf(base, sizeof(base), "http%s://%s",
	    app_cfg.tls_enabled ? "s" : "", host);

	if (page_list(STORE_SORT_MTIME, &pages) == -1) {
		ecewo_send(res, ECEWO_INTERNAL_SERVER_ERROR, NULL, 0);
		return;
	}

	wbuf_init(&items, 4096);

	for (i = 0; i < pages.count && i < 50; i++) {
		char	*encoded = url_encode_str(pages.ents[i].name);

		wbuf_init(&src, 1024);
		(void)page_read(pages.ents[i].name, &src);
		markdown_excerpt((const char *)src.data, src.offset, excerpt,
		    sizeof(excerpt));
		wbuf_cleanup(&src);

		fmt_rfc822(pages.ents[i].mtime, date, sizeof(date));

		buf_lit(&items, "\t<item>\n\t\t<title>");
		xml_escape(&items, pages.ents[i].name,
		    strlen(pages.ents[i].name));
		wbuf_appendf(&items, "</title>\n"
		    "\t\t<link>%s/wiki/%s</link>\n"
		    "\t\t<guid isPermaLink=\"true\">%s/wiki/%s</guid>\n"
		    "\t\t<pubDate>%s</pubDate>\n\t\t<description>",
		    base, encoded, base, encoded, date);
		xml_escape(&items, excerpt, strlen(excerpt));
		buf_lit(&items, "</description>\n\t</item>\n");

		free(encoded);
	}

	fmt_rfc822(time(NULL), date, sizeof(date));

	v = view_new("changes_rss.xml");
	view_set(v, "BASE", base);
	view_set(v, "BUILD", date);
	view_set_raw(v, "ITEMS", items.data, items.offset);

	wbuf_cleanup(&items);
	store_list_free(&pages);

	view_render_raw(res, ECEWO_OK, v,
	    "application/rss+xml; charset=utf-8");

}

/*
 * '@' searches page text only, '/' searches names only, and a bare term
 * searches both.
 */
static int
search_matches(const char *name, const char *needle, int mode, char *excerpt,
    size_t len)
{
	struct wbuf	 src;
	const char	*hit;
	size_t		 start;
	int		 found;

	excerpt[0] = '\0';

	if (mode != 1 && str_casestr(name, needle) != NULL) {
		wbuf_init(&src, 1024);
		if (page_read(name, &src) == 0) {
			markdown_excerpt((const char *)src.data,
			    MIN(src.offset, len * 2), excerpt, len);
		}
		wbuf_cleanup(&src);
		return (1);
	}

	if (mode == 2)
		return (0);

	wbuf_init(&src, 4096);

	if (page_read(name, &src) == -1) {
		wbuf_cleanup(&src);
		return (0);
	}

	found = 0;
	wbuf_stringify(&src, NULL);

	if ((hit = str_casestr((const char *)src.data, needle)) != NULL) {
		/* Excerpt around the hit rather than from the top. */
		start = (size_t)(hit - (const char *)src.data);
		start = (start > 60) ? start - 60 : 0;

		markdown_excerpt((const char *)src.data + start,
		    MIN(src.offset - start, len * 2), excerpt, len);
		found = 1;
	}

	wbuf_cleanup(&src);

	return (found);
}

void
route_search(ecewo_request_t *req, ecewo_response_t *res)
{
	struct view		*v;
	struct wbuf		 results;
	struct store_list	 pages;
	const char		*query, *needle;
	char			 excerpt[EXCERPT_LEN];
	size_t			 i, hits;
	int			 mode;
	const char		*only = NULL;

	if (!require_login(req, res))
		return;

	if ((query = ecewo_query(req, "q")) == NULL || !v_body(query))
		query = "";

	needle = query;
	mode = 0;

	if (needle[0] == '@') {
		mode = 1;	/* content only */
		needle++;
	} else if (needle[0] == '/') {
		mode = 2;	/* names only */
		needle++;
	}

	wbuf_init(&results, 2048);
	hits = 0;

	if (needle[0] != '\0' && page_list(STORE_SORT_NAME, &pages) == 0) {
		for (i = 0; i < pages.count && hits < SEARCH_MAX_HITS; i++) {
			char	*encoded;

			if (!search_matches(pages.ents[i].name, needle, mode,
			    excerpt, sizeof(excerpt)))
				continue;

			if (hits == 0)
				only = pages.ents[i].name;

			encoded = url_encode_str(pages.ents[i].name);

			wbuf_appendf(&results,
			    "<div class=\"hit\"><h3><a href=\"/wiki/%s\">",
			    encoded);
			html_escape(&results, pages.ents[i].name,
			    strlen(pages.ents[i].name));
			buf_lit(&results, "</a></h3><p>");
			html_escape(&results, excerpt, strlen(excerpt));
			buf_lit(&results, "</p></div>");

			free(encoded);
			hits++;
		}

		/* One hit goes straight to the page. */
		if (hits == 1 && only != NULL) {
			char	*target = url_encode_str(only);

			store_list_free(&pages);
			wbuf_cleanup(&results);

			view_redirectf(res, "/wiki/%s", target);
			free(target);

			return;
		}

		store_list_free(&pages);
	}

	v = view_new("search.html");

	view_title(v, "Search");
	view_set(v, "QUERY", query);
	view_set_raw(v, "RESULTS", results.data, results.offset);

	if (needle[0] == '\0') {
		view_set_lit(v, "SUMMARY", "");
	} else {
		struct wbuf	sum;

		wbuf_init(&sum, 128);
		wbuf_appendf(&sum, "<p class=\"meta\">%zu %s.</p>", hits,
		    (hits == 1) ? "match" : "matches");
		view_set_raw(v, "SUMMARY", sum.data, sum.offset);
		wbuf_cleanup(&sum);
	}

	wbuf_cleanup(&results);

	view_render(req, res, ECEWO_OK, v);

}
