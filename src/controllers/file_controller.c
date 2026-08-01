/*
 * didimdwiki - file controller.
 */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ecewo.h"
#include "ecewo-multipart.h"

#include "app.h"
#include "controllers.h"
#include "middleware.h"
#include "models.h"
#include "strutil.h"
#include "validate.h"
#include "views.h"
#include "xmalloc.h"

static int	file_receive(ecewo_request_t *, const char *, char *, size_t,
		    off_t *);
static void	file_common(ecewo_request_t *, struct view *,
		    const struct upload_meta *);
static void	file_receive_error(ecewo_request_t *, ecewo_response_t *, int);

/*
 * Takes the uploaded part and moves it into the store. `name` is NULL for a
 * new upload (derive it from the client filename) or set when replacing an
 * existing file.
 *
 * ecewo-multipart has already streamed the body to a staging file under
 * BODY_TMP_DIR, so there is no read/write loop here. The plugin hands us
 * ownership of that staging file and never cleans it up, so every rejection
 * path below has to unlink it.
 *
 * Returns 0, or -1 I/O, -2 unusable filename, -3 refused type, -4 too large.
 */
static int
file_receive(ecewo_request_t *req, const char *name, char *out, size_t outlen,
    off_t *written)
{
	const ecewo_multipart_file_t	*file;
	const char			*staged, *client_name;
	int				 ret;

	*written = 0;

	if ((file = ecewo_multipart_get_file(req, "file")) == NULL)
		return (-1);

	if ((staged = ecewo_multipart_file_path(file)) == NULL) {
		/*
		 * Only happens if the part stayed in memory, which means the
		 * stream middleware was not in the chain for this route.
		 */
		app_log(LOG_ERR, "multipart part has no staged file; is "
		    "ecewo_body_stream missing from the route?");
		return (-1);
	}

	if (name != NULL) {
		if (str_lcpy(out, name, outlen) >= outlen) {
			(void)unlink(staged);
			return (-1);
		}
	} else {
		/*
		 * The client filename is attacker controlled. It never
		 * reaches the filesystem -- ecewo-multipart names the staging
		 * file itself -- but it is still the basis for the stored
		 * name, so it is slugified and then revalidated.
		 */
		client_name = ecewo_multipart_file_filename(file);

		if (client_name == NULL ||
		    store_slugify(client_name, out, outlen) == -1) {
			(void)unlink(staged);
			return (-2);
		}
	}

	if (!upload_mime_allowed(out)) {
		(void)unlink(staged);
		return (-3);
	}

	if ((ret = upload_adopt(staged, out, written)) != 0) {
		if (ret == -1)
			app_log(LOG_ERR, "upload_adopt(%s -> %s): %s", staged,
			    out, strerror(errno));
		(void)unlink(staged);
		return (ret);
	}

	return (0);
}

static void
file_receive_error(ecewo_request_t *req, ecewo_response_t *res, int ret)
{
	switch (ret) {
	case -2:
		view_error(req, res, ECEWO_BAD_REQUEST,
		    "That filename could not be used. Rename the file and retry.");
		break;
	case -3:
		view_error(req, res, ECEWO_FORBIDDEN,
		    "That file type is not accepted: HTML, SVG and scripts "
		    "would run in the browser under this site's origin.");
		break;
	case -4:
		view_error(req, res, ECEWO_PAYLOAD_TOO_LARGE,
		    "That file is too large.");
		break;
	default:
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "The upload failed.");
		break;
	}
}

static void
file_common(ecewo_request_t *req, struct view *v,
    const struct upload_meta *meta)
{
	char	*encoded, date[64], size[32];

	view_set(v, "NAME", meta->name);

	encoded = url_encode_str(meta->name);
	view_set(v, "NAME_URL", encoded);
	free(encoded);

	fmt_date(meta->mtime, date, sizeof(date));
	fmt_size(meta->size, size, sizeof(size));

	view_set(v, "ORIGINAL", meta->original);
	view_set(v, "MIME", meta->mime);
	view_set(v, "SIZE", size);
	view_set(v, "MODIFIED", date);
	view_set(v, "OWNER", meta->owner);
	view_set(v, "CSRF", csrf_token(req));

	view_title(v, meta->name);
}

void
file_index(ecewo_request_t *req, ecewo_response_t *res)
{
	struct view		*v;
	struct wbuf		 rows;
	struct upload_meta	*metas;
	size_t			 i, count;
	int			 sort;
	const char		*sortarg;
	char			 date[64], size[32], *encoded;

	sort = STORE_SORT_MTIME;
	if ((sortarg = ecewo_query(req, "sort")) != NULL && v_sort(sortarg) &&
	    !strcmp(sortarg, "name"))
		sort = STORE_SORT_NAME;

	if (upload_list(sort, &metas, &count) == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not list the file store.");
		return;
	}

	wbuf_init(&rows, 2048);

	for (i = 0; i < count; i++) {
		fmt_date(metas[i].mtime, date, sizeof(date));
		fmt_size(metas[i].size, size, sizeof(size));
		encoded = url_encode_str(metas[i].name);

		wbuf_appendf(&rows, "<tr><td><a href=\"/files/%s\">", encoded);
		html_escape(&rows, metas[i].name, strlen(metas[i].name));
		buf_lit(&rows, "</a></td><td>");
		html_escape(&rows, metas[i].mime, strlen(metas[i].mime));
		wbuf_appendf(&rows, "</td><td>%s</td><td>%s</td><td>", size,
		    date);
		html_escape(&rows, metas[i].owner, strlen(metas[i].owner));
		wbuf_appendf(&rows,
		    "</td><td><a href=\"/files/%s?edit=1\">manage</a></td></tr>",
		    encoded);

		free(encoded);
	}

	v = view_new("file_index.html");

	view_title(v, "Files");
	view_setf(v, "COUNT", "%zu", count);
	view_set(v, "CSRF", csrf_token(req));
	view_set_raw(v, "ROWS", rows.data, rows.offset);
	view_set_lit(v, "NOTICE", "");

	if (count == 0)
		view_set_lit(v, "EMPTY",
		    "<p class=\"meta\">No files uploaded yet.</p>");
	else
		view_set_lit(v, "EMPTY", "");

	wbuf_cleanup(&rows);
	free(metas);

	view_render(req, res, ECEWO_OK, v);
}

void
file_show(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct wbuf		 data;
	struct upload_meta	 meta;
	char			 path[PATH_MAX_LEN], disp[256];

	if (upload_meta_read(name, &meta) == -1) {
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That file does not exist.");
		return;
	}

	if (upload_path(path, sizeof(path), name) == -1) {
		view_error(req, res, ECEWO_BAD_REQUEST, "Invalid file name.");
		return;
	}

	wbuf_init(&data, 8192);

	if (store_read(path, &data) == -1) {
		wbuf_cleanup(&data);
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That file does not exist.");
		return;
	}

	/* nosniff plus an explicit type: the browser never guesses for us. */
	(void)snprintf(disp, sizeof(disp), "inline; filename=\"%s\"",
	    meta.name);

	ecewo_header_set(res, "content-type", meta.mime);
	ecewo_header_set(res, "content-disposition", disp);
	ecewo_header_set(res, "x-content-type-options", "nosniff");
	ecewo_send(res, ECEWO_OK, data.data, data.offset);

	wbuf_cleanup(&data);
}

void
file_edit_form(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	struct view		*v;
	struct upload_meta	 meta;
	char			*encoded;

	if (!require_login(req, res))
		return;

	if (upload_meta_read(name, &meta) == -1) {
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That file does not exist.");
		return;
	}

	v = view_new("file_edit.html");
	file_common(req, v, &meta);
	view_set_lit(v, "NOTICE", "");

	if (!strncmp(meta.mime, "image/", 6)) {
		struct wbuf	img;

		encoded = url_encode_str(meta.name);
		wbuf_init(&img, 256);
		wbuf_appendf(&img, "<img src=\"/files/%s\" alt=\"\">", encoded);
		view_set_raw(v, "PREVIEW", img.data, img.offset);
		wbuf_cleanup(&img);
		free(encoded);
	} else {
		view_set_lit(v, "PREVIEW", "");
	}

	view_render(req, res, ECEWO_OK, v);
}

void
file_upload(ecewo_request_t *req, ecewo_response_t *res)
{
	char	name[NAME_MAX_LEN];
	off_t	written;
	int	ret;

	ret = file_receive(req, NULL, name, sizeof(name), &written);

	if (ret != 0) {
		file_receive_error(req, res, ret);
		return;
	}

	(void)upload_finish(name, name, ctx_user(req), written);

	view_redirectf(res, "/files/%s?edit=1", name);
}

void
file_replace(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	char	stored[NAME_MAX_LEN];
	off_t	written;
	int	ret;

	if (!upload_exists(name)) {
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That file does not exist.");
		return;
	}

	ret = file_receive(req, name, stored, sizeof(stored), &written);

	if (ret != 0) {
		file_receive_error(req, res, ret);
		return;
	}

	(void)upload_finish(stored, stored, ctx_user(req), written);

	view_redirectf(res, "/files/%s?edit=1", stored);
}

void
file_do_rename(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    const struct form *form)
{
	const char	*newname;
	int		 ret;

	if ((newname = form_get(form, "newname")) == NULL ||
	    !v_filename(newname)) {
		view_error(req, res, ECEWO_BAD_REQUEST,
		    "That file name is not allowed.");
		return;
	}

	ret = upload_rename(name, newname);

	if (ret == -2) {
		view_error(req, res, ECEWO_CONFLICT,
		    "A file with that name already exists.");
		return;
	}

	if (ret == -3) {
		view_error(req, res, ECEWO_FORBIDDEN,
		    "That file extension is not accepted.");
		return;
	}

	if (ret == -1) {
		view_error(req, res, ECEWO_INTERNAL_SERVER_ERROR,
		    "Could not rename the file.");
		return;
	}

	view_redirectf(res, "/files/%s?edit=1", newname);
}

void
file_do_delete(ecewo_request_t *req, ecewo_response_t *res, const char *name)
{
	if (upload_delete(name) == -1) {
		view_error(req, res, ECEWO_NOT_FOUND,
		    "That file does not exist.");
		return;
	}

	view_redirect(res, "/files");
}
