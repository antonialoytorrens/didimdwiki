/*
 * didimdwiki - /files routes.
 */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>

#include "ecewo.h"
#include "ecewo-multipart.h"

#include "app.h"
#include "controllers.h"
#include "middleware.h"
#include "models.h"
#include "routes.h"
#include "validate.h"
#include "views.h"

/*
 * One /files route serves both the multipart upload form and the plain
 * urlencoded rename/delete forms, so read the fields out of whichever
 * representation the request actually used.
 */
int
route_form_any(ecewo_request_t *req, struct form *form)
{
	const ecewo_multipart_t		*mp;
	const ecewo_multipart_field_t	*field;
	size_t				 i, n;

	if ((mp = ecewo_multipart_get(req)) == NULL)
		return (route_form(req, form));

	form_init(form);

	n = ecewo_multipart_field_count(mp);

	for (i = 0; i < n; i++) {
		if ((field = ecewo_multipart_field_at(mp, i)) == NULL)
			continue;

		(void)form_add(form, ecewo_multipart_field_name(field),
		    ecewo_multipart_field_value(field));
	}

	return (0);
}

void
route_files_index_get(ecewo_request_t *req, ecewo_response_t *res)
{
	if (!require_login(req, res))
		return;

	file_index(req, res);
}

void
route_files_index_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	 form;
	const char	*action;

	if (route_form_any(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	if (!require_login(req, res))
		goto done;

	if (!csrf_verify(req, &form)) {
		route_csrf_error(req, res);
		goto done;
	}

	if ((action = form_get(&form, "action")) != NULL &&
	    strcmp(action, "upload")) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Unknown action.");
		goto done;
	}

	file_upload(req, res);

done:
	form_free(&form);
}

void
route_files_get(ecewo_request_t *req, ecewo_response_t *res)
{
	const char	*name;

	if ((name = route_name(req)) == NULL) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Invalid file name.");
		return;
	}

	if (!require_login(req, res))
		return;

	if (route_flag(req, "edit"))
		file_edit_form(req, res, name);
	else
		file_show(req, res, name);
}

void
route_files_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	 form;
	const char	*name, *action;

	if ((name = route_name(req)) == NULL) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Invalid file name.");
		return;
	}

	if (route_form_any(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	if (!require_login(req, res))
		goto done;

	if (!csrf_verify(req, &form)) {
		route_csrf_error(req, res);
		goto done;
	}

	if (method_override(&form) == METHOD_DELETE) {
		file_do_delete(req, res, name);
		goto done;
	}

	if ((action = form_get(&form, "action")) == NULL || !v_action(action))
		action = "";

	if (!strcmp(action, "rename"))
		file_do_rename(req, res, name, &form);
	else if (!strcmp(action, "replace"))
		file_replace(req, res, name);
	else
		file_edit_form(req, res, name);

done:
	form_free(&form);
}
