/*
 * didimdwiki - /wiki routes, plus the helpers every route file shares.
 */

#include <sys/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecewo.h"

#include "app.h"
#include "controllers.h"
#include "middleware.h"
#include "models.h"
#include "routes.h"
#include "strutil.h"
#include "validate.h"
#include "views.h"

const char *
route_name(ecewo_request_t *req)
{
	const char	*name;

	if ((name = ecewo_param(req, "name")) == NULL)
		return (NULL);

	if (!store_valid_name(name))
		return (NULL);

	return (name);
}

int
route_flag(ecewo_request_t *req, const char *name)
{
	const char	*value;

	if ((value = ecewo_query(req, name)) == NULL)
		return (0);

	return (v_bool(value));
}

int
route_form(ecewo_request_t *req, struct form *form)
{
	return (form_parse(form, ecewo_req_body(req), ecewo_req_body_len(req)));
}

void
route_csrf_error(ecewo_request_t *req, ecewo_response_t *res)
{
	view_error(req, res, ECEWO_FORBIDDEN,
	    "Invalid or missing CSRF token. Reload the form and retry.");
}

void
route_home(ecewo_request_t *req, ecewo_response_t *res)
{
	if (!require_login(req, res))
		return;

	if (page_exists(HOME_PAGE)) {
		page_show(req, res, HOME_PAGE);
		return;
	}

	view_redirect(res, "/wiki?name=" HOME_PAGE);
}

void
route_pages_get(ecewo_request_t *req, ecewo_response_t *res)
{
	const char	*name, *diff;
	int		 err;
	long long	 stamp;

	if ((name = route_name(req)) == NULL) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Invalid page name.");
		return;
	}

	if (!require_login(req, res))
		return;

	if (route_flag(req, "edit"))
		page_edit_form(req, res, name);
	else if (route_flag(req, "delete"))
		page_delete_form(req, res, name);
	else if (route_flag(req, "rename"))
		page_rename_form(req, res, name);
	else if (route_flag(req, "history"))
		page_history(req, res, name);
	else if (route_flag(req, "raw"))
		page_raw(req, res, name);
	else if ((diff = ecewo_query(req, "diff")) != NULL && v_number(diff) &&
	    (stamp = str_tonum(diff, 10, 0, INT64_MAX, &err), err == STR_OK))
		page_diff(req, res, name, (time_t)stamp);
	else
		page_show(req, res, name);
}

void
route_pages_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	 form;
	const char	*name, *action, *stampstr;
	int		 err;
	long long	 stamp;

	if ((name = route_name(req)) == NULL) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Invalid page name.");
		return;
	}

	if (route_form(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	/*
	 * Writes always require a session, whether or not the portal is on.
	 * The CSRF check then returns 0 for anonymous requests, which is what
	 * turns an unauthenticated POST into a 403.
	 */
	if (!require_auth(req, res))
		goto done;

	if (!csrf_verify(req, &form)) {
		route_csrf_error(req, res);
		goto done;
	}

	if (method_override(&form) == METHOD_DELETE) {
		page_do_delete(req, res, name);
		goto done;
	}

	if ((action = form_get(&form, "action")) == NULL || !v_action(action))
		action = "save";

	if (!strcmp(action, "rename")) {
		page_do_rename(req, res, name, &form);
	} else if (!strcmp(action, "restore")) {
		stamp = 0;
		stampstr = form_get(&form, "stamp");
		if (stampstr != NULL && v_number(stampstr))
			stamp = str_tonum(stampstr, 10, 0, INT64_MAX, &err);
		page_restore(req, res, name, (time_t)stamp);
	} else if (!strcmp(action, "preview")) {
		page_edit_preview(req, res, name, &form);
	} else {
		page_save(req, res, name, &form);
	}

done:
	form_free(&form);
}

void
route_page_new_get(ecewo_request_t *req, ecewo_response_t *res)
{
	if (!require_login(req, res))
		return;

	page_new_form(req, res);
}

void
route_page_new_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	form;

	if (route_form(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	/*
	 * Only require_login here, not require_auth -- but csrf_verify()
	 * returns 0 for anonymous requests, so an unauthenticated POST still
	 * gets a 403 rather than creating a page.
	 */
	if (!require_login(req, res))
		goto done;

	if (!csrf_verify(req, &form)) {
		route_csrf_error(req, res);
		goto done;
	}

	page_create(req, res, &form);

done:
	form_free(&form);
}
