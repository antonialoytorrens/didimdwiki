/*
 * didimdwiki - HTML template loading.
 *
 * kodev compiled everything under assets/ into the .so as asset_<name>_<ext>
 * symbols, keyed on basename. ecewo has no asset pipeline, so the templates
 * are read from disk once at startup into the app-lifetime arena and looked
 * up by the same basename.
 *
 * Reading them at startup rather than per request keeps the request path
 * free of disk I/O, and still lets an operator edit a template and restart
 * without a rebuild -- which the compiled-in version could not do.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "models.h"
#include "views.h"
#include "wbuf.h"
#include "xmalloc.h"

#define TEMPLATES_MAX	32

struct template {
	char		name[64];
	const char	*data;
	size_t		len;
};

static struct template	templates[TEMPLATES_MAX];
static size_t		template_count;

int
templates_load(ecewo_app_t *app, const char *dir)
{
	DIR		*d;
	struct dirent	*dp;
	struct wbuf	 buf;
	ecewo_arena_t	*arena;
	char		 path[PATH_MAX_LEN];
	size_t		 nlen;
	char		*copy;

	arena = ecewo_app_arena(app);
	template_count = 0;

	if ((d = opendir(dir)) == NULL) {
		app_log(LOG_ERR, "cannot open template directory %s: %s",
		    dir, strerror(errno));
		return (-1);
	}

	while ((dp = readdir(d)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		nlen = strlen(dp->d_name);
		if (nlen >= sizeof(templates[0].name))
			continue;

		if (snprintf(path, sizeof(path), "%s/%s", dir,
		    dp->d_name) >= (int)sizeof(path))
			continue;

		if (!store_exists(path))
			continue;

		if (template_count == TEMPLATES_MAX) {
			app_log(LOG_ERR, "too many templates in %s (max %d)",
			    dir, TEMPLATES_MAX);
			closedir(d);
			return (-1);
		}

		wbuf_init(&buf, 4096);
		if (store_read(path, &buf) == -1) {
			app_log(LOG_ERR, "cannot read template %s", path);
			wbuf_cleanup(&buf);
			closedir(d);
			return (-1);
		}

		/*
		 * Copied into the app arena: it outlives every request and is
		 * released when ecewo_run() returns, so nothing here is freed
		 * by hand.
		 */
		copy = ecewo_alloc(arena, buf.offset + 1);
		if (copy == NULL) {
			wbuf_cleanup(&buf);
			closedir(d);
			return (-1);
		}

		memcpy(copy, buf.data, buf.offset);
		copy[buf.offset] = '\0';

		memcpy(templates[template_count].name, dp->d_name, nlen + 1);
		templates[template_count].data = copy;
		templates[template_count].len = buf.offset;
		template_count++;

		wbuf_cleanup(&buf);
	}

	closedir(d);

	if (template_count == 0) {
		app_log(LOG_ERR, "no templates found in %s", dir);
		return (-1);
	}

	/*
	 * Every render wraps itself in layout.html, and view_error() is the
	 * path that reports other failures -- so if either is missing, fail
	 * at startup rather than at the first request.
	 */
	if (template_get("layout.html", NULL) == NULL ||
	    template_get("error.html", NULL) == NULL) {
		app_log(LOG_ERR, "layout.html and error.html are required");
		return (-1);
	}

	app_log(LOG_INFO, "loaded %zu templates from %s", template_count, dir);

	return (0);
}

const char *
template_get(const char *name, size_t *len)
{
	size_t		i;

	for (i = 0; i < template_count; i++) {
		if (!strcmp(templates[i].name, name)) {
			if (len != NULL)
				*len = templates[i].len;
			return (templates[i].data);
		}
	}

	if (len != NULL)
		*len = 0;

	return (NULL);
}
