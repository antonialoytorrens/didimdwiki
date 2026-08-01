/*
 * didimdwiki - sessions.
 * data/sessions.tsv: token \t user \t csrf \t expires
 *
 * A random bearer token is stored in a cookie.
 *
 * Deliberately not ecewo-session: that plugin keeps sessions in memory only
 * (so a restart logs everyone out), hardcodes the cookie name to "session",
 * and has nowhere to put the per-session CSRF token. A file is also what
 * lets session_rewrite() expire stale rows as a side effect of logout.
 */

#include <sys/types.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "models.h"
#include "strutil.h"
#include "wbuf.h"

static int	session_parse(char *, struct session *);
static int	session_rewrite(const char *, const struct session *);

static int
session_parse(char *line, struct session *out)
{
	char		*fields[5];
	int		count, err;

	count = str_split(line, "\t", fields, 5);
	if (count < 4)
		return (-1);

	memset(out, 0, sizeof(*out));

	if (str_lcpy(out->token, fields[0], sizeof(out->token)) >=
	    sizeof(out->token))
		return (-1);
	if (str_lcpy(out->user, fields[1], sizeof(out->user)) >=
	    sizeof(out->user))
		return (-1);
	if (str_lcpy(out->csrf, fields[2], sizeof(out->csrf)) >=
	    sizeof(out->csrf))
		return (-1);

	out->expires = (time_t)str_tonum(fields[3], 10, 0, INT64_MAX, &err);
	if (err != STR_OK)
		return (-1);

	return (0);
}

int
session_create(const char *user, struct session *out)
{
	FILE		*fp;
	struct session	s;

	memset(&s, 0, sizeof(s));

	if (str_lcpy(s.user, user, sizeof(s.user)) >= sizeof(s.user))
		return (-1);

	token_generate(s.token, sizeof(s.token));
	token_generate(s.csrf, sizeof(s.csrf));
	s.expires = time(NULL) + SESSION_TTL;

	if ((fp = fopen(SESSIONS_FILE, "a")) == NULL)
		return (-1);

	fprintf(fp, "%s\t%s\t%s\t%lld\n", s.token, s.user, s.csrf,
	    (long long)s.expires);

	if (fclose(fp) != 0)
		return (-1);

	if (out != NULL)
		*out = s;

	return (0);
}

int
session_lookup(const char *token, struct session *out)
{
	FILE		*fp;
	char		line[1024];
	struct session	s;
	time_t		now;

	if (token == NULL || strlen(token) != TOKEN_HEX_LEN - 1)
		return (-1);

	if ((fp = fopen(SESSIONS_FILE, "r")) == NULL)
		return (-1);

	now = time(NULL);

	while (fgets(line, sizeof(line), fp) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '\0')
			continue;

		if (session_parse(line, &s) == -1)
			continue;

		/* Constant time compare so the token is not a timing oracle. */
		if (!str_timingsafe_equal(s.token, token))
			continue;

		fclose(fp);

		if (s.expires <= now)
			return (-1);

		if (out != NULL)
			*out = s;

		return (0);
	}

	fclose(fp);
	return (-1);
}

/* Rewrites the file, dropping expired rows and the row matching `token`. */
static int
session_rewrite(const char *drop, const struct session *replace)
{
	FILE		*fp;
	char		line[1024];
	struct wbuf	buf;
	struct session	s;
	time_t		now;
	int		ret;

	wbuf_init(&buf, 1024);
	now = time(NULL);

	if ((fp = fopen(SESSIONS_FILE, "r")) != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL) {
			line[strcspn(line, "\r\n")] = '\0';
			if (line[0] == '\0')
				continue;

			if (session_parse(line, &s) == -1)
				continue;
			if (s.expires <= now)
				continue;
			if (drop != NULL && !strcmp(s.token, drop))
				continue;

			wbuf_appendf(&buf, "%s\t%s\t%s\t%lld\n", s.token,
			    s.user, s.csrf, (long long)s.expires);
		}
		fclose(fp);
	}

	if (replace != NULL) {
		wbuf_appendf(&buf, "%s\t%s\t%s\t%lld\n", replace->token,
		    replace->user, replace->csrf, (long long)replace->expires);
	}

	ret = store_write(SESSIONS_FILE, buf.data, buf.offset);
	wbuf_cleanup(&buf);

	return (ret);
}

int
session_destroy(const char *token)
{
	/*
	 * The rewrite also drops every expired row, which is the only
	 * garbage collection the session file gets.
	 */
	return (session_rewrite(token, NULL));
}
