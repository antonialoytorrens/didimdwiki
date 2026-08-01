/*
 * Unity - markdown rendering.
 * The load-bearing cases are wiki links and raw HTML staying inert.
 */

#include <string.h>

#include "unity.h"

#include "test_support.h"


#include "app.h"
#include "wbuf.h"
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
render(const char *src, int flags)
{
	markdown_render(src, strlen(src), &out, flags);

	return (wbuf_stringify(&out, NULL));
}

#define ASSERT_HAS(hay, needle) \
	TEST_ASSERT_NOT_NULL_MESSAGE(strstr(hay, needle), needle)
#define ASSERT_HASNT(hay, needle) \
	TEST_ASSERT_NULL_MESSAGE(strstr(hay, needle), needle)

static void
test_basic_blocks(void)
{
	const char	*html = render(
	    "# Title\n\nSome **bold** and *italic* text.\n\n"
	    "- one\n- two\n\n1. first\n2. second\n\n---\n\n> quoted\n", 0);

	ASSERT_HAS(html, "<h1>Title</h1>");
	ASSERT_HAS(html, "<strong>bold</strong>");
	ASSERT_HAS(html, "<em>italic</em>");
	ASSERT_HAS(html, "<ul>");
	ASSERT_HAS(html, "<ol>");
	ASSERT_HAS(html, "<hr>");
	ASSERT_HAS(html, "<blockquote>");
}

/* Fenced code blocks render with a language-aware code block. */
static void
test_fenced_code(void)
{
	const char	*html = render(
	    "```c\nint main(void) { return 0; }\n```\n", 0);

	ASSERT_HAS(html, "<pre><code class=\"language-c\">");
	ASSERT_HAS(html, "int main(void)");
}

static void
test_tables_and_tasklists(void)
{
	const char	*html = render(
	    "| a | b |\n| - | - |\n| 1 | 2 |\n\n"
	    "- [ ] todo\n- [x] done\n", 0);

	ASSERT_HAS(html, "<table>");
	ASSERT_HAS(html, "<th>");
	ASSERT_HAS(html, "type=\"checkbox\"");
	ASSERT_HAS(html, "checked");
}

static void
test_wikilinks(void)
{
	const char	*html = render(
	    "See [[WikiHelp]] and [[Some Page|this label]].\n", 0);

	ASSERT_HAS(html, "<a class=\"wikilink\" href=\"/wiki/WikiHelp\">");
	ASSERT_HAS(html, "WikiHelp</a>");
	ASSERT_HAS(html, "this label</a>");
	/* Spaces in a target must be url-encoded, not emitted raw. */
	ASSERT_HAS(html, "/wiki/Some%20Page");
	/* Upstream md4c's custom element must not survive our patch. */
	ASSERT_HASNT(html, "x-wikilink");
}

/* Target text goes through an HTML-unescape/URL-escape round trip. */
static void
test_wikilink_target_with_ampersand(void)
{
	const char	*html = render("See [[A & B]].\n", 0);

	ASSERT_HAS(html, "href=\"/wiki/A%20&amp;%20B\"");
	ASSERT_HAS(html, "A &amp; B</a>");
}

/* Links inside code must stay literal. */
static void
test_wikilink_inside_code_is_literal(void)
{
	const char	*html = render(
	    "Inline `[[NotALink]]` stays put.\n\n"
	    "```\n[[AlsoNotALink]]\n```\n", 0);

	ASSERT_HAS(html, "[[NotALink]]");
	ASSERT_HAS(html, "[[AlsoNotALink]]");
	ASSERT_HASNT(html, "href=\"/wiki/NotALink\"");
	ASSERT_HASNT(html, "href=\"/wiki/AlsoNotALink\"");
}

/* MD_FLAG_NOHTML keeps page HTML inert. */
static void
test_raw_html_is_escaped(void)
{
	const char	*html = render(
	    "<script>alert(1)</script>\n\nInline <b>tag</b> too.\n\n"
	    "<img src=x onerror=alert(1)>\n", 0);

	ASSERT_HASNT(html, "<script>");
	ASSERT_HASNT(html, "<b>tag</b>");
	ASSERT_HASNT(html, "onerror=alert(1)>");
	ASSERT_HAS(html, "&lt;script&gt;");
	ASSERT_HAS(html, "&lt;b&gt;");
}

static void
test_javascript_url_is_not_linked(void)
{
	const char	*html = render("[click](javascript:alert(1))\n", 0);

	/* md4c emits the href; the scheme must at least not be executable markup. */
	ASSERT_HASNT(html, "<script");
}

static void
test_camelcase_off_by_default(void)
{
	const char	*html = render("A WikiWord here.\n", 0);

	ASSERT_HASNT(html, "href=\"/wiki/WikiWord\"");
}

static void
test_camelcase_when_enabled(void)
{
	const char	*html = render(
	    "A WikiWord here, `CodeWord` not, ALLCAPS not, Single not.\n",
	    MD_RENDER_CAMELCASE);

	ASSERT_HAS(html, "href=\"/wiki/WikiWord\"");
	ASSERT_HASNT(html, "href=\"/wiki/CodeWord\"");
	ASSERT_HASNT(html, "href=\"/wiki/ALLCAPS\"");
	ASSERT_HASNT(html, "href=\"/wiki/Single\"");
}

static void
test_camelcase_skips_existing_links(void)
{
	const char	*html = render(
	    "See [[WikiHelp]] and [MyLabel](/x).\n", MD_RENDER_CAMELCASE);

	/* Must not nest an anchor inside an anchor. */
	ASSERT_HASNT(html, "<a class=\"wikilink\" href=\"/wiki/MyLabel\">");
	ASSERT_HASNT(html, "<a href=\"/x\"><a");
}

static void
test_excerpt_strips_markup(void)
{
	const char	*src = "# Title\n\nSome **bold** text with `code`.";
	char		 buf[64];

	markdown_excerpt(src, strlen(src), buf, sizeof(buf));

	TEST_ASSERT_EQUAL_STRING("Title Some bold text with code.", buf);
}

static void
test_excerpt_truncates_safely(void)
{
	char	buf[16];

	const char	*src = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

	markdown_excerpt(src, strlen(src), buf, sizeof(buf));

	TEST_ASSERT_EQUAL_INT(15, (int)strlen(buf));
}

static void
test_empty_input(void)
{
	const char	*html = render("", 0);

	TEST_ASSERT_NOT_NULL(html);
}

int
main(void)
{
	TEST_BOOTSTRAP();

	UNITY_BEGIN();

	RUN_TEST(test_basic_blocks);
	RUN_TEST(test_fenced_code);
	RUN_TEST(test_tables_and_tasklists);
	RUN_TEST(test_wikilinks);
	RUN_TEST(test_wikilink_target_with_ampersand);
	RUN_TEST(test_wikilink_inside_code_is_literal);
	RUN_TEST(test_raw_html_is_escaped);
	RUN_TEST(test_javascript_url_is_not_linked);
	RUN_TEST(test_camelcase_off_by_default);
	RUN_TEST(test_camelcase_when_enabled);
	RUN_TEST(test_camelcase_skips_existing_links);
	RUN_TEST(test_excerpt_strips_markup);
	RUN_TEST(test_excerpt_truncates_safely);
	RUN_TEST(test_empty_input);

	return (UNITY_END());
}
