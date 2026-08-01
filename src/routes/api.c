/*
 * didimdwiki - /api/v1 JSON.
 *
 * Everything that mutates here needs a session. GET reads stay open even
 * when the portal is on, and there is no CSRF check because nothing here is
 * form-driven.
 */

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "ecewo.h"
#include "ecewo-multipart.h"

#include "app.h"
#include "middleware.h"
#include "models.h"
#include "routes.h"
#include "strutil.h"
#include "views.h"
#include "xmalloc.h"

static void	 api_error(ecewo_response_t *, int, const char *);
static void	 api_send(ecewo_response_t *, int, cJSON *);
static cJSON	*api_body_json(ecewo_request_t *);
static const char *api_json_string(const cJSON *, const char *);
static int	 api_require_auth(ecewo_request_t *, ecewo_response_t *);
static const char *api_name(ecewo_request_t *, ecewo_response_t *,
		     const char *);
static void	 api_file_store(ecewo_request_t *, ecewo_response_t *,
		     const char *, int);

static void
api_error(ecewo_response_t *res, int status, const char *message)
{
	cJSON	*root;

	if ((root = cJSON_CreateObject()) == NULL) {
		ecewo_send(res, ECEWO_INTERNAL_SERVER_ERROR, NULL, 0);
		return;
	}

	cJSON_AddStringToObject(root, "error", message);
	api_send(res, status, root);
}

/* Serialises, sends, and frees the tree. */
static void
api_send(ecewo_response_t *res, int status, cJSON *root)
{
	char	*text;

	if (root == NULL) {
		ecewo_send(res, ECEWO_INTERNAL_SERVER_ERROR, NULL, 0);
		return;
	}

	text = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);

	if (text == NULL) {
		ecewo_send(res, ECEWO_INTERNAL_SERVER_ERROR, NULL, 0);
		return;
	}

	ecewo_send_json(res, status, text);
	free(text);
}

/* Caller must cJSON_Delete() the result. */
static cJSON *
api_body_json(ecewo_request_t *req)
{
	const uint8_t	*body;
	size_t		 len;

	body = ecewo_req_body(req);
	len = ecewo_req_body_len(req);

	if (body == NULL || len == 0 || len > MAX_PAGE_BYTES)
		return (NULL);

	return (cJSON_ParseWithLength((const char *)body, len));
}

static const char *
api_json_string(const cJSON *root, const char *key)
{
	const cJSON	*item;

	if (root == NULL)
		return (NULL);

	item = cJSON_GetObjectItemCaseSensitive(root, key);

	if (!cJSON_IsString(item) || item->valuestring == NULL)
		return (NULL);

	return (item->valuestring);
}

static int
api_require_auth(ecewo_request_t *req, ecewo_response_t *res)
{
	if (ctx_authed(req))
		return (1);

	api_error(res, ECEWO_UNAUTHORIZED, "authentication required");

	return (0);
}

/* Validated :name, or NULL having already answered with a 400. */
static const char *
api_name(ecewo_request_t *req, ecewo_response_t *res, const char *what)
{
	const char	*name;

	if ((name = route_name(req)) == NULL) {
		api_error(res, ECEWO_BAD_REQUEST, what);
		return (NULL);
	}

	return (name);
}

/* ------------------------------------------------------------------ pages */

void
api_pages_list(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON			*root, *array, *item;
	struct store_list	 pages;
	size_t			 i;

	(void)req;

	if (page_list(STORE_SORT_MTIME, &pages) == -1) {
		api_error(res, ECEWO_INTERNAL_SERVER_ERROR, "cannot list pages");
		return;
	}

	root = cJSON_CreateObject();
	array = cJSON_AddArrayToObject(root, "pages");

	for (i = 0; i < pages.count; i++) {
		item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", pages.ents[i].name);
		cJSON_AddNumberToObject(item, "size",
		    (double)pages.ents[i].size);
		cJSON_AddNumberToObject(item, "modified",
		    (double)pages.ents[i].mtime);
		cJSON_AddItemToArray(array, item);
	}

	store_list_free(&pages);
	api_send(res, ECEWO_OK, root);
}

void
api_pages_create(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON		*json, *root;
	const char	*name, *body;

	if (!api_require_auth(req, res))
		return;

	if ((json = api_body_json(req)) == NULL) {
		api_error(res, ECEWO_BAD_REQUEST, "invalid json body");
		return;
	}

	name = api_json_string(json, "name");

	if (name == NULL || !store_valid_name(name)) {
		cJSON_Delete(json);
		api_error(res, ECEWO_BAD_REQUEST, "invalid page name");
		return;
	}

	if (page_exists(name)) {
		cJSON_Delete(json);
		api_error(res, ECEWO_CONFLICT, "page already exists");
		return;
	}

	if ((body = api_json_string(json, "body")) == NULL)
		body = "";

	if (page_write(name, ctx_user(req), body, strlen(body)) == -1) {
		cJSON_Delete(json);
		api_error(res, ECEWO_INTERNAL_SERVER_ERROR, "cannot write page");
		return;
	}

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", name);
	cJSON_AddBoolToObject(root, "created", 1);

	cJSON_Delete(json);
	api_send(res, ECEWO_CREATED, root);
}

void
api_page_get(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON		*root;
	struct wbuf	 body;
	const char	*name;

	if ((name = api_name(req, res, "invalid page name")) == NULL)
		return;

	if (!page_exists(name)) {
		api_error(res, ECEWO_NOT_FOUND, "no such page");
		return;
	}

	wbuf_init(&body, 4096);
	(void)page_read(name, &body);
	wbuf_stringify(&body, NULL);

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", name);
	cJSON_AddStringToObject(root, "body", (char *)body.data);

	wbuf_cleanup(&body);
	api_send(res, ECEWO_OK, root);
}

void
api_page_put(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON		*json, *root;
	const char	*name, *body;

	if ((name = api_name(req, res, "invalid page name")) == NULL)
		return;

	if (!api_require_auth(req, res))
		return;

	if ((json = api_body_json(req)) == NULL) {
		api_error(res, ECEWO_BAD_REQUEST, "invalid json body");
		return;
	}

	if ((body = api_json_string(json, "body")) == NULL) {
		cJSON_Delete(json);
		api_error(res, ECEWO_BAD_REQUEST, "body field required");
		return;
	}

	if (page_write(name, ctx_user(req), body, strlen(body)) == -1) {
		cJSON_Delete(json);
		api_error(res, ECEWO_INTERNAL_SERVER_ERROR, "cannot write page");
		return;
	}

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", name);
	cJSON_AddBoolToObject(root, "saved", 1);

	cJSON_Delete(json);
	api_send(res, ECEWO_OK, root);
}

void
api_page_patch(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON		*json, *root;
	const char	*name, *newname;
	int		 ret;

	if ((name = api_name(req, res, "invalid page name")) == NULL)
		return;

	if (!api_require_auth(req, res))
		return;

	if ((json = api_body_json(req)) == NULL) {
		api_error(res, ECEWO_BAD_REQUEST, "invalid json body");
		return;
	}

	newname = api_json_string(json, "name");

	if (newname == NULL || !store_valid_name(newname)) {
		cJSON_Delete(json);
		api_error(res, ECEWO_BAD_REQUEST, "invalid new name");
		return;
	}

	ret = page_rename(name, newname, ctx_user(req));

	if (ret != 0) {
		cJSON_Delete(json);
		api_error(res, (ret == -2) ? ECEWO_CONFLICT : ECEWO_NOT_FOUND,
		    (ret == -2) ? "target exists" : "no such page");
		return;
	}

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", newname);
	cJSON_AddBoolToObject(root, "renamed", 1);

	cJSON_Delete(json);
	api_send(res, ECEWO_OK, root);
}

void
api_page_delete(ecewo_request_t *req, ecewo_response_t *res)
{
	const char	*name;

	if ((name = api_name(req, res, "invalid page name")) == NULL)
		return;

	if (!api_require_auth(req, res))
		return;

	if (page_delete(name, ctx_user(req)) == -1) {
		api_error(res, ECEWO_NOT_FOUND, "no such page");
		return;
	}

	ecewo_send(res, ECEWO_NO_CONTENT, NULL, 0);
}

void
api_page_revisions(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON			*root, *array, *item;
	struct revision_list	 revs;
	const char		*name;
	size_t			 i;

	if ((name = api_name(req, res, "invalid page name")) == NULL)
		return;

	if (revision_list(name, &revs) == -1) {
		api_error(res, ECEWO_INTERNAL_SERVER_ERROR,
		    "cannot list revisions");
		return;
	}

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", name);
	array = cJSON_AddArrayToObject(root, "revisions");

	for (i = 0; i < revs.count; i++) {
		item = cJSON_CreateObject();
		cJSON_AddNumberToObject(item, "stamp",
		    (double)revs.revs[i].stamp);
		cJSON_AddStringToObject(item, "author", revs.revs[i].author);
		cJSON_AddNumberToObject(item, "size",
		    (double)revs.revs[i].size);
		cJSON_AddItemToArray(array, item);
	}

	revision_list_free(&revs);
	api_send(res, ECEWO_OK, root);
}

/* ------------------------------------------------------------------ files */

void
api_files_list(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON			*root, *array, *item;
	struct upload_meta	*metas;
	size_t			 i, count;

	(void)req;

	if (upload_list(STORE_SORT_MTIME, &metas, &count) == -1) {
		api_error(res, ECEWO_INTERNAL_SERVER_ERROR, "cannot list files");
		return;
	}

	root = cJSON_CreateObject();
	array = cJSON_AddArrayToObject(root, "files");

	for (i = 0; i < count; i++) {
		item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", metas[i].name);
		cJSON_AddStringToObject(item, "original", metas[i].original);
		cJSON_AddStringToObject(item, "mime", metas[i].mime);
		cJSON_AddStringToObject(item, "owner", metas[i].owner);
		cJSON_AddNumberToObject(item, "size", (double)metas[i].size);
		cJSON_AddNumberToObject(item, "modified",
		    (double)metas[i].mtime);
		cJSON_AddItemToArray(array, item);
	}

	free(metas);
	api_send(res, ECEWO_OK, root);
}

/*
 * Shared by POST /files (create, name derived from the client filename) and
 * PUT /files/:name (replace, name fixed). As in the HTML controller,
 * ecewo-multipart has already staged the bytes on disk and we own that file,
 * so every rejection path unlinks it.
 */
static void
api_file_store(ecewo_request_t *req, ecewo_response_t *res, const char *name,
    int replacing)
{
	const ecewo_multipart_file_t	*file;
	const char			*staged, *client_name;
	cJSON				*root;
	char				 stored[NAME_MAX_LEN];
	off_t				 written;
	int				 ret;

	if ((file = ecewo_multipart_get_file(req, "file")) == NULL) {
		api_error(res, ECEWO_BAD_REQUEST,
		    "multipart field 'file' required");
		return;
	}

	if ((staged = ecewo_multipart_file_path(file)) == NULL) {
		api_error(res, ECEWO_INTERNAL_SERVER_ERROR, "cannot store file");
		return;
	}

	client_name = ecewo_multipart_file_filename(file);

	if (replacing) {
		if (!upload_exists(name)) {
			(void)unlink(staged);
			api_error(res, ECEWO_NOT_FOUND, "no such file");
			return;
		}

		if (str_lcpy(stored, name, sizeof(stored)) >= sizeof(stored)) {
			(void)unlink(staged);
			api_error(res, ECEWO_BAD_REQUEST, "invalid file name");
			return;
		}
	} else {
		if (client_name == NULL ||
		    store_slugify(client_name, stored, sizeof(stored)) == -1) {
			(void)unlink(staged);
			api_error(res, ECEWO_BAD_REQUEST, "unusable filename");
			return;
		}
	}

	if (!upload_mime_allowed(stored)) {
		(void)unlink(staged);
		api_error(res, ECEWO_FORBIDDEN, "file type not accepted");
		return;
	}

	if ((ret = upload_adopt(staged, stored, &written)) != 0) {
		(void)unlink(staged);
		if (ret == -4)
			api_error(res, ECEWO_PAYLOAD_TOO_LARGE, "file too large");
		else
			api_error(res, ECEWO_INTERNAL_SERVER_ERROR,
			    "cannot store file");
		return;
	}

	(void)upload_finish(stored,
	    (client_name != NULL) ? client_name : stored, ctx_user(req),
	    written);

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", stored);
	cJSON_AddNumberToObject(root, "size", (double)written);

	api_send(res, replacing ? ECEWO_OK : ECEWO_CREATED, root);
}

void
api_files_create(ecewo_request_t *req, ecewo_response_t *res)
{
	if (!api_require_auth(req, res))
		return;

	api_file_store(req, res, NULL, 0);
}

void
api_file_get(ecewo_request_t *req, ecewo_response_t *res)
{
	struct wbuf		 data;
	struct upload_meta	 meta;
	const char		*name;
	char			 path[PATH_MAX_LEN];

	if ((name = api_name(req, res, "invalid file name")) == NULL)
		return;

	if (upload_meta_read(name, &meta) == -1 ||
	    upload_path(path, sizeof(path), name) == -1) {
		api_error(res, ECEWO_NOT_FOUND, "no such file");
		return;
	}

	wbuf_init(&data, 8192);

	if (store_read(path, &data) == -1) {
		wbuf_cleanup(&data);
		api_error(res, ECEWO_NOT_FOUND, "no such file");
		return;
	}

	ecewo_header_set(res, "content-type", meta.mime);
	ecewo_header_set(res, "x-content-type-options", "nosniff");
	ecewo_send(res, ECEWO_OK, data.data, data.offset);

	wbuf_cleanup(&data);
}

void
api_file_put(ecewo_request_t *req, ecewo_response_t *res)
{
	const char	*name;

	if ((name = api_name(req, res, "invalid file name")) == NULL)
		return;

	if (!api_require_auth(req, res))
		return;

	api_file_store(req, res, name, 1);
}

void
api_file_patch(ecewo_request_t *req, ecewo_response_t *res)
{
	cJSON		*json, *root;
	const char	*name, *newname;
	int		 ret;

	if ((name = api_name(req, res, "invalid file name")) == NULL)
		return;

	if (!api_require_auth(req, res))
		return;

	if ((json = api_body_json(req)) == NULL) {
		api_error(res, ECEWO_BAD_REQUEST, "invalid json body");
		return;
	}

	newname = api_json_string(json, "name");

	if (newname == NULL || !store_valid_name(newname)) {
		cJSON_Delete(json);
		api_error(res, ECEWO_BAD_REQUEST, "invalid new name");
		return;
	}

	ret = upload_rename(name, newname);

	if (ret != 0) {
		cJSON_Delete(json);

		if (ret == -2)
			api_error(res, ECEWO_CONFLICT, "target exists");
		else if (ret == -3)
			api_error(res, ECEWO_FORBIDDEN, "file type not accepted");
		else
			api_error(res, ECEWO_NOT_FOUND, "no such file");

		return;
	}

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "name", newname);
	cJSON_AddBoolToObject(root, "renamed", 1);

	cJSON_Delete(json);
	api_send(res, ECEWO_OK, root);
}

void
api_file_delete(ecewo_request_t *req, ecewo_response_t *res)
{
	const char	*name;

	if ((name = api_name(req, res, "invalid file name")) == NULL)
		return;

	if (!api_require_auth(req, res))
		return;

	if (upload_delete(name) == -1) {
		api_error(res, ECEWO_NOT_FOUND, "no such file");
		return;
	}

	ecewo_send(res, ECEWO_NO_CONTENT, NULL, 0);
}

/* Catch-all, registered after every concrete /api/v1 route. */
void
api_not_found(ecewo_request_t *req, ecewo_response_t *res)
{
	(void)req;

	api_error(res, ECEWO_NOT_FOUND, "unknown endpoint");
}
