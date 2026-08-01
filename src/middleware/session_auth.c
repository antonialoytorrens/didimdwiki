/*
 * didimdwiki - session middleware.
 *
 * Global middleware: ecewo_use() runs it for every request, it resolves the
 * session cookie once, and handlers read the result out of the request
 * context.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecewo.h"
#include "ecewo-cookie.h"

#include "app.h"
#include "helpers.h"
#include "middleware.h"
#include "models.h"
#include "xmalloc.h"

#define CTX_KEY	"didimdwiki.ctx"

static struct req_ctx	*ctx_load(ecewo_request_t *);
static int		 redirect_login(ecewo_request_t *, ecewo_response_t *);
static void		 session_cookie_send(ecewo_response_t *, const char *,
			    int);

static struct req_ctx *
ctx_load(ecewo_request_t *req)
{
	struct req_ctx	*ctx;
	const char	*token;

	if ((ctx = ecewo_context_get(req, CTX_KEY)) != NULL)
		return (ctx);

	/*
	 * Request-arena allocated: released when the response is sent, so
	 * there is nothing to free and nothing that can outlive the request.
	 */
	ctx = ecewo_alloc(ecewo_req_arena(req), sizeof(*ctx));
	if (ctx == NULL)
		fatal("ctx_load: request arena exhausted");

	memset(ctx, 0, sizeof(*ctx));
	ecewo_context_set(req, CTX_KEY, ctx);

	if ((token = ecewo_cookie_get(req, SESSION_COOKIE)) == NULL)
		return (ctx);

	if (session_lookup(token, &ctx->session) == -1)
		return (ctx);

	if (user_lookup(ctx->session.user, &ctx->user) == -1) {
		/* Session outlived its account. */
		memset(&ctx->session, 0, sizeof(ctx->session));
		return (ctx);
	}

	ctx->authed = 1;

	return (ctx);
}

void
ctx_middleware(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next)
{
	(void)ctx_load(req);
	next(req, res);
}

struct req_ctx *
ctx_get(ecewo_request_t *req)
{
	return (ctx_load(req));
}

int
ctx_authed(ecewo_request_t *req)
{
	return (ctx_load(req)->authed);
}

const char *
ctx_user(ecewo_request_t *req)
{
	struct req_ctx	*ctx = ctx_load(req);

	return (ctx->authed ? ctx->user.name : "");
}

static int
redirect_login(ecewo_request_t *req, ecewo_response_t *res)
{
	char	 loc[PATH_MAX_LEN + 32];
	char	*enc;

	enc = url_encode_str(ecewo_req_path(req));
	(void)snprintf(loc, sizeof(loc), "/login?next=%s", enc);
	free(enc);

	ecewo_redirect(res, ECEWO_SEE_OTHER, loc);

	return (0);
}

/*
 * The "portal" gate. When login_required is set, redirects unauthenticated
 * requests to /login?next=<current-path> and returns 0. Returns 1 otherwise.
 */
int
require_login(ecewo_request_t *req, ecewo_response_t *res)
{
	if (!app_cfg.login_required || ctx_authed(req))
		return (1);

	return (redirect_login(req, res));
}

/*
 * Always requires a session. Use for write actions (edit/rename/delete)
 * regardless of login_required.
 */
int
require_auth(ecewo_request_t *req, ecewo_response_t *res)
{
	if (ctx_authed(req))
		return (1);

	return (redirect_login(req, res));
}

/*
 * Secure is omitted when TLS is off, or the browser would never send the
 * cookie back. max_age of 0 deletes.
 */
static void
session_cookie_send(ecewo_response_t *res, const char *value, int max_age)
{
	ecewo_cookie_options_t	*opts;

	if ((opts = ecewo_cookie_options_new()) == NULL) {
		app_log(LOG_ERR, "ecewo_cookie_options_new failed");
		return;
	}

	ecewo_cookie_options_set_path(opts, "/");
	ecewo_cookie_options_set_max_age(opts, max_age);
	ecewo_cookie_options_set_http_only(opts, 1);
	ecewo_cookie_options_set_same_site(opts, ECEWO_COOKIE_SAMESITE_LAX);
	ecewo_cookie_options_set_secure(opts, app_cfg.tls_enabled ? 1 : 0);

	if (ecewo_cookie_set(res, SESSION_COOKIE, value, opts) != 0)
		app_log(LOG_ERR, "failed to set the session cookie");

	ecewo_cookie_options_free(opts);
}

void
auth_login(ecewo_request_t *req, ecewo_response_t *res, const struct user *u)
{
	struct req_ctx	*ctx;

	ctx = ctx_load(req);

	if (session_create(u->name, &ctx->session) == -1) {
		app_log(LOG_ERR, "session_create failed for %s", u->name);
		return;
	}

	ctx->user = *u;
	ctx->authed = 1;

	session_cookie_send(res, ctx->session.token, SESSION_TTL);
}

void
auth_logout(ecewo_request_t *req, ecewo_response_t *res)
{
	struct req_ctx	*ctx;

	ctx = ctx_load(req);

	if (ctx->session.token[0] != '\0')
		(void)session_destroy(ctx->session.token);

	memset(ctx, 0, sizeof(*ctx));

	session_cookie_send(res, "", 0);
}
