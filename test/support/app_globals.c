/*
 * didimdwiki - app-wide configuration that normally lives in src/main.c.
 * main.c is not linked into the tests: it pulls in ecewo and the whole route
 * table, and nothing under test needs either.
 */

#include "app.h"

struct app_config	app_cfg = {
	.allow_registration = 1,
	.camelcase_links = 0,
	.tls_enabled = 0,
	.login_required = 0,
	.bind = "127.0.0.1",
	.port = 8888,
};
