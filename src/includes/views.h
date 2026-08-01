/*
 * didimdwiki - view layer: markdown rendering, HTML helpers and the
 * $VAR$ template engine that stands in for express' res.render().
 */

#ifndef DIDIMDWIKI_VIEWS_H
#define DIDIMDWIKI_VIEWS_H

#include "ecewo.h"

#include "app.h"
#include "helpers.h"
#include "wbuf.h"

/* ------------------------------------------------------------ templates */

/*
 * kodev compiled assets/ into the .so as asset_<name>_<ext> symbols. ecewo
 * has no asset pipeline, so templates are read from disk once at startup
 * into the app arena and looked up by basename.
 */
int		 templates_load(ecewo_app_t *, const char *);
const char	*template_get(const char *, size_t *);

/* --------------------------------------------------------------- render */

#define VIEW_MAX_RAW	8

struct raw_slot {
	char	*key;
	uint8_t	*val;
	size_t	 len;
};

struct view {
	struct wbuf	*body;
	char		*title;
	struct raw_slot	 raw[VIEW_MAX_RAW];
	size_t		 nraw;
};

/* Builds a view from a template basename, e.g. view_new("page.html"). */
struct view	*view_new(const char *);
void		 view_free(struct view *);

#define view_set_lit(v, k, s)	view_set_raw(v, k, s, sizeof(s) - 1)

/* $KEY$ substitution on a bare buffer; the primitive the rest builds on. */
void	tpl_set(struct wbuf *, const char *, const char *);

/* Substitutes $KEY$. view_set escapes, view_set_raw does not. */
void	view_set(struct view *, const char *, const char *);
void	view_set_raw(struct view *, const char *, const void *, size_t);
void	view_setf(struct view *, const char *, const char *, ...)
	    __attribute__((format (printf, 3, 4)));
void	view_title(struct view *, const char *);

/* Wraps the view in layout.html and responds. Both consume the view. */
void	view_render(ecewo_request_t *, ecewo_response_t *, int, struct view *);
/* Responds with the view body verbatim (RSS, fragments). */
void	view_render_raw(ecewo_response_t *, int, struct view *, const char *);

void	view_error(ecewo_request_t *, ecewo_response_t *, int, const char *);
void	view_redirect(ecewo_response_t *, const char *);
void	view_redirectf(ecewo_response_t *, const char *, ...)
	    __attribute__((format (printf, 2, 3)));
void	view_redirect_permanent(ecewo_response_t *, const char *);
void	view_redirect_permanentf(ecewo_response_t *, const char *, ...)
	    __attribute__((format (printf, 2, 3)));

#endif /* !DIDIMDWIKI_VIEWS_H */
