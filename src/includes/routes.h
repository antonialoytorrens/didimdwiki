/*
 * didimdwiki - route handlers and their shared helpers.
 */

#ifndef DIDIMDWIKI_ROUTES_H
#define DIDIMDWIKI_ROUTES_H

#include "ecewo.h"

#include "form.h"

/* ---------------------------------------------------------------- helpers */

/*
 * Validated :name path parameter, or NULL.
 *
 * ecewo percent-decodes route parameters, so all this adds is the
 * store_valid_name() check -- the traversal defence for every /wiki/,
 * /files/ and /api/v1/ path.
 */
const char	*route_name(ecewo_request_t *);

/*
 * True when a query flag such as ?edit=1 is set.
 *
 * The flags carry a value rather than being bare (?edit=1, not ?edit):
 * ecewo's query parser discards valueless pairs, and the raw query string is
 * not reachable -- the '?' is overwritten when the path is split.
 */
int		 route_flag(ecewo_request_t *, const char *);

/*
 * Reads and parses an urlencoded request body. Returns 0 on success. The
 * caller must form_free() on every path.
 */
int		 route_form(ecewo_request_t *, struct form *);

/*
 * Same, but also accepts multipart/form-data, presenting its non-file fields
 * through the same struct form. Needed on /files, where one route serves both
 * the multipart upload form and the plain rename/delete forms.
 */
int		 route_form_any(ecewo_request_t *, struct form *);

/* Sends the standard 403 for a failed CSRF check. */
void		 route_csrf_error(ecewo_request_t *, ecewo_response_t *);

/* ----------------------------------------------------------------- pages */

void	route_home(ecewo_request_t *, ecewo_response_t *);
void	route_pages_get(ecewo_request_t *, ecewo_response_t *);
void	route_pages_post(ecewo_request_t *, ecewo_response_t *);
void	route_page_new_get(ecewo_request_t *, ecewo_response_t *);
void	route_page_new_post(ecewo_request_t *, ecewo_response_t *);

/* ----------------------------------------------------------------- files */

void	route_files_index_get(ecewo_request_t *, ecewo_response_t *);
void	route_files_index_post(ecewo_request_t *, ecewo_response_t *);
void	route_files_get(ecewo_request_t *, ecewo_response_t *);
void	route_files_post(ecewo_request_t *, ecewo_response_t *);

/* --------------------------------------------------------------- listings */

void	route_index(ecewo_request_t *, ecewo_response_t *);
void	route_changes(ecewo_request_t *, ecewo_response_t *);
void	route_changes_rss(ecewo_request_t *, ecewo_response_t *);
void	route_search(ecewo_request_t *, ecewo_response_t *);

/* ------------------------------------------------------------------ auth */

void	route_login_get(ecewo_request_t *, ecewo_response_t *);
void	route_login_post(ecewo_request_t *, ecewo_response_t *);
void	route_logout(ecewo_request_t *, ecewo_response_t *);
void	route_register_get(ecewo_request_t *, ecewo_response_t *);
void	route_register_post(ecewo_request_t *, ecewo_response_t *);
void	route_account_get(ecewo_request_t *, ecewo_response_t *);
void	route_account_post(ecewo_request_t *, ecewo_response_t *);

/* ------------------------------------------------------------------- api */

void	api_pages_list(ecewo_request_t *, ecewo_response_t *);
void	api_pages_create(ecewo_request_t *, ecewo_response_t *);
void	api_page_get(ecewo_request_t *, ecewo_response_t *);
void	api_page_put(ecewo_request_t *, ecewo_response_t *);
void	api_page_patch(ecewo_request_t *, ecewo_response_t *);
void	api_page_delete(ecewo_request_t *, ecewo_response_t *);
void	api_page_revisions(ecewo_request_t *, ecewo_response_t *);
void	api_files_list(ecewo_request_t *, ecewo_response_t *);
void	api_files_create(ecewo_request_t *, ecewo_response_t *);
void	api_file_get(ecewo_request_t *, ecewo_response_t *);
void	api_file_put(ecewo_request_t *, ecewo_response_t *);
void	api_file_patch(ecewo_request_t *, ecewo_response_t *);
void	api_file_delete(ecewo_request_t *, ecewo_response_t *);
void	api_not_found(ecewo_request_t *, ecewo_response_t *);

#endif /* !DIDIMDWIKI_ROUTES_H */
