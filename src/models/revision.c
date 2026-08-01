/*
 * didimdwiki - revision history.
 * data/pages/.rev/<Name>/<unixts>.<author>.md
 *
 * One directory per page: listing a page's history is a readdir of that
 * directory rather than a filtered scan of every revision in the wiki, and
 * renaming carries the whole history across in a single rename(2).
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "models.h"
#include "strutil.h"
#include "xmalloc.h"

static int	revision_dir(char *, size_t, const char *);
static int	revision_path(char *, size_t, const char *, time_t,
		    const char *);
static int	revision_scan(const char *, struct revision_list *);
static int	revision_find(const char *, time_t, char *, size_t);
static int	revision_cmp(const void *, const void *);
static void	revision_trim(const char *);

/* REVS_DIR/<name> */
static int
revision_dir(char *out, size_t len, const char *name)
{
	return (store_path(out, len, REVS_DIR, name, NULL));
}

/* REVS_DIR/<name>/<stamp>.<author>.md */
static int
revision_path(char *out, size_t len, const char *name, time_t stamp,
    const char *author)
{
	int	ret;

	if (!store_valid_name(name))
		return (-1);

	ret = snprintf(out, len, "%s/%s/%lld.%s.md", REVS_DIR, name,
	    (long long)stamp,
	    (author != NULL && author[0] != '\0') ? author : "anon");

	if (ret < 0 || (size_t)ret >= len)
		return (-1);

	return (0);
}

/*
 * Read one page's revision directory. A page with no history has no
 * directory at all, which is not an error.
 */
static int
revision_scan(const char *name, struct revision_list *out)
{
	DIR		*d;
	struct dirent	*dp;
	struct stat	 st;
	struct revision	*revs, *tmp;
	char		 dir[PATH_MAX_LEN], full[PATH_MAX_LEN];
	char		 entry[PATH_MAX_LEN];
	const char	*author;
	char		*dot;
	size_t		 nlen, cap;
	int		 err;

	out->revs = NULL;
	out->count = 0;

	if (revision_dir(dir, sizeof(dir), name) == -1)
		return (-1);

	if ((d = opendir(dir)) == NULL)
		return (errno == ENOENT ? 0 : -1);

	cap = 32;
	revs = xcalloc(cap, sizeof(*revs));

	while ((dp = readdir(d)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		nlen = strlen(dp->d_name);

		/* "<stamp>.<author>.md" */
		if (nlen < 4 || strcmp(dp->d_name + nlen - 3, ".md") != 0)
			continue;

		if (nlen - 3 >= sizeof(entry))
			continue;

		if (snprintf(full, sizeof(full), "%s/%s", dir,
		    dp->d_name) >= (int)sizeof(full))
			continue;

		if (stat(full, &st) == -1 || !S_ISREG(st.st_mode))
			continue;

		/* entry = "<stamp>.<author>" */
		memcpy(entry, dp->d_name, nlen - 3);
		entry[nlen - 3] = '\0';

		if ((dot = strchr(entry, '.')) == NULL)
			continue;

		*dot = '\0';
		author = dot + 1;

		if (author[0] == '\0')
			continue;

		if (out->count == cap) {
			cap *= 2;
			tmp = xcalloc(cap, sizeof(*revs));
			memcpy(tmp, revs, out->count * sizeof(*revs));
			free(revs);
			revs = tmp;
		}

		revs[out->count].stamp = (time_t)str_tonum(entry, 10, 0,
		    INT64_MAX, &err);
		if (err != STR_OK)
			continue;

		revs[out->count].size = st.st_size;

		if (str_lcpy(revs[out->count].author, author,
		    sizeof(revs[out->count].author)) >=
		    sizeof(revs[out->count].author))
			continue;

		out->count++;
	}

	closedir(d);

	if (out->count > 0)
		qsort(revs, out->count, sizeof(*revs), revision_cmp);

	out->revs = revs;
	return (0);
}

static int
revision_cmp(const void *a, const void *b)
{
	const struct revision	*ra = a, *rb = b;

	if (ra->stamp < rb->stamp)
		return (1);	/* newest first */
	if (ra->stamp > rb->stamp)
		return (-1);

	return (0);
}

/* Copies the live page into the revision store. */
int
revision_snapshot(const char *name, const char *author)
{
	char	src[PATH_MAX_LEN], dst[PATH_MAX_LEN], dir[PATH_MAX_LEN];
	time_t	stamp;

	if (store_path(src, sizeof(src), PAGES_DIR, name, PAGE_EXT) == -1)
		return (-1);

	if (!store_exists(src))
		return (-1);

	if (revision_dir(dir, sizeof(dir), name) == -1)
		return (-1);

	if (mkdir(dir, 0750) == -1 && errno != EEXIST)
		return (-1);

	/*
	 * Stamps are the revision identity, so two saves in the same second
	 * must not collide: step forward until the slot is free.
	 */
	stamp = time(NULL);

	for (;;) {
		if (revision_path(dst, sizeof(dst), name, stamp, author) == -1)
			return (-1);

		if (!store_exists(dst))
			break;

		stamp++;
	}

	if (store_copy(src, dst) == -1)
		return (-1);

	revision_trim(name);

	return (0);
}

int
revision_list(const char *name, struct revision_list *out)
{
	return (revision_scan(name, out));
}

void
revision_list_free(struct revision_list *list)
{
	free(list->revs);

	list->revs = NULL;
	list->count = 0;
}

/*
 * Locate a revision by stamp. The author is part of the filename and is not
 * known to the caller, so this is still a scan -- but only of one page's
 * directory.
 */
static int
revision_find(const char *name, time_t stamp, char *out, size_t len)
{
	DIR		*d;
	struct dirent	*dp;
	char		 dir[PATH_MAX_LEN], prefix[64];
	size_t		 plen;
	int		 found;

	if (revision_dir(dir, sizeof(dir), name) == -1)
		return (-1);

	if (snprintf(prefix, sizeof(prefix), "%lld.",
	    (long long)stamp) >= (int)sizeof(prefix))
		return (-1);
	plen = strlen(prefix);

	found = -1;

	if ((d = opendir(dir)) == NULL)
		return (-1);

	while ((dp = readdir(d)) != NULL) {
		if (strncmp(dp->d_name, prefix, plen) != 0)
			continue;

		if (snprintf(out, len, "%s/%s", dir, dp->d_name) < (int)len) {
			found = 0;
			break;
		}
	}

	closedir(d);
	return (found);
}

int
revision_read(const char *name, time_t stamp, struct wbuf *out)
{
	char	path[PATH_MAX_LEN];

	if (revision_find(name, stamp, path, sizeof(path)) == -1)
		return (-1);

	return (store_read(path, out));
}

/* Drop a page's entire history. */
int
revision_purge(const char *name)
{
	DIR		*d;
	struct dirent	*dp;
	char		 dir[PATH_MAX_LEN], path[PATH_MAX_LEN];

	if (revision_dir(dir, sizeof(dir), name) == -1)
		return (-1);

	if ((d = opendir(dir)) == NULL)
		return (errno == ENOENT ? 0 : -1);

	while ((dp = readdir(d)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		if (snprintf(path, sizeof(path), "%s/%s", dir,
		    dp->d_name) >= (int)sizeof(path))
			continue;

		(void)store_unlink(path);
	}

	closedir(d);

	if (rmdir(dir) == -1 && errno != ENOENT)
		return (-1);

	return (0);
}

/* Carry a page's history across a rename: one directory rename. */
int
revision_rename(const char *from, const char *to)
{
	char	src[PATH_MAX_LEN], dst[PATH_MAX_LEN];

	if (revision_dir(src, sizeof(src), from) == -1)
		return (-1);
	if (revision_dir(dst, sizeof(dst), to) == -1)
		return (-1);

	/* No history yet is not a failure. */
	if (access(src, F_OK) == -1)
		return (0);

	return (store_rename(src, dst));
}

/* Keep the newest REVISIONS_KEPT snapshots. */
static void
revision_trim(const char *name)
{
	char			 path[PATH_MAX_LEN];
	size_t			 i;
	struct revision_list	 revs;

	if (revision_scan(name, &revs) == -1)
		return;

	if (revs.count <= REVISIONS_KEPT) {
		revision_list_free(&revs);
		return;
	}

	for (i = REVISIONS_KEPT; i < revs.count; i++) {
		if (revision_path(path, sizeof(path), name,
		    revs.revs[i].stamp, revs.revs[i].author) == -1)
			continue;

		(void)store_unlink(path);
	}

	revision_list_free(&revs);
}
