/*
 * A hand-written stand-in for store.c, used only by test_page_mocked.
 *
 * Provides the seam page.c needs: every store_* symbol it calls, with call
 * recording and scripted return values.
 *
 * An object file provides all of its symbols or none, so this has to cover
 * the entire store_* surface page.c can reach, not just the interesting few.
 */

#include <string.h>

#include "app.h"
#include "models.h"
#include "store_stub.h"

struct store_stub	stub;

void
store_stub_reset(void)
{
	memset(&stub, 0, sizeof(stub));
}

/* ------------------------------------------------------- scripted calls */

int
store_write(const char *path, const void *data, size_t len)
{
	(void)path;
	(void)data;
	(void)len;

	stub.write_calls++;

	return (stub.write_result);
}

int
store_exists(const char *path)
{
	(void)path;

	stub.exists_calls++;

	return (stub.exists_result);
}

int
store_path(char *out, size_t len, const char *dir, const char *name,
    const char *ext)
{
	stub.path_calls++;

	if (stub.path_result != 0)
		return (stub.path_result);

	if (ext == NULL)
		ext = "";

	if ((size_t)snprintf(out, len, "%s/%s%s", dir, name, ext) >= len)
		return (-1);

	return (0);
}

int
store_unlink(const char *path)
{
	(void)path;

	stub.unlink_calls++;

	return (0);
}

/* ----------------------------------------------------- unreached by page.c
 *
 * Present so the link succeeds; a test that reaches one of these has gone
 * somewhere it was not meant to, so they return failure rather than
 * pretending to work.
 */

int
store_valid_name(const char *name)
{
	return (name != NULL && name[0] != '\0');
}

int
store_read(const char *path, struct wbuf *out)
{
	(void)path;
	(void)out;

	return (-1);
}

int
store_copy(const char *src, const char *dst)
{
	(void)src;
	(void)dst;

	return (-1);
}

int
store_rename(const char *from, const char *to)
{
	(void)from;
	(void)to;

	stub.rename_calls++;

	return (stub.rename_result);
}

int
store_stat(const char *path, off_t *size, time_t *mtime)
{
	(void)path;
	(void)size;
	(void)mtime;

	return (-1);
}

int
store_list(const char *dir, const char *suffix, int sort,
    struct store_list *out)
{
	(void)dir;
	(void)suffix;
	(void)sort;

	out->ents = NULL;
	out->count = 0;

	return (0);
}

void
store_list_free(struct store_list *list)
{
	list->ents = NULL;
	list->count = 0;
}

int
store_slugify(const char *in, char *out, size_t len)
{
	(void)in;
	(void)out;
	(void)len;

	return (-1);
}

int
store_verify_dirs(void)
{
	return (0);
}
