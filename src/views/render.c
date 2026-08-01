/*
 * didimdwiki - the $VAR$ template engine, standing in for res.render().
 *
 * Three properties here are load-bearing and easy to lose:
 *
 *   1. tpl_set() escapes '$' as well as HTML, so a substituted value can
 *      never introduce a placeholder for a later pass to expand.
 *   2. view_set_raw() defers to render time, because markdown output may
 *      legitimately contain a '$'.
 *   3. view_render() splits layout.html at the literal $BODY$ instead of
 *      substituting into it, so page content is never rescanned at all.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecewo.h"

#include "app.h"
#include "middleware.h"
#include "views.h"
#include "wbuf.h"
#include "xmalloc.h"

static void	view_apply_raw(struct view *);
static void	view_chrome(ecewo_request_t *, struct wbuf *);

struct view *
view_new(const char *template)
{
	struct view	*v;
	const char	*data;
	size_t		 len;

	if ((data = template_get(template, &len)) == NULL)
		fatal("view_new: no such template: %s", template);

	v = xcalloc(1, sizeof(*v));
	v->body = wbuf_alloc(len + 1024);

	wbuf_append(v->body, data, len);

	return (v);
}

void
view_free(struct view *v)
{
	size_t		i;

	if (v == NULL)
		return;

	for (i = 0; i < v->nraw; i++) {
		free(v->raw[i].key);
		free(v->raw[i].val);
	}

	wbuf_free(v->body);
	free(v->title);
	free(v);
}

/*
 * Escapes HTML and also '$', so a substituted value can never introduce a
 * new placeholder for a later pass.
 */
void
tpl_set(struct wbuf *buf, const char *key, const char *value)
{
	char		token[64];
	struct wbuf	esc;
	size_t		i, len;

	if (value == NULL)
		value = "";

	(void)snprintf(token, sizeof(token), "$%s$", key);

	len = strlen(value);
	wbuf_init(&esc, len + 32);

	for (i = 0; i < len; i++) {
		if (value[i] == '$')
			buf_lit(&esc, "&#36;");
		else
			html_escape(&esc, &value[i], 1);
	}

	wbuf_replace_string(buf, token, esc.data, esc.offset);
	wbuf_cleanup(&esc);
}

void
view_set(struct view *v, const char *key, const char *value)
{
	tpl_set(v->body, key, value);
}

void
view_setf(struct view *v, const char *key, const char *fmt, ...)
{
	va_list		args;
	char		buf[1024];

	va_start(args, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	view_set(v, key, buf);
}

/*
 * Raw values are deferred to render time: markdown output may legitimately
 * contain a '$', and deferring means no later pass rescans it.
 */
void
view_set_raw(struct view *v, const char *key, const void *val, size_t len)
{
	struct raw_slot	*slot;
	size_t		 klen;

	if (v->nraw >= VIEW_MAX_RAW) {
		app_log(LOG_ERR, "view_set_raw: too many slots for %s", key);
		return;
	}

	slot = &v->raw[v->nraw++];

	klen = strlen(key) + 3;
	slot->key = xmalloc(klen);
	(void)snprintf(slot->key, klen, "$%s$", key);

	slot->len = len;
	slot->val = xmalloc(len + 1);
	memcpy(slot->val, val, len);
	slot->val[len] = '\0';
}

static void
view_apply_raw(struct view *v)
{
	size_t		i;

	for (i = 0; i < v->nraw; i++) {
		wbuf_replace_string(v->body, v->raw[i].key, v->raw[i].val,
		    v->raw[i].len);
		free(v->raw[i].key);
		free(v->raw[i].val);
	}

	v->nraw = 0;
}

void
view_title(struct view *v, const char *title)
{
	free(v->title);
	v->title = xstrdup(title);
}

/*
 * Wraps the view body in layout.html. The layout is split at $BODY$ rather
 * than substituted, so page content is never scanned for placeholders.
 *
 * The security headers come from ecewo-helmet, which applies them to every
 * response rather than just to HTML renders.
 */
void
view_render(ecewo_request_t *req, ecewo_response_t *res, int status,
    struct view *v)
{
	struct wbuf	*doc;
	const char	*layout;
	char		*flat, *split;
	size_t		 layout_len, flen;

	view_apply_raw(v);

	layout = template_get("layout.html", &layout_len);

	doc = wbuf_alloc(layout_len + v->body->offset + 512);
	wbuf_append(doc, layout, layout_len);

	tpl_set(doc, "TITLE", (v->title != NULL) ? v->title : APP_NAME);
	tpl_set(doc, "VERSION", APP_VERSION);
	view_chrome(req, doc);

	flat = wbuf_stringify(doc, &flen);

	if ((split = strstr(flat, "$BODY$")) != NULL) {
		struct wbuf	*out;

		out = wbuf_alloc(flen + v->body->offset + 16);
		wbuf_append(out, flat, (size_t)(split - flat));
		wbuf_append(out, v->body->data, v->body->offset);
		wbuf_append(out, split + 6, strlen(split + 6));

		wbuf_free(doc);
		doc = out;
	}

	ecewo_header_set(res, "content-type", "text/html; charset=utf-8");
	ecewo_send(res, status, doc->data, doc->offset);

	wbuf_free(doc);
	view_free(v);
}

static void
view_chrome(ecewo_request_t *req, struct wbuf *doc)
{
	char		nav[512];
	const char	*user;

	if (req != NULL && ctx_authed(req)) {
		user = ctx_user(req);
		(void)snprintf(nav, sizeof(nav),
		    "<a href=\"/account\">%s</a><a href=\"/logout\">log out</a>",
		    user);
		tpl_set(doc, "USER", user);
	} else {
		(void)snprintf(nav, sizeof(nav),
		    "<a href=\"/login\">log in</a>");
		tpl_set(doc, "USER", "");
	}

	/* Already-escaped markup, so it goes in without another escaping pass. */
	wbuf_replace_string(doc, "$NAVAUTH$", nav, strlen(nav));
}

void
view_render_raw(ecewo_response_t *res, int status, struct view *v,
    const char *ctype)
{
	view_apply_raw(v);

	ecewo_header_set(res, "content-type", ctype);
	ecewo_send(res, status, v->body->data, v->body->offset);

	view_free(v);
}

void
view_error(ecewo_request_t *req, ecewo_response_t *res, int status,
    const char *message)
{
	struct view	*v;

	v = view_new("error.html");

	view_title(v, "Error");
	view_setf(v, "STATUS", "%d", status);
	view_set(v, "MESSAGE", message);

	view_render(req, res, status, v);
}

void
view_redirect(ecewo_response_t *res, const char *location)
{
	ecewo_redirect(res, ECEWO_SEE_OTHER, location);
}

void
view_redirectf(ecewo_response_t *res, const char *fmt, ...)
{
	va_list		args;
	char		buf[PATH_MAX_LEN];

	va_start(args, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	view_redirect(res, buf);
}

void
view_redirect_permanent(ecewo_response_t *res, const char *location)
{
	ecewo_redirect(res, ECEWO_MOVED_PERMANENTLY, location);
}

void
view_redirect_permanentf(ecewo_response_t *res, const char *fmt, ...)
{
	va_list		args;
	char		buf[PATH_MAX_LEN];

	va_start(args, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	view_redirect_permanent(res, buf);
}
