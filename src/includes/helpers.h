/*
 * didimdwiki - HTML/URL escaping, formatting, and markdown rendering.
 *
 * Kept separate from views.h, which pulls in ecewo: the model layer and the
 * unit tests use these and have no business linking the web framework.
 */

#ifndef DIDIMDWIKI_HELPERS_H
#define DIDIMDWIKI_HELPERS_H

#include <sys/types.h>
#include <time.h>

#include "wbuf.h"

void	 html_escape(struct wbuf *, const char *, size_t);
void	 url_encode(struct wbuf *, const char *, size_t);
void	 xml_escape(struct wbuf *, const char *, size_t);

/* Caller frees the result. */
char	*html_escape_str(const char *);
char	*url_encode_str(const char *);

void	 fmt_date(time_t, char *, size_t);
void	 fmt_rfc822(time_t, char *, size_t);
void	 fmt_size(off_t, char *, size_t);

/* ------------------------------------------------------------- markdown */

#define MD_RENDER_CAMELCASE	0x0001

void	markdown_render(const void *, size_t, struct wbuf *, int);
void	markdown_excerpt(const char *, size_t, char *, size_t);

#endif /* !DIDIMDWIKI_HELPERS_H */
