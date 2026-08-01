/*
 * didimdwiki - account controller.
 */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ecewo.h"

#include "app.h"
#include "controllers.h"
#include "middleware.h"
#include "models.h"
#include "validate.h"
#include "views.h"
#include "xmalloc.h"

static void	notice(struct view *, const char *, int);

/* Renders a notice block, or clears the placeholder when there is none. */
static void
notice(struct view *v, const char *text, int is_error)
{
	struct wbuf	buf;

	if (text == NULL) {
		view_set_lit(v, "NOTICE", "");
		return;
	}

	wbuf_init(&buf, 256);
	wbuf_appendf(&buf, "<p class=\"notice%s\">", is_error ? " error" : "");
	html_escape(&buf, text, strlen(text));
	buf_lit(&buf, "</p>");

	view_set_raw(v, "NOTICE", buf.data, buf.offset);
	wbuf_cleanup(&buf);
}

void
user_login_form(ecewo_request_t *req, ecewo_response_t *res, const char *next,
    const char *message)
{
	struct view	*v;

	if (next == NULL || next[0] != '/')
		next = "/";

	v = view_new("login.html");

	view_title(v, "Log in");
	view_set(v, "NEXT", next);
	notice(v, message, 1);

	/*
	 * The first account is always allowed even when registration is
	 * closed, so the link has to follow the same rule as the /register
	 * gate rather than the flag alone.
	 */
	if (app_cfg.allow_registration || user_count() == 0) {
		view_set_lit(v, "REGISTER_LINK",
		    "<p class=\"meta\">No account? "
		    "<a href=\"/register\">Create one</a>.</p>");
	} else {
		view_set_lit(v, "REGISTER_LINK", "");
	}

	view_render(req, res, ECEWO_OK, v);
}

void
user_do_login(ecewo_request_t *req, ecewo_response_t *res,
    const struct form *form)
{
	struct user	 u;
	const char	*username, *password, *next;

	next = form_get(form, "next");

	username = form_get(form, "username");
	password = form_get(form, "password");

	if (username == NULL || password == NULL || !v_username(username) ||
	    !v_password(password)) {
		user_login_form(req, res, next,
		    "Enter a username and password.");
		return;
	}

	if (user_verify(username, password, &u) == -1) {
		/* One message for both cases: no account enumeration. */
		app_log(LOG_INFO, "failed login for '%s'", username);
		user_login_form(req, res, next,
		    "Incorrect username or password.");
		return;
	}

	auth_login(req, res, &u);

	/* Only ever redirect somewhere on this site. */
	if (next == NULL || next[0] != '/')
		next = "/";

	view_redirect(res, next);
}

void
user_register_form(ecewo_request_t *req, ecewo_response_t *res,
    const char *message)
{
	struct view	*v;

	v = view_new("register.html");

	view_title(v, "Create an account");
	notice(v, message, 1);

	view_render(req, res, ECEWO_OK, v);
}

void
user_do_register(ecewo_request_t *req, ecewo_response_t *res,
    const struct form *form)
{
	struct user	 u;
	const char	*username, *password, *password2;
	int		 ret;

	username = form_get(form, "username");
	password = form_get(form, "password");
	password2 = form_get(form, "password2");

	if (username == NULL || !v_username(username)) {
		user_register_form(req, res,
		    "Usernames are 2 to 127 letters, digits, dot, dash "
		    "or underscore.");
		return;
	}

	if (password == NULL || !v_password(password)) {
		user_register_form(req, res,
		    "Passwords must be at least 8 characters.");
		return;
	}

	if (password2 == NULL || strcmp(password, password2)) {
		user_register_form(req, res, "The passwords did not match.");
		return;
	}

	ret = user_create(username, password);

	if (ret == -2) {
		user_register_form(req, res, "That username is taken.");
		return;
	}

	if (ret == -1) {
		user_register_form(req, res, "Could not create the account.");
		return;
	}

	if (user_lookup(username, &u) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "The account was created but could not be loaded.");
		return;
	}

	auth_login(req, res, &u);
	view_redirect(res, "/");
}

void
user_account_page(ecewo_request_t *req, ecewo_response_t *res,
    const char *message, const char *error)
{
	struct view	*v;
	struct req_ctx	*ctx;
	char		 date[64];

	ctx = ctx_get(req);

	if (!ctx->authed) {
		view_redirect(res, "/login");
		return;
	}

	fmt_date(ctx->user.created, date, sizeof(date));

	v = view_new("account.html");

	view_title(v, "Account");
	view_set(v, "NAME", ctx->user.name);
	view_set(v, "CREATED", date);
	view_set(v, "CSRF", ctx->session.csrf);
	notice(v, (error != NULL) ? error : message, error != NULL);

	view_render(req, res, ECEWO_OK, v);
}

void
user_change_password(ecewo_request_t *req, ecewo_response_t *res,
    const struct form *form)
{
	struct req_ctx	*ctx;
	const char	*old, *new, *new2;

	ctx = ctx_get(req);

	if (!ctx->authed) {
		view_redirect(res, "/login");
		return;
	}

	old = form_get(form, "oldpassword");
	new = form_get(form, "password");
	new2 = form_get(form, "password2");

	if (old == NULL || new == NULL || new2 == NULL ||
	    !v_password(old) || !v_password(new)) {
		user_account_page(req, res, NULL,
		    "Fill in every password field. New passwords need at "
		    "least 8 characters.");
		return;
	}

	if (strcmp(new, new2)) {
		user_account_page(req, res, NULL,
		    "The new passwords did not match.");
		return;
	}

	if (user_verify(ctx->user.name, old, NULL) == -1) {
		user_account_page(req, res, NULL,
		    "The current password was wrong.");
		return;
	}

	if (user_set_password(ctx->user.name, new) == -1) {
		user_account_page(req, res, NULL,
		    "Could not change the password.");
		return;
	}

	user_account_page(req, res, "Password changed.", NULL);
}
