/*
 * didimdwiki - middleware: session auth, CSRF, method override.
 *
 * There is no declarative auth or validation layer -- these are ordinary
 * calls made at the top of each handler.
 */

#ifndef DIDIMDWIKI_MIDDLEWARE_H
#define DIDIMDWIKI_MIDDLEWARE_H

#include "ecewo.h"

#include "app.h"
#include "form.h"

/* ------------------------------------------------------------------ auth */

/*
 * Global middleware, registered with ecewo_use(app, NULL, ...). Resolves the
 * session cookie once per request and stashes a struct req_ctx so handlers
 * can call ctx_authed()/ctx_user() without touching the session file again.
 */
void	ctx_middleware(ecewo_request_t *, ecewo_response_t *, ecewo_next_t);

struct req_ctx	*ctx_get(ecewo_request_t *);
int		 ctx_authed(ecewo_request_t *);
const char	*ctx_user(ecewo_request_t *);

/*
 * Soft gate: only blocks when login_required (the "portal") is on.
 * Returns 1 to continue, or 0 having already sent a 303 to /login.
 */
int	require_login(ecewo_request_t *, ecewo_response_t *);

/* Hard gate: always blocks unauthenticated requests. Same return contract. */
int	require_auth(ecewo_request_t *, ecewo_response_t *);

void	auth_login(ecewo_request_t *, ecewo_response_t *, const struct user *);
void	auth_logout(ecewo_request_t *, ecewo_response_t *);

/* ------------------------------------------------------------------ csrf */

const char	*csrf_token(ecewo_request_t *);

/*
 * Per-session token, constant-time compare. Returns 0 for an unauthenticated
 * request, which is what makes an anonymous form POST a 403 rather than a
 * 401.
 */
int		 csrf_verify(ecewo_request_t *, const struct form *);

/* --------------------------------------------------------- method override */

#define METHOD_POST	0
#define METHOD_PUT	1
#define METHOD_PATCH	2
#define METHOD_DELETE	3

/*
 * Reads _method from a POSTed form. Routing has already happened by the time
 * a handler runs, so this cannot rewrite the request method -- it returns the
 * effective one and the handler branches on it.
 */
int	method_override(const struct form *);

#endif /* !DIDIMDWIKI_MIDDLEWARE_H */
