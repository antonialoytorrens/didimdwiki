/*
 * didimdwiki - controllers: model to view glue, no URL parsing.
 *
 * Handlers that read a submitted form take it already parsed: ecewo has no
 * http_populate_post() equivalent, so the route parses the body once and
 * passes it down.
 */

#ifndef DIDIMDWIKI_CONTROLLERS_H
#define DIDIMDWIKI_CONTROLLERS_H

#include <time.h>

#include "ecewo.h"

#include "form.h"

/* ----------------------------------------------------------------- pages */

void	page_show(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_raw(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_edit_form(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_edit_preview(ecewo_request_t *, ecewo_response_t *, const char *,
	    const struct form *);
void	page_new_form(ecewo_request_t *, ecewo_response_t *);
void	page_delete_form(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_rename_form(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_history(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_diff(ecewo_request_t *, ecewo_response_t *, const char *, time_t);
void	page_save(ecewo_request_t *, ecewo_response_t *, const char *,
	    const struct form *);
void	page_create(ecewo_request_t *, ecewo_response_t *, const struct form *);
void	page_do_rename(ecewo_request_t *, ecewo_response_t *, const char *,
	    const struct form *);
void	page_do_delete(ecewo_request_t *, ecewo_response_t *, const char *);
void	page_restore(ecewo_request_t *, ecewo_response_t *, const char *,
	    time_t);

/* ----------------------------------------------------------------- files */

void	file_index(ecewo_request_t *, ecewo_response_t *);
void	file_show(ecewo_request_t *, ecewo_response_t *, const char *);
void	file_upload(ecewo_request_t *, ecewo_response_t *);
void	file_replace(ecewo_request_t *, ecewo_response_t *, const char *);
void	file_do_rename(ecewo_request_t *, ecewo_response_t *, const char *,
	    const struct form *);
void	file_do_delete(ecewo_request_t *, ecewo_response_t *, const char *);
void	file_edit_form(ecewo_request_t *, ecewo_response_t *, const char *);

/* ----------------------------------------------------------------- users */

void	user_login_form(ecewo_request_t *, ecewo_response_t *, const char *,
	    const char *);
void	user_do_login(ecewo_request_t *, ecewo_response_t *,
	    const struct form *);
void	user_register_form(ecewo_request_t *, ecewo_response_t *,
	    const char *);
void	user_do_register(ecewo_request_t *, ecewo_response_t *,
	    const struct form *);
void	user_account_page(ecewo_request_t *, ecewo_response_t *, const char *,
	    const char *);
void	user_change_password(ecewo_request_t *, ecewo_response_t *,
	    const struct form *);

#endif /* !DIDIMDWIKI_CONTROLLERS_H */
