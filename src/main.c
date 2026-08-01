/*
 * didimdwiki - application bootstrap.
 *
 * Configuration, module setup and the route table all live here; there is no
 * config file, and settings arrive as key=value arguments (see
 * app_parse_config).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecewo.h"
#include "ecewo-helmet.h"
#include "ecewo-multipart.h"
#include "ecewo-static.h"

#include "app.h"
#include "middleware.h"
#include "models.h"
#include "routes.h"
#include "seed.h"
#include "strutil.h"
#include "views.h"
#include "xmalloc.h"

struct app_config	app_cfg = {
	.allow_registration = 1,
	.camelcase_links = 0,
	.tls_enabled = 0,
	.login_required = 0,
	.bind = "127.0.0.1",
	.port = 8888,
	.views_dir = VIEWS_DIR,
	.public_dir = PUBLIC_DIR,
};

static void	app_parse_config(int, char **);
static int	config_bool(const char *, int *);
static void	register_routes(ecewo_app_t *);
static int	install_helmet(ecewo_app_t *);
static void	on_shutdown(void *);

/*
 * Accepts key=1/yes/0/no. Returns 1 when the value was understood.
 */
static int
config_bool(const char *value, int *out)
{
	if (!strcmp(value, "1") || !strcmp(value, "yes") ||
	    !strcmp(value, "true")) {
		*out = 1;
		return (1);
	}

	if (!strcmp(value, "0") || !strcmp(value, "no") ||
	    !strcmp(value, "false")) {
		*out = 0;
		return (1);
	}

	return (0);
}

static void
app_parse_config(int argc, char **argv)
{
	const char	*env;
	char		*eq, *key, *value;
	long		 port;
	int		 i, err;

	/*
	 * Environment first, argv second, so a command line always wins.
	 */
	static const struct {
		const char	*env;
		const char	*key;
	} envmap[] = {
		{ "DIDIMDWIKI_LOGIN_REQUIRED",		"login_required" },
		{ "DIDIMDWIKI_ALLOW_REGISTRATION",	"allow_registration" },
		{ "DIDIMDWIKI_CAMELCASE_LINKS",		"camelcase_links" },
		{ "DIDIMDWIKI_TLS_ENABLED",		"tls_enabled" },
		{ NULL, NULL }
	};

	for (i = 0; envmap[i].env != NULL; i++) {
		if ((env = getenv(envmap[i].env)) == NULL)
			continue;

		if (!strcmp(envmap[i].key, "login_required"))
			(void)config_bool(env, &app_cfg.login_required);
		else if (!strcmp(envmap[i].key, "allow_registration"))
			(void)config_bool(env, &app_cfg.allow_registration);
		else if (!strcmp(envmap[i].key, "camelcase_links"))
			(void)config_bool(env, &app_cfg.camelcase_links);
		else
			(void)config_bool(env, &app_cfg.tls_enabled);
	}

	if ((env = getenv("DIDIMDWIKI_BIND")) != NULL)
		str_lcpy(app_cfg.bind, env, sizeof(app_cfg.bind));

	if ((env = getenv("DIDIMDWIKI_VIEWS_DIR")) != NULL)
		str_lcpy(app_cfg.views_dir, env, sizeof(app_cfg.views_dir));

	if ((env = getenv("DIDIMDWIKI_PUBLIC_DIR")) != NULL)
		str_lcpy(app_cfg.public_dir, env, sizeof(app_cfg.public_dir));

	if ((env = getenv("DIDIMDWIKI_PORT")) != NULL) {
		port = (long)str_tonum(env, 10, 1, 65535, &err);
		if (err == STR_OK)
			app_cfg.port = (unsigned short)port;
	}

	for (i = 1; i < argc; i++) {
		if ((eq = strchr(argv[i], '=')) == NULL) {
			app_log(LOG_WARNING, "ignoring argument '%s'", argv[i]);
			continue;
		}

		*eq = '\0';
		key = argv[i];
		value = eq + 1;

		if (!strcmp(key, "login_required")) {
			if (!config_bool(value, &app_cfg.login_required))
				goto bad;
		} else if (!strcmp(key, "allow_registration")) {
			if (!config_bool(value, &app_cfg.allow_registration))
				goto bad;
		} else if (!strcmp(key, "camelcase_links")) {
			if (!config_bool(value, &app_cfg.camelcase_links))
				goto bad;
		} else if (!strcmp(key, "tls_enabled")) {
			if (!config_bool(value, &app_cfg.tls_enabled))
				goto bad;
		} else if (!strcmp(key, "bind")) {
			str_lcpy(app_cfg.bind, value, sizeof(app_cfg.bind));
		} else if (!strcmp(key, "views_dir")) {
			str_lcpy(app_cfg.views_dir, value,
			    sizeof(app_cfg.views_dir));
		} else if (!strcmp(key, "public_dir")) {
			str_lcpy(app_cfg.public_dir, value,
			    sizeof(app_cfg.public_dir));
		} else if (!strcmp(key, "port")) {
			port = (long)str_tonum(value, 10, 1, 65535, &err);
			if (err != STR_OK)
				goto bad;
			app_cfg.port = (unsigned short)port;
		} else {
			app_log(LOG_WARNING, "unknown setting '%s'", key);
		}

		*eq = '=';
		continue;
bad:
		fatal("bad value for %s: %s", key, value);
	}
}

/*
 * helmet applies these to every response, not just to rendered HTML. All
 * assets are same-origin, so the CSP needs no exceptions.
 */
static int
install_helmet(ecewo_app_t *app)
{
	ecewo_helmet_config_t	*h;

	if ((h = ecewo_helmet_config_new()) == NULL)
		return (-1);

	ecewo_helmet_config_set_csp(h, "default-src 'self'; img-src 'self' data:");
	ecewo_helmet_config_set_frame_options(h, "SAMEORIGIN");
	ecewo_helmet_config_set_referrer_policy(h, "same-origin");
	ecewo_helmet_config_set_nosniff(h, true);
	ecewo_helmet_config_set_ie_no_open(h, true);

	/* HSTS would be a lie over plain HTTP, and this app speaks plain HTTP
	 * unless something in front of it terminates TLS. */
	ecewo_helmet_config_set_hsts(h,
	    app_cfg.tls_enabled ? "31536000" : NULL, false, false);

	/* Consumes the config on success and on failure alike. */
	return (ecewo_helmet_install(app, h));
}

static void
register_routes(ecewo_app_t *app)
{
	ECEWO_GET   (app, "/",            route_home);
	ECEWO_GET   (app, "/wiki",        route_page_new_get);
	ECEWO_POST  (app, "/wiki",        route_page_new_post);
	ECEWO_GET   (app, "/wiki/:name",  route_pages_get);
	ECEWO_POST  (app, "/wiki/:name",  route_pages_post);

	ECEWO_GET   (app, "/files",       route_files_index_get);
	ECEWO_POST  (app, "/files",       ecewo_body_stream, ecewo_multipart,
	    route_files_index_post);
	ECEWO_GET   (app, "/files/:name", route_files_get);
	ECEWO_POST  (app, "/files/:name", ecewo_body_stream, ecewo_multipart,
	    route_files_post);

	ECEWO_GET   (app, "/search",      route_search);
	ECEWO_GET   (app, "/index",       route_index);
	ECEWO_GET   (app, "/changes",     route_changes);
	ECEWO_GET   (app, "/changes.rss", route_changes_rss);

	ECEWO_GET   (app, "/login",       route_login_get);
	ECEWO_POST  (app, "/login",       route_login_post);
	ECEWO_GET   (app, "/logout",      route_logout);
	ECEWO_POST  (app, "/logout",      route_logout);
	ECEWO_GET   (app, "/register",    route_register_get);
	ECEWO_POST  (app, "/register",    route_register_post);
	ECEWO_GET   (app, "/account",     route_account_get);
	ECEWO_POST  (app, "/account",     route_account_post);

	/*
	 * GET reads are deliberately open even when the portal is on, and
	 * none of these carry a CSRF check -- the API is not form-driven.
	 */
	ECEWO_GET   (app, "/api/v1/pages",                  api_pages_list);
	ECEWO_POST  (app, "/api/v1/pages",                  api_pages_create);
	ECEWO_GET   (app, "/api/v1/pages/:name",            api_page_get);
	ECEWO_PUT   (app, "/api/v1/pages/:name",            api_page_put);
	ECEWO_PATCH (app, "/api/v1/pages/:name",            api_page_patch);
	ECEWO_DELETE(app, "/api/v1/pages/:name",            api_page_delete);
	ECEWO_GET   (app, "/api/v1/pages/:name/revisions",  api_page_revisions);

	ECEWO_GET   (app, "/api/v1/files",                  api_files_list);
	ECEWO_POST  (app, "/api/v1/files",       ecewo_body_stream,
	    ecewo_multipart, api_files_create);
	ECEWO_GET   (app, "/api/v1/files/:name",            api_file_get);
	ECEWO_PUT   (app, "/api/v1/files/:name", ecewo_body_stream,
	    ecewo_multipart, api_file_put);
	ECEWO_PATCH (app, "/api/v1/files/:name",            api_file_patch);
	ECEWO_DELETE(app, "/api/v1/files/:name",            api_file_delete);

	/* After every concrete API route, so it only catches the leftovers. */
	ECEWO_GET   (app, "/api/v1/*", api_not_found);
	ECEWO_POST  (app, "/api/v1/*", api_not_found);
	ECEWO_PUT   (app, "/api/v1/*", api_not_found);
	ECEWO_PATCH (app, "/api/v1/*", api_not_found);
	ECEWO_DELETE(app, "/api/v1/*", api_not_found);
}

static void
on_shutdown(void *user_data)
{
	(void)user_data;

	ecewo_static_cleanup();
}

int
main(int argc, char **argv)
{
	ecewo_app_t	*app;

	app_parse_config(argc, argv);

	/*
	 * Fail here rather than on the first write. The directories are
	 * created by the systemd unit (StateDirectory plus an ExecStartPre
	 * for the dot-subdirectories, which StateDirectory cannot express).
	 */
	if (store_verify_dirs() == -1)
		fatal("data directories missing; deploy via systemd or "
		    "create them manually");

	if ((app = ecewo_create()) == NULL)
		fatal("ecewo_create failed");

	if (ecewo_static_init() != 0)
		fatal("ecewo_static_init failed");

	if (templates_load(app, app_cfg.views_dir) == -1)
		fatal("could not load templates from %s", app_cfg.views_dir);

	app_seed();

	/*
	 * Uploads stage into BODY_TMP_DIR and are renamed into place by
	 * upload_adopt(); same filesystem, so the rename is atomic.
	 */
	if (ecewo_multipart_disk(app, BODY_TMP_DIR) != 0)
		fatal("could not configure multipart staging in %s",
		    BODY_TMP_DIR);

	if (ecewo_multipart_max_size(app, MAX_UPLOAD_BYTES) != 0)
		fatal("could not set the multipart size limit");

	if (install_helmet(app) != 0)
		fatal("ecewo_helmet_install failed");

	if (ecewo_use(app, NULL, ctx_middleware) != 0)
		fatal("could not register the session middleware");

	register_routes(app);

	/*
	 * After the routes: a mount also registers a wildcard beneath itself,
	 * and routes match in registration order.
	 */
	if (ecewo_serve_static(app, "/public", app_cfg.public_dir, NULL) != 0)
		fatal("could not mount %s", app_cfg.public_dir);

	if (ecewo_atexit(app, on_shutdown, NULL) != 0)
		fatal("could not register the shutdown hook");

	ecewo_set_listen_address(app, app_cfg.bind);

	app_log(LOG_INFO, "%s %s listening on %s:%u (login_required=%d, "
	    "allow_registration=%d)", APP_NAME, APP_VERSION, app_cfg.bind,
	    app_cfg.port, app_cfg.login_required, app_cfg.allow_registration);

	if (ecewo_listen(app, app_cfg.port) != 0)
		fatal("could not listen on %s:%u", app_cfg.bind, app_cfg.port);

	return (0);
}
