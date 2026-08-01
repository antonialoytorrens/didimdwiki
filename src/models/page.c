/*
 * didimdwiki - wiki pages.
 * One data/pages/<Name>.md per page, with revision snapshots and redirects for
 * renames.
 */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "models.h"

#define REDIR_EXT	".redir"
#define REDIR_MAX_HOPS	3

static int	page_file(char *, size_t, const char *);
static int	page_redir_file(char *, size_t, const char *);

static int
page_file(char *out, size_t len, const char *name)
{
	return (store_path(out, len, PAGES_DIR, name, PAGE_EXT));
}

static int
page_redir_file(char *out, size_t len, const char *name)
{
	return (store_path(out, len, PAGES_DIR, name, REDIR_EXT));
}

int
page_exists(const char *name)
{
	char	path[PATH_MAX_LEN];

	if (page_file(path, sizeof(path), name) == -1)
		return (0);

	return (store_exists(path));
}

int
page_stat(const char *name, off_t *size, time_t *mtime)
{
	char	path[PATH_MAX_LEN];

	if (page_file(path, sizeof(path), name) == -1)
		return (-1);

	return (store_stat(path, size, mtime));
}

int
page_read(const char *name, struct wbuf *out)
{
	char	path[PATH_MAX_LEN];

	if (page_file(path, sizeof(path), name) == -1)
		return (-1);

	return (store_read(path, out));
}

int
page_write(const char *name, const char *author, const void *data, size_t len)
{
	char	path[PATH_MAX_LEN];
	char	rpath[PATH_MAX_LEN];
	int	ret;

	if (page_file(path, sizeof(path), name) == -1)
		return (-1);

	if (len > MAX_PAGE_BYTES)
		return (-1);

	/* Snapshot the outgoing text before it is overwritten. */
	if (store_exists(path))
		(void)revision_snapshot(name, author);

	ret = store_write(path, data, len);

	/*
	 * If a redirect from this name existed (left by a previous rename),
	 * it is now stale: the page lives here again.  Remove it so that a
	 * future deletion of this page does not resurrect the old redirect.
	 */
	if (ret == 0 && page_redir_file(rpath, sizeof(rpath), name) == 0 &&
	    store_exists(rpath))
		(void)store_unlink(rpath);

	return (ret);
}

int
page_rename(const char *from, const char *to, const char *author)
{
	char	src[PATH_MAX_LEN], dst[PATH_MAX_LEN];
	int	ret;

	if (page_file(src, sizeof(src), from) == -1)
		return (-1);
	if (page_file(dst, sizeof(dst), to) == -1)
		return (-1);

	if (!strcmp(from, to))
		return (0);

	if (!store_exists(src))
		return (-1);

	if (store_exists(dst))
		return (-2);	/* target taken */

	(void)revision_snapshot(from, author);

	ret = store_rename(src, dst);
	if (ret == 0) {
		char	rpath[PATH_MAX_LEN];

		(void)revision_rename(from, to);

		if (page_redir_file(rpath, sizeof(rpath), from) == 0)
			(void)store_write(rpath, to, strlen(to));
	}

	return (ret);
}

int
page_delete(const char *name, const char *author)
{
	char	path[PATH_MAX_LEN];
	int	ret;

	if (page_file(path, sizeof(path), name) == -1)
		return (-1);

	if (!store_exists(path))
		return (-1);

	/* Keep the history: a deleted page can be recreated from a revision. */
	(void)revision_snapshot(name, author);

	ret = store_unlink(path);

	return (ret);
}

int
page_list(int sort, struct store_list *out)
{
	return (store_list(PAGES_DIR, PAGE_EXT, sort, out));
}

/* Resolve .redir chain into dst.  Returns 0 on success, -1 otherwise. */
int
page_redirect_lookup(const char *name, char *dst, size_t dstlen)
{
	struct wbuf	 buf;
	char		 path[PATH_MAX_LEN];
	char		 cur[NAME_MAX_LEN];
	char		 nxt[NAME_MAX_LEN];
	size_t		 nlen;
	int		 hops;

	if (!store_valid_name(name))
		return (-1);

	(void)snprintf(cur, sizeof(cur), "%s", name);

	for (hops = 0; hops < REDIR_MAX_HOPS; hops++) {
		if (page_redir_file(path, sizeof(path), cur) == -1)
			return (-1);

		if (!store_exists(path)) {
			/* cur has no .redir — it is the final destination. */
			if (hops == 0)
				return (-1);	/* original name has no redirect */
			if ((size_t)snprintf(dst, dstlen, "%s", cur) >= dstlen)
				return (-1);
			return (0);
		}

		wbuf_init(&buf, NAME_MAX_LEN);
		if (store_read(path, &buf) == -1) {
			wbuf_cleanup(&buf);
			return (-1);
		}
		(void)wbuf_stringify(&buf, NULL);

		/* Strip trailing CR/LF that editors might add. */
		((char *)buf.data)[strcspn((char *)buf.data, "\r\n")] = '\0';
		nlen = strlen((char *)buf.data);

		if (nlen == 0 || nlen >= NAME_MAX_LEN ||
		    !store_valid_name((char *)buf.data)) {
			wbuf_cleanup(&buf);
			return (-1);
		}

		(void)snprintf(nxt, sizeof(nxt), "%s", (char *)buf.data);
		wbuf_cleanup(&buf);

		/* Detect a loop back to the original name. */
		if (!strcmp(nxt, name))
			return (-1);

		(void)snprintf(cur, sizeof(cur), "%s", nxt);
	}

	/* Chain exceeded REDIR_MAX_HOPS without finding a terminal page. */
	return (-1);
}
