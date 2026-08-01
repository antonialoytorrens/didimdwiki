/*
 * Unity - the file store.
 */

#include <sys/types.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"


#include "app.h"
#include "models.h"
#include "test_support.h"

static char	scratch[256];

void
setUp(void)
{
	struct upload_meta	*metas;
	size_t			 i, n;

	if (upload_list(STORE_SORT_NAME, &metas, &n) == 0) {
		for (i = 0; i < n; i++)
			(void)upload_delete(metas[i].name);
		if (metas != NULL)
			free(metas);
	}
}

void
tearDown(void)
{
}

/*
 * Mimics what file_receive() does, minus the multipart plumbing: the bytes
 * arrive as a staging file that upload_adopt() moves into place.
 */
static int
put_file(const char *name, const char *body, const char *owner)
{
	char	staged[PATH_MAX_LEN];
	FILE	*fp;
	off_t	written;
	size_t	len;

	len = strlen(body);

	(void)snprintf(staged, sizeof(staged), "%s/put_file.tmp", BODY_TMP_DIR);

	if ((fp = fopen(staged, "w")) == NULL)
		return (-1);

	if (fwrite(body, 1, len, fp) != len) {
		fclose(fp);
		(void)unlink(staged);
		return (-1);
	}

	if (fclose(fp) != 0) {
		(void)unlink(staged);
		return (-1);
	}

	if (upload_adopt(staged, name, &written) != 0) {
		(void)unlink(staged);
		return (-1);
	}

	return (upload_finish(name, name, owner, written));
}

static void
test_store_and_read_metadata(void)
{
	struct upload_meta	meta;

	TEST_ASSERT_FALSE(upload_exists("notes.txt"));
	TEST_ASSERT_EQUAL_INT(0, put_file("notes.txt", "hello there", "alice"));
	TEST_ASSERT_TRUE(upload_exists("notes.txt"));

	TEST_ASSERT_EQUAL_INT(0, upload_meta_read("notes.txt", &meta));
	TEST_ASSERT_EQUAL_STRING("notes.txt", meta.name);
	TEST_ASSERT_EQUAL_STRING("alice", meta.owner);
	TEST_ASSERT_EQUAL_INT(11, (int)meta.size);
	TEST_ASSERT_TRUE(meta.mtime > 0);
}

/* The type comes from the extension, never from a client-supplied header. */
static void
test_mime_is_derived_server_side(void)
{
	TEST_ASSERT_EQUAL_STRING("image/png", upload_mime_for("a.png"));
	TEST_ASSERT_EQUAL_STRING("image/jpeg", upload_mime_for("a.JPEG"));
	TEST_ASSERT_EQUAL_STRING("application/pdf", upload_mime_for("doc.pdf"));
	TEST_ASSERT_EQUAL_STRING("application/zip", upload_mime_for("bundle.zip"));
	TEST_ASSERT_EQUAL_STRING("application/octet-stream",
	    upload_mime_for("mystery.qqq"));
	TEST_ASSERT_EQUAL_STRING("application/octet-stream",
	    upload_mime_for("noextension"));
}

/* Anything the browser would execute same-origin is refused. */
static void
test_dangerous_types_refused(void)
{
	TEST_ASSERT_FALSE(upload_mime_allowed("page.html"));
	TEST_ASSERT_FALSE(upload_mime_allowed("page.HTM"));
	TEST_ASSERT_FALSE(upload_mime_allowed("script.js"));
	TEST_ASSERT_FALSE(upload_mime_allowed("mod.mjs"));
	TEST_ASSERT_FALSE(upload_mime_allowed("vector.svg"));
	TEST_ASSERT_FALSE(upload_mime_allowed("shell.php"));
	TEST_ASSERT_FALSE(upload_mime_allowed("lib.so"));

	TEST_ASSERT_TRUE(upload_mime_allowed("photo.png"));
	TEST_ASSERT_TRUE(upload_mime_allowed("notes.txt"));
	TEST_ASSERT_TRUE(upload_mime_allowed("archive.tar.gz"));
}

static void
test_metadata_survives_without_sidecar(void)
{
	struct upload_meta	meta;
	char			path[PATH_MAX_LEN];

	TEST_ASSERT_EQUAL_INT(0, put_file("orphan.txt", "data", "alice"));

	/* Drop the sidecar; the file itself must still describe itself. */
	TEST_ASSERT_EQUAL_INT(0,
	    store_path(path, sizeof(path), UPLOADS_META_DIR, "orphan.txt",
	    ".tsv"));
	TEST_ASSERT_EQUAL_INT(0, store_unlink(path));

	TEST_ASSERT_EQUAL_INT(0, upload_meta_read("orphan.txt", &meta));
	TEST_ASSERT_EQUAL_STRING("orphan.txt", meta.name);
	TEST_ASSERT_EQUAL_STRING("-", meta.owner);
	TEST_ASSERT_EQUAL_INT(4, (int)meta.size);
}

static void
test_replace_updates_size(void)
{
	struct upload_meta	meta;

	TEST_ASSERT_EQUAL_INT(0, put_file("doc.txt", "short", "alice"));
	TEST_ASSERT_EQUAL_INT(0, put_file("doc.txt", "a much longer body", "bob"));

	TEST_ASSERT_EQUAL_INT(0, upload_meta_read("doc.txt", &meta));
	TEST_ASSERT_EQUAL_INT(18, (int)meta.size);
	TEST_ASSERT_EQUAL_STRING("bob", meta.owner);
}

static void
test_rename(void)
{
	struct upload_meta	meta;

	TEST_ASSERT_EQUAL_INT(0, put_file("before.txt", "content", "alice"));
	TEST_ASSERT_EQUAL_INT(0, upload_rename("before.txt", "after.txt"));

	TEST_ASSERT_FALSE(upload_exists("before.txt"));
	TEST_ASSERT_TRUE(upload_exists("after.txt"));

	TEST_ASSERT_EQUAL_INT(0, upload_meta_read("after.txt", &meta));
	TEST_ASSERT_EQUAL_STRING("after.txt", meta.name);
	TEST_ASSERT_EQUAL_STRING("alice", meta.owner);
}

/* Renaming must not become a way to smuggle in an executable extension. */
static void
test_rename_cannot_smuggle_html(void)
{
	TEST_ASSERT_EQUAL_INT(0, put_file("safe.txt", "x", "alice"));

	TEST_ASSERT_EQUAL_INT(-3, upload_rename("safe.txt", "evil.html"));
	TEST_ASSERT_EQUAL_INT(-3, upload_rename("safe.txt", "evil.js"));

	TEST_ASSERT_TRUE(upload_exists("safe.txt"));
	TEST_ASSERT_FALSE(upload_exists("evil.html"));
}

static void
test_rename_conflicts_and_bad_names(void)
{
	TEST_ASSERT_EQUAL_INT(0, put_file("a.txt", "a", "alice"));
	TEST_ASSERT_EQUAL_INT(0, put_file("b.txt", "b", "alice"));

	TEST_ASSERT_EQUAL_INT(-2, upload_rename("a.txt", "b.txt"));
	TEST_ASSERT_TRUE(upload_exists("a.txt"));

	TEST_ASSERT_EQUAL_INT(-1, upload_rename("ghost.txt", "c.txt"));
	TEST_ASSERT_EQUAL_INT(-1, upload_rename("a.txt", "../escape.txt"));
	TEST_ASSERT_EQUAL_INT(-1, upload_rename("a.txt", ".hidden"));

	TEST_ASSERT_EQUAL_INT(0, upload_rename("a.txt", "a.txt"));
	TEST_ASSERT_TRUE(upload_exists("a.txt"));
}

static void
test_delete_removes_file_and_sidecar(void)
{
	char	path[PATH_MAX_LEN];

	TEST_ASSERT_EQUAL_INT(0, put_file("gone.txt", "x", "alice"));

	TEST_ASSERT_EQUAL_INT(0,
	    store_path(path, sizeof(path), UPLOADS_META_DIR, "gone.txt",
	    ".tsv"));
	TEST_ASSERT_TRUE(store_exists(path));

	TEST_ASSERT_EQUAL_INT(0, upload_delete("gone.txt"));

	TEST_ASSERT_FALSE(upload_exists("gone.txt"));
	TEST_ASSERT_FALSE(store_exists(path));

	TEST_ASSERT_EQUAL_INT(-1, upload_delete("gone.txt"));
}

static void
test_list_excludes_metadata_dir(void)
{
	struct upload_meta	*metas;
	size_t			 i, n;

	TEST_ASSERT_EQUAL_INT(0, put_file("one.txt", "1", "alice"));
	TEST_ASSERT_EQUAL_INT(0, put_file("two.txt", "2", "alice"));

	TEST_ASSERT_EQUAL_INT(0, upload_list(STORE_SORT_NAME, &metas, &n));
	TEST_ASSERT_EQUAL_INT(2, n);

	for (i = 0; i < n; i++)
		TEST_ASSERT_TRUE(metas[i].name[0] != '.');

	TEST_ASSERT_EQUAL_STRING("one.txt", metas[0].name);
	TEST_ASSERT_EQUAL_STRING("two.txt", metas[1].name);

	free(metas);
}

/*
 * upload_adopt() must refuse a destination name outright rather than let a
 * traversal through; the staging path it is handed is trusted, the name is
 * not.
 */
static void
test_adopt_rejects_bad_names(void)
{
	char	staged[PATH_MAX_LEN];
	FILE	*fp;
	off_t	written;

	(void)snprintf(staged, sizeof(staged), "%s/adopt.tmp", BODY_TMP_DIR);

	fp = fopen(staged, "w");
	TEST_ASSERT_NOT_NULL(fp);
	fputs("x", fp);
	fclose(fp);

	TEST_ASSERT_EQUAL_INT(-1, upload_adopt(staged, "../escape.txt", &written));
	TEST_ASSERT_EQUAL_INT(-1, upload_adopt(staged, ".hidden", &written));
	TEST_ASSERT_EQUAL_INT(-1, upload_adopt(staged, "dir/file.txt", &written));

	/* Every rejection leaves the staged file where it was. */
	TEST_ASSERT_EQUAL_INT(0, access(staged, F_OK));

	(void)unlink(staged);
}

int
main(void)
{
	int	result;

	TEST_BOOTSTRAP();

	if (test_chdir_scratch(scratch, sizeof(scratch)) == -1)
		return (1);

	if (test_init_data_dirs() == -1 || store_verify_dirs() != 0) {
		test_rm_scratch(scratch);
		return (1);
	}

	UNITY_BEGIN();
	RUN_TEST(test_store_and_read_metadata);
	RUN_TEST(test_mime_is_derived_server_side);
	RUN_TEST(test_dangerous_types_refused);
	RUN_TEST(test_metadata_survives_without_sidecar);
	RUN_TEST(test_replace_updates_size);
	RUN_TEST(test_rename);
	RUN_TEST(test_rename_cannot_smuggle_html);
	RUN_TEST(test_rename_conflicts_and_bad_names);
	RUN_TEST(test_delete_removes_file_and_sidecar);
	RUN_TEST(test_list_excludes_metadata_dir);
	RUN_TEST(test_adopt_rejects_bad_names);
	result = UNITY_END();

	test_rm_scratch(scratch);

	return (result);
}
