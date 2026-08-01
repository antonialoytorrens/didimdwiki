/*
 * didimdwiki - scratch data directory helpers for the integration suites.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "test_support.h"

static void	rm_recursive(const char *);
static int	mkdir_p(const char *);

int
test_chdir_scratch(char *out, size_t len)
{
	char	tmpl[] = "/tmp/didimdwiki-test-XXXXXX";

	if (mkdtemp(tmpl) == NULL)
		return (-1);

	if (strlen(tmpl) >= len)
		return (-1);

	memcpy(out, tmpl, strlen(tmpl) + 1);

	/* The models resolve every path relative to the process cwd. */
	if (chdir(out) == -1)
		return (-1);

	return (0);
}

void
test_rm_scratch(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return;

	/* Refuse anything that is not one of our own mkdtemp names. */
	if (strncmp(path, "/tmp/didimdwiki-test-", 21))
		return;

	if (chdir("/") == -1)
		return;

	rm_recursive(path);
}

static void
rm_recursive(const char *path)
{
	DIR		*d;
	struct dirent	*dp;
	struct stat	 st;
	char		 child[1024];

	if ((d = opendir(path)) == NULL) {
		unlink(path);
		return;
	}

	while ((dp = readdir(d)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

		if (snprintf(child, sizeof(child), "%s/%s", path,
		    dp->d_name) >= (int)sizeof(child))
			continue;

		if (lstat(child, &st) == -1)
			continue;

		if (S_ISDIR(st.st_mode))
			rm_recursive(child);
		else
			unlink(child);
	}

	closedir(d);
	rmdir(path);
}

static int
mkdir_p(const char *path)
{
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		return (-1);

	return (0);
}

int
test_init_data_dirs(void)
{
	static const char	*dirs[] = {
		DATA_DIR, PAGES_DIR, REVS_DIR, UPLOADS_DIR,
		UPLOADS_META_DIR, TEMPLATES_DIR, BODY_TMP_DIR, NULL
	};
	int			 i;

	for (i = 0; dirs[i] != NULL; i++) {
		if (mkdir_p(dirs[i]) == -1)
			return (-1);
	}

	return (0);
}
