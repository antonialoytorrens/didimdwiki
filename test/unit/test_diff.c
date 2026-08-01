/*
 * Unity - the line differ that replaces popen("diff -abB").
 */

#include <string.h>

#include "unity.h"

#include "test_support.h"


#include "app.h"
#include "models.h"
#include "helpers.h"

static struct wbuf	out;

void
setUp(void)
{
	wbuf_init(&out, 4096);
}

void
tearDown(void)
{
	wbuf_cleanup(&out);
}

static const char *
diff(const char *a, const char *b)
{
	diff_render_html(a, strlen(a), b, strlen(b), &out);

	return (wbuf_stringify(&out, NULL));
}

static int
count(const char *hay, const char *needle)
{
	int		n = 0;
	const char	*p = hay;

	while ((p = strstr(p, needle)) != NULL) {
		n++;
		p += strlen(needle);
	}

	return (n);
}

static void
test_identical_is_all_context(void)
{
	const char	*html = diff("one\ntwo\nthree\n", "one\ntwo\nthree\n");

	TEST_ASSERT_EQUAL_INT(3, count(html, "class=\"ctx\""));
	TEST_ASSERT_EQUAL_INT(0, count(html, "class=\"add\""));
	TEST_ASSERT_EQUAL_INT(0, count(html, "class=\"del\""));
}

static void
test_pure_addition(void)
{
	const char	*html = diff("one\ntwo\n", "one\ntwo\nthree\n");

	TEST_ASSERT_EQUAL_INT(2, count(html, "class=\"ctx\""));
	TEST_ASSERT_EQUAL_INT(1, count(html, "class=\"add\""));
	TEST_ASSERT_EQUAL_INT(0, count(html, "class=\"del\""));
}

static void
test_pure_deletion(void)
{
	const char	*html = diff("one\ntwo\nthree\n", "one\nthree\n");

	TEST_ASSERT_EQUAL_INT(2, count(html, "class=\"ctx\""));
	TEST_ASSERT_EQUAL_INT(1, count(html, "class=\"del\""));
	TEST_ASSERT_EQUAL_INT(0, count(html, "class=\"add\""));
}

static void
test_replacement_shows_both_sides(void)
{
	const char	*html = diff("one\nold\nthree\n", "one\nnew\nthree\n");

	TEST_ASSERT_EQUAL_INT(1, count(html, "class=\"del\""));
	TEST_ASSERT_EQUAL_INT(1, count(html, "class=\"add\""));
	TEST_ASSERT_NOT_NULL(strstr(html, ">old<"));
	TEST_ASSERT_NOT_NULL(strstr(html, ">new<"));
}

static void
test_diff_escapes_html(void)
{
	const char	*html = diff("", "<script>alert(1)</script>\n");

	TEST_ASSERT_NULL(strstr(html, "<script>"));
	TEST_ASSERT_NOT_NULL(strstr(html, "&lt;script&gt;"));
}

static void
test_empty_sides(void)
{
	const char	*html;

	html = diff("", "");
	TEST_ASSERT_NOT_NULL(strstr(html, "<pre>"));

	wbuf_reset(&out);
	html = diff("a\nb\n", "");
	TEST_ASSERT_EQUAL_INT(2, count(html, "class=\"del\""));

	wbuf_reset(&out);
	html = diff("", "a\nb\n");
	TEST_ASSERT_EQUAL_INT(2, count(html, "class=\"add\""));
}

static void
test_crlf_is_ignored(void)
{
	const char	*html = diff("one\r\ntwo\r\n", "one\ntwo\n");

	TEST_ASSERT_EQUAL_INT(2, count(html, "class=\"ctx\""));
	TEST_ASSERT_EQUAL_INT(0, count(html, "class=\"add\""));
}

static void
test_no_trailing_newline(void)
{
	const char	*html = diff("one\ntwo", "one\ntwo");

	TEST_ASSERT_EQUAL_INT(2, count(html, "class=\"ctx\""));
}

int
main(void)
{
	TEST_BOOTSTRAP();

	UNITY_BEGIN();

	RUN_TEST(test_identical_is_all_context);
	RUN_TEST(test_pure_addition);
	RUN_TEST(test_pure_deletion);
	RUN_TEST(test_replacement_shows_both_sides);
	RUN_TEST(test_diff_escapes_html);
	RUN_TEST(test_empty_sides);
	RUN_TEST(test_crlf_is_ignored);
	RUN_TEST(test_no_trailing_newline);

	return (UNITY_END());
}
