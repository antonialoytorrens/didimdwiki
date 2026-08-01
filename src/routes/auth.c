/*
 * didimdwiki - /login, /logout, /register, /account.
 */

#include <sys/types.h>

#include <string.h>

#include "ecewo.h"

#include "app.h"
#include "controllers.h"
#include "middleware.h"
#include "models.h"
#include "routes.h"
#include "validate.h"
#include "views.h"

void
route_login_get(ecewo_request_t *req, ecewo_response_t *res)
{
	const char	*next;

	if (ctx_authed(req)) {
		view_redirect(res, "/");
		return;
	}

	next = ecewo_query(req, "next");

	user_login_form(req, res, next, NULL);
}

void
route_login_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	form;

	if (route_form(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	user_do_login(req, res, &form);

	form_free(&form);
}

void
route_logout(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	form;

	/*
	 * GET skips the CSRF check so the nav link works. Logging someone out
	 * is not a state change worth protecting, and auth_logout() is
	 * idempotent.
	 */
	if (!strcmp(ecewo_req_method(req), "POST")) {
		if (route_form(req, &form) == -1) {
			view_error(req, res, ECEWO_BAD_REQUEST,
			    "Malformed form data.");
			return;
		}

		if (!csrf_verify(req, &form)) {
			form_free(&form);
			view_error(req, res, ECEWO_FORBIDDEN,
			    "Invalid or missing CSRF token.");
			return;
		}

		form_free(&form);
	}

	auth_logout(req, res);
	view_redirect(res, "/");
}

/*
 * The first account is always allowed through, even with registration
 * closed, or a fresh wiki could never be bootstrapped.
 */
static int
registration_closed(ecewo_request_t *req, ecewo_response_t *res)
{
	if (!app_cfg.allow_registration && user_count() > 0) {
		view_error(req, res, ECEWO_FORBIDDEN,
		    "Registration is closed on this wiki.");
		return (1);
	}

	return (0);
}

void
route_register_get(ecewo_request_t *req, ecewo_response_t *res)
{
	if (registration_closed(req, res))
		return;

	user_register_form(req, res, NULL);
}

void
route_register_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	form;

	if (registration_closed(req, res))
		return;

	if (route_form(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	user_do_register(req, res, &form);

	form_free(&form);
}

void
route_account_get(ecewo_request_t *req, ecewo_response_t *res)
{
	user_account_page(req, res, NULL, NULL);
}

void
route_account_post(ecewo_request_t *req, ecewo_response_t *res)
{
	struct form	 form;
	const char	*action;

	if (route_form(req, &form) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Malformed form data.");
		return;
	}

	if (!csrf_verify(req, &form)) {
		view_error(req, res, ECEWO_FORBIDDEN,
		    "Invalid or missing CSRF token.");
		goto done;
	}

	if ((action = form_get(&form, "action")) == NULL || !v_action(action))
		action = "";

	if (!strcmp(action, "password"))
		user_change_password(req, res, &form);
	else
		user_account_page(req, res, NULL, "Unknown action.");

done:
	form_free(&form);
}
