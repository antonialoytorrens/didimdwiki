/*
 * didimdwiki - uploaded files.
 * data/uploads/<name> with a data/uploads/.meta/<name>.tsv sidecar.
 *
 * Names are slugified and the type is decided server side from the extension.
 */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include "app.h"
#include "models.h"
#include "strutil.h"
#include "xmalloc.h"

static const struct {
	const char	*ext;
	const char	*mime;
} mime_table[] = {
	{ "png",  "image/png" },
	{ "jpg",  "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "gif",  "image/gif" },
	{ "webp", "image/webp" },
	{ "svg",  "image/svg+xml" },
	{ "ico",  "image/vnd.microsoft.icon" },
	{ "pdf",  "application/pdf" },
	{ "txt",  "text/plain; charset=utf-8" },
	{ "md",   "text/plain; charset=utf-8" },
	{ "csv",  "text/csv; charset=utf-8" },
	{ "json", "application/json" },
	{ "xml",  "application/xml" },
	{ "zip",  "application/zip" },
	{ "gz",   "application/gzip" },
	{ "bz2",  "application/x-bzip2" },
	{ "xz",   "application/x-xz" },
	{ "tar",  "application/x-tar" },
	{ "odt",  "application/vnd.oasis.opendocument.text" },
	{ "ods",  "application/vnd.oasis.opendocument.spreadsheet" },
	{ "mp3",  "audio/mpeg" },
	{ "ogg",  "audio/ogg" },
	{ "wav",  "audio/wav" },
	{ "mp4",  "video/mp4" },
	{ "webm", "video/webm" },
	{ NULL,   NULL }
};

static int	upload_meta_path(char *, size_t, const char *);

int
upload_path(char *out, size_t len, const char *name)
{
	return (store_path(out, len, UPLOADS_DIR, name, NULL));
}

static int
upload_meta_path(char *out, size_t len, const char *name)
{
	return (store_path(out, len, UPLOADS_META_DIR, name, ".tsv"));
}

/*
 * Extension driven, never the client's Content-Type. Unknown types are served
 * as a download rather than being guessed at.
 */
const char *
upload_mime_for(const char *name)
{
	const char	*ext;
	int		 i;

	if ((ext = strrchr(name, '.')) == NULL)
		return ("application/octet-stream");

	ext++;

	for (i = 0; mime_table[i].ext != NULL; i++) {
		if (!strcasecmp(ext, mime_table[i].ext))
			return (mime_table[i].mime);
	}

	return ("application/octet-stream");
}

/* HTML, JS and SVG are refused to avoid stored XSS when served same-origin. */
int
upload_mime_allowed(const char *name)
{
	const char	*ext;

	if ((ext = strrchr(name, '.')) == NULL)
		return (1);

	ext++;

	if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm") ||
	    !strcasecmp(ext, "js") || !strcasecmp(ext, "mjs") ||
	    !strcasecmp(ext, "svg") || !strcasecmp(ext, "xhtml") ||
	    !strcasecmp(ext, "php") || !strcasecmp(ext, "so"))
		return (0);

	return (1);
}

int
upload_exists(const char *name)
{
	char	path[PATH_MAX_LEN];

	if (upload_path(path, sizeof(path), name) == -1)
		return (0);

	return (store_exists(path));
}

int
upload_meta_write(const struct upload_meta *meta)
{
	char		path[PATH_MAX_LEN];
	struct wbuf	buf;
	int		ret;

	if (upload_meta_path(path, sizeof(path), meta->name) == -1)
		return (-1);

	wbuf_init(&buf, 256);
	wbuf_appendf(&buf, "%s\t%s\t%s\t%s\t%lld\t%lld\n", meta->name,
	    meta->original, meta->mime, meta->owner, (long long)meta->size,
	    (long long)meta->mtime);

	ret = store_write(path, buf.data, buf.offset);
	wbuf_cleanup(&buf);

	return (ret);
}

int
upload_meta_read(const char *name, struct upload_meta *out)
{
	FILE	*fp;
	char	 path[PATH_MAX_LEN], line[1024];
	char	*fields[7];
	int	 count, err;
	off_t	 size;
	time_t	 mtime;

	if (upload_path(path, sizeof(path), name) == -1)
		return (-1);

	if (store_stat(path, &size, &mtime) == -1)
		return (-1);

	memset(out, 0, sizeof(*out));
	str_lcpy(out->name, name, sizeof(out->name));
	str_lcpy(out->original, name, sizeof(out->original));
	str_lcpy(out->mime, upload_mime_for(name), sizeof(out->mime));
	str_lcpy(out->owner, "-", sizeof(out->owner));
	out->size = size;
	out->mtime = mtime;

	if (upload_meta_path(path, sizeof(path), name) == -1)
		return (0);

	/* The file itself is the source of truth; the sidecar only adds to it. */
	if ((fp = fopen(path, "r")) == NULL)
		return (0);

	if (fgets(line, sizeof(line), fp) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		count = str_split(line, "\t", fields, 7);

		if (count >= 4) {
			str_lcpy(out->original, fields[1],
			    sizeof(out->original));
			str_lcpy(out->mime, fields[2], sizeof(out->mime));
			str_lcpy(out->owner, fields[3], sizeof(out->owner));
		}
		if (count >= 6) {
			out->mtime = (time_t)str_tonum(fields[5], 10, 0,
			    INT64_MAX, &err);
			if (err != STR_OK)
				out->mtime = mtime;
		}
	}

	fclose(fp);

	return (0);
}

int
upload_list(int sort, struct upload_meta **out, size_t *count)
{
	size_t			i;
	struct store_list	files;
	struct upload_meta	*metas;

	*out = NULL;
	*count = 0;

	if (store_list(UPLOADS_DIR, NULL, sort, &files) == -1)
		return (-1);

	if (files.count == 0) {
		store_list_free(&files);
		return (0);
	}

	metas = xcalloc(files.count, sizeof(*metas));

	for (i = 0; i < files.count; i++) {
		if (upload_meta_read(files.ents[i].name,
		    &metas[*count]) == 0)
			(*count)++;
	}

	store_list_free(&files);

	*out = metas;
	return (0);
}

/*
 * Move a completed multipart upload into place.
 *
 * ecewo-multipart streams the body to a staging file for us (see
 * ecewo_multipart_disk()), so the bytes are already on disk and this is a
 * rename plus the size check. The caller is responsible for unlinking the
 * staging file on every rejection path; the plugin hands us ownership and
 * will not.
 */
int
upload_adopt(const char *staged, const char *name, off_t *size)
{
	char		path[PATH_MAX_LEN];
	struct stat	st;

	if (upload_path(path, sizeof(path), name) == -1)
		return (-1);

	if (stat(staged, &st) == -1 || !S_ISREG(st.st_mode))
		return (-1);

	if (st.st_size > MAX_UPLOAD_BYTES)
		return (-4);

	if (rename(staged, path) == -1)
		return (-1);

	if (chmod(path, 0640) == -1)
		app_log(LOG_WARNING, "chmod(%s): %s", path, strerror(errno));

	if (size != NULL)
		*size = st.st_size;

	return (0);
}

int
upload_finish(const char *name, const char *original, const char *owner,
    off_t size)
{
	struct upload_meta	meta;

	memset(&meta, 0, sizeof(meta));

	str_lcpy(meta.name, name, sizeof(meta.name));
	str_lcpy(meta.original, (original != NULL) ? original : name,
	    sizeof(meta.original));
	str_lcpy(meta.mime, upload_mime_for(name), sizeof(meta.mime));
	str_lcpy(meta.owner, (owner != NULL && owner[0] != '\0') ?
	    owner : "-", sizeof(meta.owner));

	meta.size = size;
	meta.mtime = time(NULL);

	return (upload_meta_write(&meta));
}

int
upload_rename(const char *from, const char *to)
{
	char			src[PATH_MAX_LEN], dst[PATH_MAX_LEN];
	int			ret;
	struct upload_meta	meta;

	if (upload_path(src, sizeof(src), from) == -1)
		return (-1);
	if (upload_path(dst, sizeof(dst), to) == -1)
		return (-1);

	if (!strcmp(from, to))
		return (0);

	if (!upload_mime_allowed(to))
		return (-3);

	if (!store_exists(src))
		return (-1);

	if (store_exists(dst))
		return (-2);

	/* Read before moving: upload_meta_read() stats the file itself. */
	if (upload_meta_read(from, &meta) == -1)
		return (-1);

	if ((ret = store_rename(src, dst)) == 0) {
		char	old_meta[PATH_MAX_LEN];

		str_lcpy(meta.name, to, sizeof(meta.name));
		str_lcpy(meta.mime, upload_mime_for(to), sizeof(meta.mime));
		(void)upload_meta_write(&meta);

		if (upload_meta_path(old_meta, sizeof(old_meta), from) == 0)
			(void)store_unlink(old_meta);
	}

	return (ret);
}

int
upload_delete(const char *name)
{
	char	path[PATH_MAX_LEN], meta[PATH_MAX_LEN];
	int	ret;

	if (upload_path(path, sizeof(path), name) == -1)
		return (-1);

	if (!store_exists(path))
		return (-1);

	ret = store_unlink(path);

	if (upload_meta_path(meta, sizeof(meta), name) == 0)
		(void)store_unlink(meta);

	return (ret);
}
