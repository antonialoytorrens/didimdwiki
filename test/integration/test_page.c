/*
 * Unity - pages and revisions against a scratch data directory.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"


#include "app.h"
#include "models.h"
#include "xmalloc.h"
#include "test_support.h"

static char	scratch[256];

/* Each test starts from an empty page store. */
void
setUp(void)
{
	struct store_list	pages;
	size_t			i;

	if (page_list(STORE_SORT_NAME, &pages) == 0) {
		for (i = 0; i < pages.count; i++) {
			(void)page_delete(pages.ents[i].name, "teardown");
			(void)revision_purge(pages.ents[i].name);
		}
		store_list_free(&pages);
	}
}

void
tearDown(void)
{
}

static void
read_page(const char *name, char *out, size_t len)
{
	struct wbuf	buf;

	wbuf_init(&buf, 1024);
	TEST_ASSERT_EQUAL_INT(0, page_read(name, &buf));
	wbuf_stringify(&buf, NULL);
	TEST_ASSERT_TRUE(strlen((char *)buf.data) < len);
	memcpy(out, buf.data, strlen((char *)buf.data) + 1);
	wbuf_cleanup(&buf);
}

static void
test_create_and_read(void)
{
	char	body[256];

	TEST_ASSERT_FALSE(page_exists("Sample"));
	TEST_ASSERT_EQUAL_INT(0, page_write("Sample", "alice", "# Hello", 7));
	TEST_ASSERT_TRUE(page_exists("Sample"));

	read_page("Sample", body, sizeof(body));
	TEST_ASSERT_EQUAL_STRING("# Hello", body);
}

static void
test_write_rejects_bad_names(void)
{
	TEST_ASSERT_EQUAL_INT(-1, page_write("../escape", "alice", "x", 1));
	TEST_ASSERT_EQUAL_INT(-1, page_write(".hidden", "alice", "x", 1));
	TEST_ASSERT_EQUAL_INT(-1, page_write("a/b", "alice", "x", 1));
	TEST_ASSERT_EQUAL_INT(-1, page_write("", "alice", "x", 1));
}

static void
test_write_rejects_oversized(void)
{
	char	*big;

	big = xcalloc(1, MAX_PAGE_BYTES + 16);
	memset(big, 'a', MAX_PAGE_BYTES + 15);

	TEST_ASSERT_EQUAL_INT(-1,
	    page_write("Huge", "alice", big, MAX_PAGE_BYTES + 15));
	TEST_ASSERT_FALSE(page_exists("Huge"));

	free(big);
}

/* The point of store_write(): a reader never sees a partial page. */
static void
test_overwrite_is_complete(void)
{
	char	body[256];

	TEST_ASSERT_EQUAL_INT(0,
	    page_write("Sample", "alice", "aaaaaaaaaaaaaaaaaaaa", 20));
	TEST_ASSERT_EQUAL_INT(0, page_write("Sample", "alice", "bb", 2));

	read_page("Sample", body, sizeof(body));
	TEST_ASSERT_EQUAL_STRING("bb", body);
}

static void
test_edit_creates_revision(void)
{
	struct revision_list	revs;

	TEST_ASSERT_EQUAL_INT(0, page_write("Doc", "alice", "v1", 2));

	/* The first write has nothing to snapshot. */
	TEST_ASSERT_EQUAL_INT(0, revision_list("Doc", &revs));
	TEST_ASSERT_EQUAL_INT(0, revs.count);
	revision_list_free(&revs);

	TEST_ASSERT_EQUAL_INT(0, page_write("Doc", "bob", "v2", 2));

	TEST_ASSERT_EQUAL_INT(0, revision_list("Doc", &revs));
	TEST_ASSERT_EQUAL_INT(1, revs.count);
	TEST_ASSERT_EQUAL_STRING("bob", revs.revs[0].author);
	revision_list_free(&revs);
}

/* Two saves in the same second must not collapse into one revision. */
static void
test_rapid_edits_keep_every_revision(void)
{
	struct revision_list	revs;
	int			i;

	TEST_ASSERT_EQUAL_INT(0, page_write("Fast", "alice", "v0", 2));

	for (i = 1; i <= 5; i++)
		TEST_ASSERT_EQUAL_INT(0, page_write("Fast", "alice", "vN", 2));

	TEST_ASSERT_EQUAL_INT(0, revision_list("Fast", &revs));
	TEST_ASSERT_EQUAL_INT(5, revs.count);
	revision_list_free(&revs);
}

static void
test_revision_content_is_the_previous_text(void)
{
	struct revision_list	revs;
	struct wbuf		buf;

	TEST_ASSERT_EQUAL_INT(0, page_write("Doc", "alice", "first", 5));
	TEST_ASSERT_EQUAL_INT(0, page_write("Doc", "alice", "second", 6));

	TEST_ASSERT_EQUAL_INT(0, revision_list("Doc", &revs));
	TEST_ASSERT_EQUAL_INT(1, revs.count);

	wbuf_init(&buf, 64);
	TEST_ASSERT_EQUAL_INT(0, revision_read("Doc", revs.revs[0].stamp, &buf));
	wbuf_stringify(&buf, NULL);
	TEST_ASSERT_EQUAL_STRING("first", (char *)buf.data);

	wbuf_cleanup(&buf);
	revision_list_free(&revs);
}

static void
test_revisions_are_capped(void)
{
	struct revision_list	revs;
	int			i;

	TEST_ASSERT_EQUAL_INT(0, page_write("Busy", "alice", "v", 1));

	for (i = 0; i < REVISIONS_KEPT + 10; i++)
		TEST_ASSERT_EQUAL_INT(0, page_write("Busy", "alice", "v", 1));

	TEST_ASSERT_EQUAL_INT(0, revision_list("Busy", &revs));
	TEST_ASSERT_EQUAL_INT(REVISIONS_KEPT, revs.count);
	revision_list_free(&revs);
}

static void
test_rename(void)
{
	char	body[256];

	TEST_ASSERT_EQUAL_INT(0, page_write("Old", "alice", "content", 7));
	TEST_ASSERT_EQUAL_INT(0, page_rename("Old", "New", "alice"));

	TEST_ASSERT_FALSE(page_exists("Old"));
	TEST_ASSERT_TRUE(page_exists("New"));

	read_page("New", body, sizeof(body));
	TEST_ASSERT_EQUAL_STRING("content", body);
}

static void
test_rename_moves_history(void)
{
	struct revision_list	revs;

	TEST_ASSERT_EQUAL_INT(0, page_write("Old", "alice", "v1", 2));
	TEST_ASSERT_EQUAL_INT(0, page_write("Old", "alice", "v2", 2));
	TEST_ASSERT_EQUAL_INT(0, page_rename("Old", "New", "alice"));

	TEST_ASSERT_EQUAL_INT(0, revision_list("Old", &revs));
	TEST_ASSERT_EQUAL_INT(0, revs.count);
	revision_list_free(&revs);

	/* Two edits plus the snapshot taken by the rename itself. */
	TEST_ASSERT_EQUAL_INT(0, revision_list("New", &revs));
	TEST_ASSERT_EQUAL_INT(2, revs.count);
	revision_list_free(&revs);
}

static void
test_rename_conflict_and_missing(void)
{
	TEST_ASSERT_EQUAL_INT(0, page_write("A", "alice", "a", 1));
	TEST_ASSERT_EQUAL_INT(0, page_write("B", "alice", "b", 1));

	TEST_ASSERT_EQUAL_INT(-2, page_rename("A", "B", "alice"));
	TEST_ASSERT_TRUE(page_exists("A"));

	TEST_ASSERT_EQUAL_INT(-1, page_rename("Ghost", "C", "alice"));
	TEST_ASSERT_EQUAL_INT(-1, page_rename("A", "../escape", "alice"));

	/* Renaming to itself is a no-op, not an error. */
	TEST_ASSERT_EQUAL_INT(0, page_rename("A", "A", "alice"));
	TEST_ASSERT_TRUE(page_exists("A"));
}

static void
test_rename_creates_redirect_marker(void)
{
	char	dst[NAME_MAX_LEN];

	TEST_ASSERT_EQUAL_INT(0, page_write("Old", "alice", "content", 7));
	TEST_ASSERT_EQUAL_INT(0, page_rename("Old", "New", "alice"));

	/* The old name must redirect to the new one. */
	TEST_ASSERT_EQUAL_INT(0, page_redirect_lookup("Old", dst, sizeof(dst)));
	TEST_ASSERT_EQUAL_STRING("New", dst);

	/* A live page has no redirect. */
	TEST_ASSERT_EQUAL_INT(-1, page_redirect_lookup("New", dst, sizeof(dst)));
}

static void
test_redirect_lookup_on_missing_page(void)
{
	char	dst[NAME_MAX_LEN];

	/* A page that never existed has no redirect. */
	TEST_ASSERT_EQUAL_INT(-1, page_redirect_lookup("Ghost", dst, sizeof(dst)));
}

static void
test_redirect_chain(void)
{
	char	dst[NAME_MAX_LEN];

	/* A → B → C: the lookup should resolve all the way to C. */
	TEST_ASSERT_EQUAL_INT(0, page_write("A", "alice", "start", 5));
	TEST_ASSERT_EQUAL_INT(0, page_rename("A", "B", "alice"));
	TEST_ASSERT_EQUAL_INT(0, page_rename("B", "C", "alice"));

	TEST_ASSERT_EQUAL_INT(0, page_redirect_lookup("A", dst, sizeof(dst)));
	TEST_ASSERT_EQUAL_STRING("C", dst);

	/* B also redirects to C directly. */
	TEST_ASSERT_EQUAL_INT(0, page_redirect_lookup("B", dst, sizeof(dst)));
	TEST_ASSERT_EQUAL_STRING("C", dst);
}

static void
test_redirect_chain_limit(void)
{
	char	dst[NAME_MAX_LEN];

	/* A → B → C → D exceeds the hop limit of REDIR_MAX_HOPS=3. */
	TEST_ASSERT_EQUAL_INT(0, page_write("A", "alice", "v", 1));
	TEST_ASSERT_EQUAL_INT(0, page_rename("A", "B", "alice"));
	TEST_ASSERT_EQUAL_INT(0, page_rename("B", "C", "alice"));
	TEST_ASSERT_EQUAL_INT(0, page_rename("C", "D", "alice"));

	TEST_ASSERT_EQUAL_INT(-1, page_redirect_lookup("A", dst, sizeof(dst)));
}

static void
test_rename_to_itself_no_redirect(void)
{
	char	dst[NAME_MAX_LEN];

	TEST_ASSERT_EQUAL_INT(0, page_write("Same", "alice", "body", 4));
	TEST_ASSERT_EQUAL_INT(0, page_rename("Same", "Same", "alice"));

	/* Rename to self is a no-op; no .redir file should be created. */
	TEST_ASSERT_EQUAL_INT(-1, page_redirect_lookup("Same", dst, sizeof(dst)));
	TEST_ASSERT_TRUE(page_exists("Same"));
}

static void
test_recreate_old_name_clears_redirect(void)
{
	char	 dst[NAME_MAX_LEN];
	char	 body[256];

	/* Rename Old → New; a redirect marker is created for Old. */
	TEST_ASSERT_EQUAL_INT(0, page_write("Old", "alice", "original", 8));
	TEST_ASSERT_EQUAL_INT(0, page_rename("Old", "New", "alice"));
	TEST_ASSERT_EQUAL_INT(0, page_redirect_lookup("Old", dst, sizeof(dst)));
	TEST_ASSERT_EQUAL_STRING("New", dst);

	/* Re-create a page at the old name. */
	TEST_ASSERT_EQUAL_INT(0, page_write("Old", "alice", "reborn", 6));

	/* The redirect marker must be gone: lookup returns -1. */
	TEST_ASSERT_EQUAL_INT(-1, page_redirect_lookup("Old", dst, sizeof(dst)));

	/* The new Old page serves its own content. */
	read_page("Old", body, sizeof(body));
	TEST_ASSERT_EQUAL_STRING("reborn", body);

	/* The New page is unaffected. */
	read_page("New", body, sizeof(body));
	TEST_ASSERT_EQUAL_STRING("original", body);
}

static void
test_delete_keeps_history(void)
{
	struct revision_list	revs;

	TEST_ASSERT_EQUAL_INT(0, page_write("Doomed", "alice", "text", 4));
	TEST_ASSERT_EQUAL_INT(0, page_delete("Doomed", "alice"));

	TEST_ASSERT_FALSE(page_exists("Doomed"));

	/* Deleting snapshots first, so the text is still recoverable. */
	TEST_ASSERT_EQUAL_INT(0, revision_list("Doomed", &revs));
	TEST_ASSERT_EQUAL_INT(1, revs.count);
	revision_list_free(&revs);

	TEST_ASSERT_EQUAL_INT(-1, page_delete("Doomed", "alice"));
}

static void
test_list_and_sort(void)
{
	struct store_list	pages;

	TEST_ASSERT_EQUAL_INT(0, page_write("Charlie", "alice", "c", 1));
	TEST_ASSERT_EQUAL_INT(0, page_write("alpha", "alice", "a", 1));
	TEST_ASSERT_EQUAL_INT(0, page_write("Bravo", "alice", "b", 1));

	TEST_ASSERT_EQUAL_INT(0, page_list(STORE_SORT_NAME, &pages));
	TEST_ASSERT_EQUAL_INT(3, pages.count);

	/* Case-insensitive, so "alpha" sorts before "Bravo". */
	TEST_ASSERT_EQUAL_STRING("alpha", pages.ents[0].name);
	TEST_ASSERT_EQUAL_STRING("Bravo", pages.ents[1].name);
	TEST_ASSERT_EQUAL_STRING("Charlie", pages.ents[2].name);

	store_list_free(&pages);
}

/* Revisions live in a dot-directory and must never appear as pages. */
static void
test_list_hides_internals(void)
{
	struct store_list	pages;
	size_t			i;

	TEST_ASSERT_EQUAL_INT(0, page_write("Doc", "alice", "v1", 2));
	TEST_ASSERT_EQUAL_INT(0, page_write("Doc", "alice", "v2", 2));

	TEST_ASSERT_EQUAL_INT(0, page_list(STORE_SORT_NAME, &pages));

	for (i = 0; i < pages.count; i++) {
		TEST_ASSERT_TRUE(strcmp(".rev", pages.ents[i].name) != 0);
		TEST_ASSERT_TRUE(pages.ents[i].name[0] != '.');
	}

	TEST_ASSERT_EQUAL_INT(1, pages.count);
	store_list_free(&pages);
}

static void
test_read_missing_page(void)
{
	struct wbuf	buf;

	wbuf_init(&buf, 64);
	TEST_ASSERT_EQUAL_INT(-1, page_read("Ghost", &buf));
	wbuf_cleanup(&buf);

	TEST_ASSERT_FALSE(page_exists("Ghost"));
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
	RUN_TEST(test_create_and_read);
	RUN_TEST(test_write_rejects_bad_names);
	RUN_TEST(test_write_rejects_oversized);
	RUN_TEST(test_overwrite_is_complete);
	RUN_TEST(test_edit_creates_revision);
	RUN_TEST(test_rapid_edits_keep_every_revision);
	RUN_TEST(test_revision_content_is_the_previous_text);
	RUN_TEST(test_revisions_are_capped);
	RUN_TEST(test_rename);
	RUN_TEST(test_rename_moves_history);
	RUN_TEST(test_rename_conflict_and_missing);
	RUN_TEST(test_rename_creates_redirect_marker);
	RUN_TEST(test_redirect_lookup_on_missing_page);
	RUN_TEST(test_redirect_chain);
	RUN_TEST(test_redirect_chain_limit);
	RUN_TEST(test_rename_to_itself_no_redirect);
	RUN_TEST(test_recreate_old_name_clears_redirect);
	RUN_TEST(test_delete_keeps_history);
	RUN_TEST(test_list_and_sort);
	RUN_TEST(test_list_hides_internals);
	RUN_TEST(test_read_missing_page);
	result = UNITY_END();

	test_rm_scratch(scratch);

	return (result);
}
