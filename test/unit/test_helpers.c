/*
 * Unity - escaping and formatting helpers.
 * Every user-supplied string reaches a template through one of these.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "unity.h"

#include "test_support.h"


#include "app.h"
#include "wbuf.h"
#include "helpers.h"

static struct wbuf	buf;

void
setUp(void)
{
	wbuf_init(&buf, 256);
}

void
tearDown(void)
{
	wbuf_cleanup(&buf);
}

static const char *
esc(const char *in)
{
	html_escape(&buf, in, strlen(in));

	return (wbuf_stringify(&buf, NULL));
}

static void
test_html_escape(void)
{
	TEST_ASSERT_EQUAL_STRING(
	    "&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt;",
	    esc("<script>alert(\"x\")</script>"));
}

static void
test_html_escape_covers_all_five(void)
{
	/* Single quotes matter: attributes in our templates use them. */
	TEST_ASSERT_EQUAL_STRING("&lt;&gt;&amp;&quot;&#39;", esc("<>&\"'"));
}

static void
test_html_escape_passes_plain_text(void)
{
	TEST_ASSERT_EQUAL_STRING("Hello, world 123", esc("Hello, world 123"));
}

static void
test_html_escape_keeps_utf8(void)
{
	TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9 na\xc3\xafve",
	    esc("caf\xc3\xa9 na\xc3\xafve"));
}

static void
test_xml_escape_drops_control_bytes(void)
{
	const char *in = "ok\x01\x02 <bad>";

	xml_escape(&buf, in, strlen(in));

	/* Control bytes are illegal in XML 1.0, so they are removed. */
	TEST_ASSERT_EQUAL_STRING("ok &lt;bad&gt;", wbuf_stringify(&buf, NULL));
}

static void
test_url_encode(void)
{
	url_encode(&buf, "My Page/../x?a=1&b", 18);

	TEST_ASSERT_EQUAL_STRING("My%20Page%2F..%2Fx%3Fa%3D1%26b",
	    wbuf_stringify(&buf, NULL));
}

static void
test_url_encode_keeps_unreserved(void)
{
	url_encode(&buf, "aZ09-_.~", 8);

	TEST_ASSERT_EQUAL_STRING("aZ09-_.~", wbuf_stringify(&buf, NULL));
}

static void
test_url_encode_str_roundtrip(void)
{
	char	*out = url_encode_str("Some Page");

	TEST_ASSERT_EQUAL_STRING("Some%20Page", out);
	free(out);
}

static void
test_html_escape_str(void)
{
	char	*out = html_escape_str("a<b");

	TEST_ASSERT_EQUAL_STRING("a&lt;b", out);
	free(out);
}

static void
test_fmt_size(void)
{
	char	out[32];

	fmt_size(0, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("0 B", out);

	fmt_size(512, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("512 B", out);

	fmt_size(2048, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("2.0 KB", out);

	fmt_size(5 * 1024 * 1024, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("5.0 MB", out);
}

static void
test_fmt_rfc822_is_locale_independent(void)
{
	char	out[64];

	/* 2001-09-09 01:46:40 UTC */
	fmt_rfc822((time_t)1000000000, out, sizeof(out));

	TEST_ASSERT_EQUAL_STRING("Sun, 09 Sep 2001 01:46:40 GMT", out);
}

static void
test_fmt_date_fits_buffer(void)
{
	char	out[64];

	fmt_date((time_t)1000000000, out, sizeof(out));

	TEST_ASSERT_EQUAL_INT(16, (int)strlen(out));
}

int
main(void)
{
	TEST_BOOTSTRAP();

	UNITY_BEGIN();

	RUN_TEST(test_html_escape);
	RUN_TEST(test_html_escape_covers_all_five);
	RUN_TEST(test_html_escape_passes_plain_text);
	RUN_TEST(test_html_escape_keeps_utf8);
	RUN_TEST(test_xml_escape_drops_control_bytes);
	RUN_TEST(test_url_encode);
	RUN_TEST(test_url_encode_keeps_unreserved);
	RUN_TEST(test_url_encode_str_roundtrip);
	RUN_TEST(test_html_escape_str);
	RUN_TEST(test_fmt_size);
	RUN_TEST(test_fmt_rfc822_is_locale_independent);
	RUN_TEST(test_fmt_date_fits_buffer);

	return (UNITY_END());
}
