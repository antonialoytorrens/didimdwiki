/*
 * Unity - name validation and slugification.
 * These are the guards standing between a URL and the filesystem, so the
 * traversal cases matter more than the happy path.
 */

#include <string.h>

#include "unity.h"

#include "test_support.h"

#include "app.h"
#include "models.h"

void setUp(void) {}
void tearDown(void) {}

static void
test_accepts_ordinary_names(void)
{
	TEST_ASSERT_TRUE(store_valid_name("WikiHome"));
	TEST_ASSERT_TRUE(store_valid_name("My Page"));
	TEST_ASSERT_TRUE(store_valid_name("notes-2026"));
	TEST_ASSERT_TRUE(store_valid_name("report_v2.txt"));
	TEST_ASSERT_TRUE(store_valid_name("a(1)+b"));
	TEST_ASSERT_TRUE(store_valid_name("Ünicöde"));
	TEST_ASSERT_TRUE(store_valid_name("a"));
}

static void
test_rejects_traversal(void)
{
	TEST_ASSERT_FALSE(store_valid_name(".."));
	TEST_ASSERT_FALSE(store_valid_name("."));
	TEST_ASSERT_FALSE(store_valid_name("../etc/passwd"));
	TEST_ASSERT_FALSE(store_valid_name("..%2fetc"));
	TEST_ASSERT_FALSE(store_valid_name("a/../b"));
	TEST_ASSERT_FALSE(store_valid_name("a..b"));
	TEST_ASSERT_FALSE(store_valid_name("/etc/passwd"));
	TEST_ASSERT_FALSE(store_valid_name("dir/file"));
	TEST_ASSERT_FALSE(store_valid_name("dir\\file"));
}

static void
test_rejects_hidden_and_control(void)
{
	/* Revisions and upload metadata live in dot-directories. */
	TEST_ASSERT_FALSE(store_valid_name(".rev"));
	TEST_ASSERT_FALSE(store_valid_name(".meta"));
	TEST_ASSERT_FALSE(store_valid_name(".favicon.ico"));
	TEST_ASSERT_FALSE(store_valid_name("bad\nname"));
	TEST_ASSERT_FALSE(store_valid_name("bad\tname"));
	TEST_ASSERT_FALSE(store_valid_name("nul\x01here"));
	TEST_ASSERT_FALSE(store_valid_name(""));
	TEST_ASSERT_FALSE(store_valid_name(NULL));
}

static void
test_rejects_awkward_edges(void)
{
	TEST_ASSERT_FALSE(store_valid_name("trailing "));
	TEST_ASSERT_FALSE(store_valid_name("trailing."));
	TEST_ASSERT_FALSE(store_valid_name("-leading-dash"));
	TEST_ASSERT_FALSE(store_valid_name("colon:name"));
}

static void
test_rejects_overlong(void)
{
	char	buf[NAME_MAX_LEN + 16];

	memset(buf, 'a', sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	TEST_ASSERT_FALSE(store_valid_name(buf));

	/* NAME_MAX_LEN chars will not fit alongside the NUL. */
	buf[NAME_MAX_LEN] = '\0';
	TEST_ASSERT_FALSE(store_valid_name(buf));

	buf[NAME_MAX_LEN - 1] = '\0';
	TEST_ASSERT_TRUE(store_valid_name(buf));
}

static void
test_store_path_builds_and_guards(void)
{
	char	path[PATH_MAX_LEN];

	TEST_ASSERT_EQUAL_INT(0,
	    store_path(path, sizeof(path), "data/pages", "Home", ".md"));
	TEST_ASSERT_EQUAL_STRING("data/pages/Home.md", path);

	TEST_ASSERT_EQUAL_INT(0,
	    store_path(path, sizeof(path), "data/uploads", "a.png", NULL));
	TEST_ASSERT_EQUAL_STRING("data/uploads/a.png", path);

	/* An invalid name must never reach the filesystem. */
	TEST_ASSERT_EQUAL_INT(-1,
	    store_path(path, sizeof(path), "data/pages", "../escape", ".md"));

	/* Truncation is an error, not a silently shortened path. */
	TEST_ASSERT_EQUAL_INT(-1,
	    store_path(path, 8, "data/pages", "LongEnoughName", ".md"));
}

static void
test_rejects_html_injection(void)
{
	TEST_ASSERT_FALSE(store_valid_name("<script>alert(1)</script>"));
	TEST_ASSERT_FALSE(store_valid_name("<img src=x onerror=alert(1)>"));
	TEST_ASSERT_FALSE(store_valid_name("foo\">bar"));
	/* Single quote — not in the allowed character set. */
	TEST_ASSERT_FALSE(store_valid_name("foo'bar"));
	/* '&' is not allowed, so HTML entities cannot sneak through. */
	TEST_ASSERT_FALSE(store_valid_name("foo&amp;bar"));
	TEST_ASSERT_FALSE(store_valid_name("foo&lt;bar"));
}

static void
test_rejects_header_injection(void)
{
	/* Bare CR/LF could be used to inject HTTP response headers. */
	TEST_ASSERT_FALSE(store_valid_name("foo\r\nX-Evil: yes"));
	TEST_ASSERT_FALSE(store_valid_name("foo\nbar"));
	TEST_ASSERT_FALSE(store_valid_name("foo\rbar"));
}

static void
test_rejects_url_schemes(void)
{
	/* ':' is in the rejected set, so no URL scheme can be encoded. */
	TEST_ASSERT_FALSE(store_valid_name("javascript:alert(1)"));
	TEST_ASSERT_FALSE(store_valid_name("data:text/html,<h1>x</h1>"));
	TEST_ASSERT_FALSE(store_valid_name("http://evil.com"));
}

static void
test_slugify(void)
{
	char	out[NAME_MAX_LEN];

	TEST_ASSERT_EQUAL_INT(0, store_slugify("photo.png", out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("photo.png", out);

	/* A client-supplied path is reduced to its last component. */
	TEST_ASSERT_EQUAL_INT(0,
	    store_slugify("../../etc/passwd", out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("passwd", out);

	TEST_ASSERT_EQUAL_INT(0,
	    store_slugify("C:\\Users\\me\\My Report.PDF", out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("My-Report.PDF", out);

	/* Leading dots are dropped so an upload can never become a dotfile. */
	TEST_ASSERT_EQUAL_INT(0, store_slugify(".bashrc", out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("bashrc", out);

	TEST_ASSERT_EQUAL_INT(0,
	    store_slugify("weird  name!!.txt", out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("weird-name.txt", out);

	/* Nothing usable left still yields a valid name. */
	TEST_ASSERT_EQUAL_INT(0, store_slugify("///", out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("file", out);

	/* Whatever comes out must satisfy the validator. */
	TEST_ASSERT_TRUE(store_valid_name(out));
}

int
main(void)
{
	TEST_BOOTSTRAP();

	UNITY_BEGIN();

	RUN_TEST(test_accepts_ordinary_names);
	RUN_TEST(test_rejects_traversal);
	RUN_TEST(test_rejects_hidden_and_control);
	RUN_TEST(test_rejects_awkward_edges);
	RUN_TEST(test_rejects_overlong);
	RUN_TEST(test_rejects_html_injection);
	RUN_TEST(test_rejects_header_injection);
	RUN_TEST(test_rejects_url_schemes);
	RUN_TEST(test_store_path_builds_and_guards);
	RUN_TEST(test_slugify);

	return (UNITY_END());
}
