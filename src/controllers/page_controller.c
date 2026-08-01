/*
 * didimdwiki - page controller: models to views.
 */

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ecewo.h"

#include "app.h"
#include "controllers.h"
#include "middleware.h"
#include "models.h"
#include "validate.h"
#include "views.h"

static void	page_common(ecewo_request_t *, struct view *, const char *);
static void	page_render_missing(ecewo_request_t *, ecewo_response_t *,
		    const char *);
static void	page_editor(ecewo_request_t *, ecewo_response_t *,
		    const char *, const char *, size_t, const char *,
		    const char *);
static int	md_flags(void);

static int
md_flags(void)
{
	return (app_cfg.camelcase_links ? MD_RENDER_CAMELCASE : 0);
}

/* NAME plus its url-encoded form, which every page template needs. */
static void
page_common(ecewo_request_t *req, struct view *v, const char *name)
{
	char	*encoded;

	view_set(v, "NAME", name);

	encoded = url_encode_str(name);
	view_set(v, "NAME_URL", encoded);
	free(encoded);

	view_set(v, "CSRF", csrf_token(req));
	view_title(v, name);
}

static void
page_render_missing(ecewo_request_t *req, ecewo_response_t *res,
    const char *name)
{
	struct view	*v;

	v = view_new("page_missing.html");
	page_common(req, v, name);

	view_render(req, res, ECEWO_NOT_FOUND, v);
}

void
page_show(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct view	*v;
	struct wbuf	 src, html, actions;
	char		 date[64], size[32];
	char		 redir_dst[NAME_MAX_LEN];
	char		*encoded;
	off_t		 bytes;
	time_t		 mtime;

	if (!page_exists(name)) {
		if (page_redirect_lookup(name, redir_dst,
		    sizeof(redir_dst)) == 0) {
			view_redirect_permanentf(res, "/wiki/%s", redir_dst);
			return;
		}
		page_render_missing(req, res, name);
		return;
	}

	wbuf_init(&src, 4096);

	if (page_read(name, &src) == -1) {
		wbuf_cleanup(&src);
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not read the page.");
		return;
	}

	wbuf_init(&html, src.offset * 2 + 512);
	markdown_render(src.data, src.offset, &html, md_flags());

	bytes = 0;
	mtime = 0;
	(void)page_stat(name, &bytes, &mtime);

	fmt_date(mtime, date, sizeof(date));
	fmt_size(bytes, size, sizeof(size));

	v = view_new("page.html");
	page_common(req, v, name);
	view_set(v, "MODIFIED", date);
	view_set(v, "SIZE", size);
	view_set_raw(v, "CONTENT", html.data, html.offset);

	encoded = url_encode_str(name);
	wbuf_init(&actions, 256);
	if (ctx_authed(req)) {
		wbuf_appendf(&actions,
		    "\t\t\t<a href=\"/wiki/%s?edit=1\">Edit</a>\n"
		    "\t\t\t<a href=\"/wiki/%s?history=1\">History</a>\n"
		    "\t\t\t<a href=\"/wiki/%s?rename=1\">Rename</a>\n"
		    "\t\t\t<a class=\"danger\" href=\"/wiki/%s?delete=1\">Delete</a>\n",
		    encoded, encoded, encoded, encoded);
	} else {
		wbuf_appendf(&actions,
		    "\t\t\t<a href=\"/wiki/%s?history=1\">History</a>\n",
		    encoded);
	}
	free(encoded);
	view_set_raw(v, "PAGE_ACTIONS", actions.data, actions.offset);
	wbuf_cleanup(&actions);

	wbuf_cleanup(&src);
	wbuf_cleanup(&html);

	view_render(req, res, ECEWO_OK, v);
}

/* Markdown source, for editors and scripts. */
void
page_raw(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct wbuf	src;

	(void)req;

	if (!page_exists(name)) {
		ecewo_send(res, ECEWO_NOT_FOUND, NULL, 0);
		return;
	}

	wbuf_init(&src, 4096);

	if (page_read(name, &src) == -1) {
		wbuf_cleanup(&src);
		ecewo_send(res, ECEWO_INTERNAL_SERVER_ERROR, NULL, 0);
		return;
	}

	ecewo_header_set(res, "content-type", "text/markdown; charset=utf-8");
	ecewo_send(res, ECEWO_OK, src.data, src.offset);

	wbuf_cleanup(&src);
}

static void
page_editor(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    const char *body, size_t len, const char *notice, const char *preview)
{
	struct view	*v;
	struct wbuf	 esc;

	v = view_new("page_edit.html");
	page_common(req, v, name);

	wbuf_init(&esc, len + 64);
	html_escape(&esc, body, len);
	view_set_raw(v, "BODY_TEXT", esc.data, esc.offset);
	wbuf_cleanup(&esc);

	view_set_raw(v, "NOTICE", (notice != NULL) ? notice : "",
	    (notice != NULL) ? strlen(notice) : 0);
	view_set_raw(v, "PREVIEW", (preview != NULL) ? preview : "",
	    (preview != NULL) ? strlen(preview) : 0);

	view_render(req, res, ECEWO_OK, v);
}

void
page_edit_form(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct wbuf	src;

	if (!require_auth(req, res))
		return;

	wbuf_init(&src, 4096);
	(void)page_read(name, &src);

	page_editor(req, res, name, (const char *)src.data, src.offset, NULL,
	    NULL);

	wbuf_cleanup(&src);
}

void
page_edit_preview(ecewo_request_t *req, ecewo_response_t *res,
    const char *name, const struct form *form)
{
	const char	*body;
	struct wbuf	 html, wrap;

	if ((body = form_get(form, "body")) == NULL || !v_body(body))
		body = "";

	wbuf_init(&html, strlen(body) * 2 + 512);
	markdown_render(body, strlen(body), &html, md_flags());

	wbuf_init(&wrap, html.offset + 128);
	buf_lit(&wrap, "<section class=\"preview markdown\"><h2>Preview</h2>");
	wbuf_append(&wrap, html.data, html.offset);
	buf_lit(&wrap, "</section>");

	page_editor(req, res, name, body, strlen(body), NULL,
	    (const char *)wbuf_stringify(&wrap, NULL));

	wbuf_cleanup(&html);
	wbuf_cleanup(&wrap);
}

void
page_new_form(ecewo_request_t *req, ecewo_response_t *res)
{
	struct view		*v;
	struct wbuf		 opts, body, esc;
	const char		*name, *template;
	size_t			 i;
	struct store_list	 templates;

	if (!require_login(req, res))
		return;

	if ((name = ecewo_query(req, "name")) == NULL || !v_pagename(name))
		name = "";
	if ((template = ecewo_query(req, "template")) != NULL &&
	    !v_pagename(template))
		template = NULL;

	wbuf_init(&opts, 512);
	buf_lit(&opts, "<option value=\"\">(blank page)</option>");

	if (store_list(TEMPLATES_DIR, PAGE_EXT, STORE_SORT_NAME,
	    &templates) == 0) {
		for (i = 0; i < templates.count; i++) {
			const char	*t = templates.ents[i].name;

			wbuf_appendf(&opts,
			    "<option value=\"%s\"%s>%s</option>", t,
			    (template != NULL && !strcmp(template, t)) ?
			    " selected" : "", t);
		}
		store_list_free(&templates);
	}

	wbuf_init(&body, 1024);

	if (template != NULL && template[0] != '\0') {
		char	path[PATH_MAX_LEN];

		if (store_path(path, sizeof(path), TEMPLATES_DIR, template,
		    PAGE_EXT) == 0)
			(void)store_read(path, &body);
	}

	v = view_new("page_new.html");

	view_title(v, "Create a page");
	view_set(v, "NAME", name);
	view_set(v, "CSRF", csrf_token(req));
	view_set_raw(v, "TEMPLATES", opts.data, opts.offset);
	view_set_lit(v, "NOTICE", "");

	wbuf_init(&esc, body.offset + 64);
	html_escape(&esc, (const char *)body.data, body.offset);
	view_set_raw(v, "BODY_TEXT", esc.data, esc.offset);
	wbuf_cleanup(&esc);

	wbuf_cleanup(&opts);
	wbuf_cleanup(&body);

	view_render(req, res, ECEWO_OK, v);
}

void
page_delete_form(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct view	*v;

	if (!require_auth(req, res))
		return;

	if (!page_exists(name)) {
		page_render_missing(req, res, name);
		return;
	}

	v = view_new("page_delete.html");
	page_common(req, v, name);

	view_render(req, res, ECEWO_OK, v);
}

void
page_rename_form(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct view	*v;

	if (!require_auth(req, res))
		return;

	if (!page_exists(name)) {
		page_render_missing(req, res, name);
		return;
	}

	v = view_new("page_rename.html");
	page_common(req, v, name);
	view_set_lit(v, "NOTICE", "");

	view_render(req, res, ECEWO_OK, v);
}

void
page_history(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct view		*v;
	struct wbuf		 rows;
	struct revision_list	 revs;
	size_t			 i;
	char			 date[64], size[32], *encoded;

	if (revision_list(name, &revs) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not read the page history.");
		return;
	}

	encoded = url_encode_str(name);
	wbuf_init(&rows, 1024);

	for (i = 0; i < revs.count; i++) {
		fmt_date(revs.revs[i].stamp, date, sizeof(date));
		fmt_size(revs.revs[i].size, size, sizeof(size));

		wbuf_appendf(&rows,
		    "<tr><td><a href=\"/wiki/%s?diff=%lld\">%s</a></td>"
		    "<td>", encoded, (long long)revs.revs[i].stamp, date);
		html_escape(&rows, revs.revs[i].author,
		    strlen(revs.revs[i].author));
		wbuf_appendf(&rows, "</td><td>%s</td>"
		    "<td><a href=\"/wiki/%s?diff=%lld\">compare</a></td></tr>",
		    size, encoded, (long long)revs.revs[i].stamp);
	}

	v = view_new("page_history.html");
	page_common(req, v, name);
	view_set_raw(v, "ROWS", rows.data, rows.offset);

	if (revs.count == 0)
		view_set_lit(v, "EMPTY",
		    "<p class=\"meta\">No revisions yet.</p>");
	else
		view_set_lit(v, "EMPTY", "");

	wbuf_cleanup(&rows);
	free(encoded);
	revision_list_free(&revs);

	view_render(req, res, ECEWO_OK, v);
}

void
page_diff(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    time_t stamp)
{
	struct view	*v;
	struct wbuf	 old, cur, out;
	char		 date[64];

	wbuf_init(&old, 4096);

	if (revision_read(name, stamp, &old) == -1) {
		wbuf_cleanup(&old);
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That revision does not exist.");
		return;
	}

	wbuf_init(&cur, 4096);
	(void)page_read(name, &cur);

	wbuf_init(&out, 4096);
	diff_render_html((const char *)old.data, old.offset,
	    (const char *)cur.data, cur.offset, &out);

	fmt_date(stamp, date, sizeof(date));

	v = view_new("page_diff.html");
	page_common(req, v, name);
	view_set(v, "STAMP", date);
	view_setf(v, "STAMP_RAW", "%lld", (long long)stamp);
	view_set_raw(v, "DIFF", out.data, out.offset);

	wbuf_cleanup(&old);
	wbuf_cleanup(&cur);
	wbuf_cleanup(&out);

	view_render(req, res, ECEWO_OK, v);
}

void
page_save(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    const struct form *form)
{
	const char	*body;

	if ((body = form_get(form, "body")) == NULL || !v_body(body)) {
		view_error(req, res, ECEWO_BAD_REQUEST,
		    "The page body was missing or rejected by validation.");
		return;
	}

	if (page_write(name, ctx_user(req), body, strlen(body)) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not save the page.");
		return;
	}

	view_redirectf(res, "/wiki/%s", name);
}

void
page_create(ecewo_request_t *req, ecewo_response_t *res,
    const struct form *form)
{
	const char	*name, *body;

	if ((name = form_get(form, "name")) == NULL || !v_pagename(name)) {
		view_error(req, res, ECEWO_BAD_REQUEST,
		    "That page name is not allowed.");
		return;
	}

	if ((body = form_get(form, "body")) == NULL || !v_body(body))
		body = "";

	if (page_exists(name)) {
		view_redirectf(res, "/wiki/%s?edit=1", name);
		return;
	}

	if (page_write(name, ctx_user(req), body, strlen(body)) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not create the page.");
		return;
	}

	view_redirectf(res, "/wiki/%s", name);
}

void
page_do_rename(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    const struct form *form)
{
	const char	*newname;
	int		 ret;

	if ((newname = form_get(form, "newname")) == NULL ||
	    !v_pagename(newname)) {
		view_error(req, res, ECEWO_BAD_REQUEST,
		    "That page name is not allowed.");
		return;
	}

	ret = page_rename(name, newname, ctx_user(req));

	if (ret == -2) {
		view_error(req, res, ECEWO_CONFLICT,
		    "A page with that name already exists.");
		return;
	}

	if (ret == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not rename the page.");
		return;
	}

	view_redirectf(res, "/wiki/%s", newname);
}

void
page_do_delete(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	if (page_delete(name, ctx_user(req)) == -1) {
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That page does not exist.");
		return;
	}

	view_redirect(res, "/index");
}

void
page_restore(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    time_t stamp)
{
	struct wbuf	old;

	wbuf_init(&old, 4096);

	if (revision_read(name, stamp, &old) == -1) {
		wbuf_cleanup(&old);
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That revision does not exist.");
		return;
	}

	/* Restoring snapshots the current text first, so it is never lost. */
	if (page_write(name, ctx_user(req), old.data, old.offset) == -1) {
		wbuf_cleanup(&old);
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not restore that revision.");
		return;
	}

	wbuf_cleanup(&old);
	view_redirectf(res, "/wiki/%s", name);
}
