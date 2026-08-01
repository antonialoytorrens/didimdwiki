/*
 * Unity - page.c against a stubbed store_*, for failure paths a real scratch
 * dir cannot reliably force (disk full, EIO, ...).
 *
 * The stub lives in support/store_stub.c. It is hand-written rather than
 * generated: two assertions do not justify a mock framework, and the call
 * counts read plainly.
 */

#include <stddef.h>

#include "store_stub.h"
#include "unity.h"

/*
 * page_write()'s own prototype rather than models.h, to keep this file
 * independent of the struct definitions the stub also has to satisfy.
 */
int	page_write(const char *, const char *, const void *, size_t);

void
setUp(void)
{
	store_stub_reset();
}

void
tearDown(void)
{
}

/* store_write() failing must propagate as page_write() failing. */
static void
test_write_failure_propagates(void)
{
	stub.path_result = 0;
	stub.exists_result = 0;	/* page is new, so no snapshot is taken */
	stub.write_result = -1;

	TEST_ASSERT_EQUAL_INT(-1, page_write("New", "alice", "body", 4));
	TEST_ASSERT_EQUAL_INT(1, stub.write_calls);
}

/*
 * page_write() only clears a stale redirect marker once store_write() has
 * actually succeeded - a failed write must not touch it.
 */
static void
test_write_failure_skips_redirect_cleanup(void)
{
	stub.path_result = 0;
	stub.exists_result = 0;
	stub.write_result = -1;

	TEST_ASSERT_EQUAL_INT(-1, page_write("New", "alice", "body", 4));

	/*
	 * The redirect cleanup is a second store_path() followed by
	 * store_exists() and store_unlink(). After a failed write none of
	 * them may run, so the counts pin the whole branch: one store_path()
	 * for the page itself, one store_exists() to decide on the snapshot,
	 * and no unlink at all.
	 */
	TEST_ASSERT_EQUAL_INT(1, stub.path_calls);
	TEST_ASSERT_EQUAL_INT(1, stub.exists_calls);
	TEST_ASSERT_EQUAL_INT(0, stub.unlink_calls);
}

int
main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_write_failure_propagates);
	RUN_TEST(test_write_failure_skips_redirect_cleanup);
	return UNITY_END();
}
