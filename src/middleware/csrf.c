/*
 * didimdwiki - CSRF tokens.
 *
 * One token per session, issued at login and embedded in every form.
 */

#include <string.h>

#include "ecewo.h"

#include "app.h"
#include "form.h"
#include "middleware.h"
#include "models.h"
#include "strutil.h"
#include "validate.h"

const char *
csrf_token(ecewo_request_t *req)
{
	struct req_ctx	*ctx = ctx_get(req);

	return (ctx->authed ? ctx->session.csrf : "");
}

/*
 * Returns 0 for an unauthenticated request rather than skipping the check.
 * That is what makes an anonymous form POST a 403 instead of falling through
 * to the handler, and it is relied on by the /wiki create path, which only
 * calls require_login().
 */
int
csrf_verify(ecewo_request_t *req, const struct form *form)
{
	struct req_ctx	*ctx;
	const char	*supplied;

	ctx = ctx_get(req);

	if (!ctx->authed)
		return (0);

	if ((supplied = form_get(form, "csrf")) == NULL)
		return (0);

	if (!v_token(supplied))
		return (0);

	return (str_timingsafe_equal(ctx->session.csrf, supplied));
}
