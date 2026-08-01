/*
 * Unity - accounts and sessions.
 * These tests pin the current password and session handling properties.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "unity.h"


#include "app.h"
#include "models.h"
#include "test_support.h"

static char	scratch[256];

/* Each test starts with an empty user and session store. */
void
setUp(void)
{
	unlink(USERS_FILE);
	unlink(SESSIONS_FILE);
}

void
tearDown(void)
{
}

static void
test_create_and_verify(void)
{
	struct user	u;

	TEST_ASSERT_EQUAL_INT(0, user_count());
	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(1, user_count());

	TEST_ASSERT_EQUAL_INT(0, user_verify("alice", "correcthorse", &u));
	TEST_ASSERT_EQUAL_STRING("alice", u.name);
	TEST_ASSERT_TRUE(u.created > 0);
}

static void
test_verify_rejects_wrong_password(void)
{
	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "correcthorse"));

	TEST_ASSERT_EQUAL_INT(-1, user_verify("alice", "wrong", NULL));
	TEST_ASSERT_EQUAL_INT(-1, user_verify("alice", "", NULL));
	TEST_ASSERT_EQUAL_INT(-1, user_verify("alice", "correcthorse ", NULL));
	TEST_ASSERT_EQUAL_INT(-1, user_verify("nobody", "correcthorse", NULL));
}

static void
test_duplicate_user_refused(void)
{
	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(-2, user_create("alice", "otherpassword"));
	TEST_ASSERT_EQUAL_INT(1, user_count());
}

static void
test_create_validates_input(void)
{
	TEST_ASSERT_EQUAL_INT(-1, user_create("a", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(-1, user_create("has space", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(-1, user_create("bad/name", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(-1, user_create("alice", "short"));
	TEST_ASSERT_EQUAL_INT(-1, user_create("alice", NULL));
	TEST_ASSERT_EQUAL_INT(0, user_count());
}

/* The salt is what makes two identical passwords hash differently. */
static void
test_passwords_are_salted(void)
{
	struct user	a, b;

	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "samepassword"));
	TEST_ASSERT_EQUAL_INT(0, user_create("bob", "samepassword"));

	TEST_ASSERT_EQUAL_INT(0, user_lookup("alice", &a));
	TEST_ASSERT_EQUAL_INT(0, user_lookup("bob", &b));

	TEST_ASSERT_TRUE(strcmp(a.salt, b.salt) != 0);
	TEST_ASSERT_TRUE(strcmp(a.hash, b.hash) != 0);

	/* And the stored value is nowhere near the plaintext. */
	TEST_ASSERT_NULL(strstr(a.hash, "samepassword"));
	TEST_ASSERT_EQUAL_INT(TOKEN_HEX_LEN - 1, (int)strlen(a.hash));
}

static void
test_change_password(void)
{
	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(0, user_set_password("alice", "batterystaple"));

	TEST_ASSERT_EQUAL_INT(-1, user_verify("alice", "correcthorse", NULL));
	TEST_ASSERT_EQUAL_INT(0, user_verify("alice", "batterystaple", NULL));

	TEST_ASSERT_EQUAL_INT(-1, user_set_password("alice", "short"));
	TEST_ASSERT_EQUAL_INT(-1, user_set_password("ghost", "batterystaple"));
}

/* Rewriting one row must not disturb the others. */
static void
test_rewrite_preserves_other_users(void)
{
	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(0, user_create("bob", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(0, user_create("carol", "correcthorse"));

	TEST_ASSERT_EQUAL_INT(0, user_set_password("bob", "newpassword"));

	TEST_ASSERT_EQUAL_INT(3, user_count());
	TEST_ASSERT_EQUAL_INT(0, user_verify("alice", "correcthorse", NULL));
	TEST_ASSERT_EQUAL_INT(0, user_verify("carol", "correcthorse", NULL));
	TEST_ASSERT_EQUAL_INT(0, user_verify("bob", "newpassword", NULL));
}

static void
test_delete_user(void)
{
	TEST_ASSERT_EQUAL_INT(0, user_create("alice", "correcthorse"));
	TEST_ASSERT_EQUAL_INT(0, user_create("bob", "correcthorse"));

	TEST_ASSERT_EQUAL_INT(0, user_delete("alice"));
	TEST_ASSERT_EQUAL_INT(1, user_count());
	TEST_ASSERT_EQUAL_INT(-1, user_lookup("alice", NULL));
	TEST_ASSERT_EQUAL_INT(0, user_lookup("bob", NULL));
}

static void
test_tokens_are_random_and_hex(void)
{
	char	a[TOKEN_HEX_LEN], b[TOKEN_HEX_LEN];
	size_t	i;

	token_generate(a, sizeof(a));
	token_generate(b, sizeof(b));

	TEST_ASSERT_EQUAL_INT(TOKEN_HEX_LEN - 1, (int)strlen(a));
	TEST_ASSERT_TRUE(strcmp(a, b) != 0);

	/*
	 * Hex specifically: http_populate_cookies() drops any cookie value
	 * containing '=', which base64 padding would introduce.
	 */
	for (i = 0; i < strlen(a); i++)
		TEST_ASSERT_TRUE(strchr("0123456789abcdef", a[i]) != NULL);
}

static void
test_session_roundtrip(void)
{
	struct session	s, found;

	TEST_ASSERT_EQUAL_INT(0, session_create("alice", &s));
	TEST_ASSERT_EQUAL_STRING("alice", s.user);
	TEST_ASSERT_TRUE(s.expires > time(NULL));
	TEST_ASSERT_TRUE(strcmp(s.token, s.csrf) != 0);

	TEST_ASSERT_EQUAL_INT(0, session_lookup(s.token, &found));
	TEST_ASSERT_EQUAL_STRING("alice", found.user);
	TEST_ASSERT_EQUAL_STRING(s.csrf, found.csrf);
}

static void
test_session_lookup_rejects_junk(void)
{
	struct session	s;

	TEST_ASSERT_EQUAL_INT(0, session_create("alice", &s));

	TEST_ASSERT_EQUAL_INT(-1, session_lookup("", NULL));
	TEST_ASSERT_EQUAL_INT(-1, session_lookup(NULL, NULL));
	TEST_ASSERT_EQUAL_INT(-1, session_lookup("tooshort", NULL));
	TEST_ASSERT_EQUAL_INT(-1, session_lookup(
	    "0000000000000000000000000000000000000000000000000000000000000000",
	    NULL));
}

static void
test_session_destroy(void)
{
	struct session	a, b;

	TEST_ASSERT_EQUAL_INT(0, session_create("alice", &a));
	TEST_ASSERT_EQUAL_INT(0, session_create("bob", &b));

	TEST_ASSERT_EQUAL_INT(0, session_destroy(a.token));

	TEST_ASSERT_EQUAL_INT(-1, session_lookup(a.token, NULL));
	TEST_ASSERT_EQUAL_INT(0, session_lookup(b.token, NULL));
}

static void
test_expired_session_is_rejected(void)
{
	FILE		*fp;
	struct session	 s, other;
	char		 token[TOKEN_HEX_LEN], csrf[TOKEN_HEX_LEN];

	token_generate(token, sizeof(token));
	token_generate(csrf, sizeof(csrf));

	fp = fopen(SESSIONS_FILE, "a");
	TEST_ASSERT_NOT_NULL(fp);
	fprintf(fp, "%s\t%s\t%s\t%lld\n", token, "alice", csrf,
	    (long long)(time(NULL) - 60));
	fclose(fp);

	TEST_ASSERT_EQUAL_INT(-1, session_lookup(token, &s));

	/*
	 * The sweep is a side effect of any rewrite, so destroying an
	 * unrelated session drops the expired row too.
	 */
	TEST_ASSERT_EQUAL_INT(0, session_create("bob", &other));
	TEST_ASSERT_EQUAL_INT(0, session_destroy(other.token));
	TEST_ASSERT_EQUAL_INT(-1, session_lookup(token, &s));
}

/* Destroying one session must not disturb any other live session. */
static void
test_destroy_keeps_other_sessions(void)
{
	struct session	a, b;

	TEST_ASSERT_EQUAL_INT(0, session_create("alice", &a));
	TEST_ASSERT_EQUAL_INT(0, session_create("bob", &b));

	TEST_ASSERT_EQUAL_INT(0, session_destroy(a.token));

	TEST_ASSERT_EQUAL_INT(-1, session_lookup(a.token, NULL));
	TEST_ASSERT_EQUAL_INT(0, session_lookup(b.token, NULL));
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
	RUN_TEST(test_create_and_verify);
	RUN_TEST(test_verify_rejects_wrong_password);
	RUN_TEST(test_duplicate_user_refused);
	RUN_TEST(test_create_validates_input);
	RUN_TEST(test_passwords_are_salted);
	RUN_TEST(test_change_password);
	RUN_TEST(test_rewrite_preserves_other_users);
	RUN_TEST(test_delete_user);
	RUN_TEST(test_tokens_are_random_and_hex);
	RUN_TEST(test_session_roundtrip);
	RUN_TEST(test_session_lookup_rejects_junk);
	RUN_TEST(test_session_destroy);
	RUN_TEST(test_expired_session_is_rejected);
	RUN_TEST(test_destroy_keeps_other_sessions);
	result = UNITY_END();

	test_rm_scratch(scratch);

	return (result);
}
