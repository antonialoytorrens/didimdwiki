/*
 * didimdwiki - express-style method override.
 * HTML forms can only GET and POST, so a _method field carries the real verb.
 *
 * ecewo dispatches on the method before the handler runs, so there is
 * nothing left to rewrite -- this reports the effective method and the POST
 * handler branches on it.
 */

#include <strings.h>

#include "app.h"
#include "form.h"
#include "middleware.h"
#include "validate.h"

int
method_override(const struct form *form)
{
	const char	*value;

	if ((value = form_get(form, "_method")) == NULL)
		return (METHOD_POST);

	if (!v_method(value))
		return (METHOD_POST);

	if (!strcasecmp(value, "put"))
		return (METHOD_PUT);
	if (!strcasecmp(value, "patch"))
		return (METHOD_PATCH);
	if (!strcasecmp(value, "delete"))
		return (METHOD_DELETE);

	return (METHOD_POST);
}
