/*
 * didimdwiki - filesystem primitives.
 * The only module that touches the data directory directly.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <strings.h>

#include "app.h"
#include "models.h"
#include "strutil.h"
#include "xmalloc.h"

static int	store_cmp_name(const void *, const void *);
static int	store_cmp_mtime(const void *, const void *);

/*
 * Names are single path components: no separators, no traversal, no dotfiles
 * (the revision and metadata sidecars live in dot-directories and must stay
 * unreachable from a URL).
 */
int
store_valid_name(const char *name)
{
	size_t		len, i;

	if (name == NULL)
		return (0);

	len = strlen(name);
	if (len == 0 || len >= NAME_MAX_LEN)
		return (0);

	if (name[0] == '.' || name[0] == '-')
		return (0);

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return (0);

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];

		if (c < 0x20 || c == 0x7f)
			return (0);
		if (c == '/' || c == '\\' || c == ':')
			return (0);

		if (isalnum(c) || c == '.' || c == '_' || c == '-' ||
		    c == '+' || c == '(' || c == ')' || c == ' ')
			continue;

		/* Allow UTF-8 continuation bytes so non-ASCII titles work. */
		if (c >= 0x80)
			continue;

		return (0);
	}

	/* No "..", even embedded: "a..b" is fine but "a/../b" never gets here. */
	if (strstr(name, "..") != NULL)
		return (0);

	/* Trailing space or dot confuses some filesystems. */
	if (name[len - 1] == ' ' || name[len - 1] == '.')
		return (0);

	return (1);
}

/*
 * Build dir/name+ext after validating name. Returns 0 on success.
 */
int
store_path(char *out, size_t len, const char *dir, const char *name,
    const char *ext)
{
	int		ret;

	if (!store_valid_name(name))
		return (-1);

	if (ext == NULL)
		ext = "";

	ret = snprintf(out, len, "%s/%s%s", dir, name, ext);
	if (ret < 0 || (size_t)ret >= len)
		return (-1);

	return (0);
}

int
store_exists(const char *path)
{
	struct stat	st;

	if (stat(path, &st) == -1)
		return (0);

	return (S_ISREG(st.st_mode));
}

int
store_stat(const char *path, off_t *size, time_t *mtime)
{
	struct stat	st;

	if (stat(path, &st) == -1)
		return (-1);

	if (size != NULL)
		*size = st.st_size;
	if (mtime != NULL)
		*mtime = st.st_mtime;

	return (0);
}

int
store_read(const char *path, struct wbuf *out)
{
	int		fd;
	ssize_t		r;
	uint8_t	tmp[8192];

	if ((fd = open(path, O_RDONLY | O_NOFOLLOW)) == -1)
		return (-1);

	for (;;) {
		r = read(fd, tmp, sizeof(tmp));
		if (r == -1) {
			if (errno == EINTR)
				continue;
			close(fd);
			return (-1);
		}
		if (r == 0)
			break;
		wbuf_append(out, tmp, (size_t)r);
	}

	close(fd);
	return (0);
}

/*
 * Write via a temp file plus rename so a reader never sees a half-written
 * page. With the fcntl lock gone this is the only write-safety mechanism,
 * and it also covers an editor touching data/pages by hand.
 */
int
store_write(const char *path, const void *data, size_t len)
{
	int		fd, ret;
	size_t		off;
	ssize_t		w;
	char		tmp[PATH_MAX_LEN];

	ret = snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
	if (ret < 0 || (size_t)ret >= sizeof(tmp))
		return (-1);

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0640);
	if (fd == -1)
		return (-1);

	off = 0;
	while (off < len) {
		w = write(fd, (const uint8_t *)data + off, len - off);
		if (w == -1) {
			if (errno == EINTR)
				continue;
			goto fail;
		}
		off += (size_t)w;
	}

	if (fsync(fd) == -1)
		goto fail;
	if (close(fd) == -1) {
		fd = -1;
		goto fail;
	}

	if (rename(tmp, path) == -1) {
		fd = -1;
		goto fail;
	}

	return (0);

fail:
	if (fd != -1)
		close(fd);
	unlink(tmp);
	return (-1);
}

int
store_copy(const char *src, const char *dst)
{
	int		ret;
	struct wbuf	buf;

	wbuf_init(&buf, 4096);

	if (store_read(src, &buf) == -1) {
		wbuf_cleanup(&buf);
		return (-1);
	}

	ret = store_write(dst, buf.data, buf.offset);
	wbuf_cleanup(&buf);

	return (ret);
}

int
store_rename(const char *from, const char *to)
{
	return (rename(from, to) == -1 ? -1 : 0);
}

int
store_unlink(const char *path)
{
	if (unlink(path) == -1 && errno != ENOENT)
		return (-1);

	return (0);
}

/*
 * List regular files in dir. When suffix is set only matching entries are
 * returned and the suffix is stripped from the reported name.
 */
int
store_list(const char *dir, const char *suffix, int sort,
    struct store_list *out)
{
	DIR		*d;
	struct dirent	*dp;
	struct stat	st;
	size_t		cap, slen, nlen;
	char		full[PATH_MAX_LEN];
	struct store_ent *ents, *tmp;

	out->ents = NULL;
	out->count = 0;

	if ((d = opendir(dir)) == NULL)
		return (errno == ENOENT ? 0 : -1);

	cap = 32;
	ents = xcalloc(cap, sizeof(*ents));
	slen = (suffix != NULL) ? strlen(suffix) : 0;

	while ((dp = readdir(d)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		nlen = strlen(dp->d_name);

		if (slen > 0) {
			if (nlen <= slen)
				continue;
			if (strcmp(dp->d_name + nlen - slen, suffix))
				continue;
			nlen -= slen;
		}

		if (nlen >= NAME_MAX_LEN)
			continue;

		if (snprintf(full, sizeof(full), "%s/%s", dir,
		    dp->d_name) >= (int)sizeof(full))
			continue;

		if (stat(full, &st) == -1 || !S_ISREG(st.st_mode))
			continue;

		if (out->count == cap) {
			cap *= 2;
			tmp = xcalloc(cap, sizeof(*ents));
			memcpy(tmp, ents, out->count * sizeof(*ents));
			free(ents);
			ents = tmp;
		}

		memcpy(ents[out->count].name, dp->d_name, nlen);
		ents[out->count].name[nlen] = '\0';
		ents[out->count].size = st.st_size;
		ents[out->count].mtime = st.st_mtime;
		out->count++;
	}

	closedir(d);

	if (out->count > 0) {
		qsort(ents, out->count, sizeof(*ents),
		    (sort == STORE_SORT_MTIME) ? store_cmp_mtime :
		    store_cmp_name);
	}

	out->ents = ents;
	return (0);
}

void
store_list_free(struct store_list *list)
{
	if (list->ents != NULL)
		free(list->ents);

	list->ents = NULL;
	list->count = 0;
}

static int
store_cmp_name(const void *a, const void *b)
{
	const struct store_ent	*ea = a, *eb = b;

	return (strcasecmp(ea->name, eb->name));
}

static int
store_cmp_mtime(const void *a, const void *b)
{
	const struct store_ent	*ea = a, *eb = b;

	if (ea->mtime < eb->mtime)
		return (1);
	if (ea->mtime > eb->mtime)
		return (-1);

	return (strcasecmp(ea->name, eb->name));
}

/*
 * Reduce an untrusted upload filename to a safe component. Never used to
 * bypass store_valid_name(): the result is validated by the caller too.
 */
int
store_slugify(const char *in, char *out, size_t len)
{
	size_t		i, o;
	int		dash;
	unsigned char	c;

	if (in == NULL || len < 2)
		return (-1);

	/* Drop any directory part a client may have sent. */
	for (i = strlen(in); i > 0; i--) {
		if (in[i - 1] == '/' || in[i - 1] == '\\') {
			in += i;
			break;
		}
	}

	o = 0;
	dash = 0;

	for (i = 0; in[i] != '\0' && o < len - 1; i++) {
		c = (unsigned char)in[i];

		if (isalnum(c) || c == '.' || c == '_' || c == '+') {
			if (c == '.') {
				/* Never emit a dotfile, or "name-.ext". */
				if (o == 0)
					continue;
				while (o > 0 && out[o - 1] == '-')
					o--;
				if (o == 0)
					continue;
			}
			out[o++] = (char)c;
			dash = 0;
		} else if (o > 0 && dash == 0) {
			out[o++] = '-';
			dash = 1;
		}
	}

	while (o > 0 && (out[o - 1] == '-' || out[o - 1] == '.'))
		o--;

	out[o] = '\0';

	if (o == 0) {
		if (str_lcpy(out, "file", len) >= len)
			return (-1);
	}

	return (store_valid_name(out) ? 0 : -1);
}

/*
 * Verify that all required data directories exist.
 * They are created by the systemd service before startup via StateDirectory
 * and ExecStartPre; this is a fast-fail check that catches misconfigured
 * deployments at startup rather than on the first write.
 */
int
store_verify_dirs(void)
{
	static const char	*dirs[] = {
		DATA_DIR, PAGES_DIR, REVS_DIR, UPLOADS_DIR,
		UPLOADS_META_DIR, TEMPLATES_DIR, BODY_TMP_DIR, NULL
	};
	struct stat		 st;
	int			 i;

	for (i = 0; dirs[i] != NULL; i++) {
		if (stat(dirs[i], &st) == -1 || !S_ISDIR(st.st_mode)) {
			app_log(LOG_ERR, "required directory missing: %s",
			    dirs[i]);
			return (-1);
		}
	}

	return (0);
}
