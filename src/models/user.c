/*
 * didimdwiki - user accounts.
 * data/users.tsv: name \t algo \t salt \t hash \t created
 *
 * Passwords are hashed with argon2id. The algo field is a self-describing
 * tag ("argon2id$v=19$m=65536,t=3,p=1") and verification reads the parameters
 * back out of it, so the cost can be raised later and existing rows keep
 * verifying against the cost they were written with.
 */

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <argon2.h>

#include "app.h"
#include "models.h"
#include "strutil.h"
#include "wbuf.h"
#include "xmalloc.h"

struct argon2_params {
	uint32_t	m_cost;		/* KiB */
	uint32_t	t_cost;
	uint32_t	parallelism;
};

static int	user_parse(char *, struct user *);
static int	user_hash(const struct argon2_params *, const char *,
		    const char *, char *, size_t);
static void	user_algo_tag(const struct argon2_params *, char *, size_t);
static int	user_algo_parse(const char *, struct argon2_params *);
static int	user_rewrite(const char *, const struct user *);
static int	user_list(struct user **, size_t *);
static void	random_bytes(uint8_t *, size_t);

static const struct argon2_params argon2_current = {
	.m_cost = ARGON2_MEMORY_KIB,
	.t_cost = ARGON2_ITERATIONS,
	.parallelism = ARGON2_PARALLELISM,
};

static void
random_bytes(uint8_t *buf, size_t len)
{
	int		fd;
	ssize_t		r;
	size_t		off = 0;

	/*
	 * /dev/urandom rather than getrandom(2), which is not POSIX. systemd's
	 * PrivateDevices= still provides it in the sandboxed /dev.
	 */
	if ((fd = open("/dev/urandom", O_RDONLY)) == -1)
		fatal("open(/dev/urandom): %s", strerror(errno));

	while (off < len) {
		r = read(fd, buf + off, len - off);
		if (r <= 0) {
			if (r == -1 && errno == EINTR)
				continue;
			fatal("read(/dev/urandom): %s", strerror(errno));
		}
		off += (size_t)r;
	}

	close(fd);
}

void
token_generate(char *out, size_t len)
{
	uint8_t		raw[TOKEN_BYTES];

	if (len < TOKEN_HEX_LEN)
		fatal("token_generate: buffer too small");

	/*
	 * Hex rather than base64: base64 pads with '=', and a cookie value
	 * containing '=' is ambiguous to parse.
	 */
	random_bytes(raw, sizeof(raw));
	str_hex(raw, sizeof(raw), out, len);
}

static void
user_algo_tag(const struct argon2_params *p, char *out, size_t len)
{
	if ((size_t)snprintf(out, len, "argon2id$v=%d$m=%u,t=%u,p=%u",
	    ARGON2_VERSION_NUMBER, p->m_cost, p->t_cost, p->parallelism) >= len)
		fatal("user_algo_tag: buffer too small");
}

static int
user_algo_parse(const char *tag, struct argon2_params *out)
{
	unsigned int	v, m, t, p;

	if (tag == NULL)
		return (-1);

	if (sscanf(tag, "argon2id$v=%u$m=%u,t=%u,p=%u", &v, &m, &t, &p) != 4)
		return (-1);

	if (v != ARGON2_VERSION_NUMBER)
		return (-1);

	/* Refuse absurd parameters rather than trying to honour them. */
	if (m < 8 || m > (1024 * 1024) || t < 1 || t > 64 || p < 1 || p > 16)
		return (-1);

	out->m_cost = m;
	out->t_cost = t;
	out->parallelism = p;

	return (0);
}

/*
 * The salt is stored hex-encoded and fed to argon2 as those ASCII bytes.
 * That is 64 bytes carrying 256 bits of entropy, and it keeps users.tsv
 * readable.
 */
static int
user_hash(const struct argon2_params *p, const char *salt,
    const char *password, char *out, size_t len)
{
	uint8_t		raw[ARGON2_HASH_LEN];
	int		rc;

	if (len < (ARGON2_HASH_LEN * 2) + 1)
		fatal("user_hash: buffer too small");

	rc = argon2id_hash_raw(p->t_cost, p->m_cost, p->parallelism,
	    password, strlen(password), salt, strlen(salt),
	    raw, sizeof(raw));

	if (rc != ARGON2_OK) {
		app_log(LOG_ERR, "argon2id_hash_raw: %s",
		    argon2_error_message(rc));
		return (-1);
	}

	str_hex(raw, sizeof(raw), out, len);

	return (0);
}

int
user_valid_name(const char *name)
{
	size_t		i, len;

	if (name == NULL)
		return (0);

	len = strlen(name);
	if (len < 2 || len >= NAME_MAX_LEN)
		return (0);

	for (i = 0; i < len; i++) {
		if (!isalnum((unsigned char)name[i]) && name[i] != '.' &&
		    name[i] != '_' && name[i] != '-')
			return (0);
	}

	return (1);
}

static int
user_parse(char *line, struct user *out)
{
	char		*fields[6];
	int		 count, err;

	count = str_split(line, "\t", fields, 6);
	if (count < 5)
		return (-1);

	memset(out, 0, sizeof(*out));

	if (str_lcpy(out->name, fields[0], sizeof(out->name)) >=
	    sizeof(out->name))
		return (-1);
	if (str_lcpy(out->algo, fields[1], sizeof(out->algo)) >=
	    sizeof(out->algo))
		return (-1);
	if (str_lcpy(out->salt, fields[2], sizeof(out->salt)) >=
	    sizeof(out->salt))
		return (-1);
	if (str_lcpy(out->hash, fields[3], sizeof(out->hash)) >=
	    sizeof(out->hash))
		return (-1);

	out->created = (time_t)str_tonum(fields[4], 10, 0, INT64_MAX, &err);

	if (err != STR_OK)
		out->created = 0;

	return (0);
}

static int
user_list(struct user **out, size_t *count)
{
	FILE		*fp;
	char		 line[1024];
	size_t		 cap;
	struct user	*users, *tmp, u;

	*out = NULL;
	*count = 0;

	if ((fp = fopen(USERS_FILE, "r")) == NULL)
		return (errno == ENOENT ? 0 : -1);

	cap = 16;
	users = xcalloc(cap, sizeof(*users));

	while (fgets(line, sizeof(line), fp) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '\0' || line[0] == '#')
			continue;

		if (user_parse(line, &u) == -1)
			continue;

		if (*count == cap) {
			cap *= 2;
			tmp = xcalloc(cap, sizeof(*users));
			memcpy(tmp, users, *count * sizeof(*users));
			free(users);
			users = tmp;
		}

		users[(*count)++] = u;
	}

	fclose(fp);

	*out = users;
	return (0);
}

int
user_count(void)
{
	size_t		 n;
	struct user	*users;

	if (user_list(&users, &n) == -1)
		return (-1);

	free(users);

	return ((int)n);
}

int
user_lookup(const char *name, struct user *out)
{
	size_t		 i, n;
	int		 found;
	struct user	*users;

	if (!user_valid_name(name))
		return (-1);

	if (user_list(&users, &n) == -1)
		return (-1);

	found = -1;

	for (i = 0; i < n; i++) {
		if (!strcmp(users[i].name, name)) {
			if (out != NULL)
				*out = users[i];
			found = 0;
			break;
		}
	}

	free(users);

	return (found);
}

int
user_create(const char *name, const char *password)
{
	FILE		*fp;
	struct user	 u;

	if (!user_valid_name(name))
		return (-1);
	if (password == NULL || strlen(password) < 8)
		return (-1);

	if (user_lookup(name, NULL) == 0)
		return (-2);	/* already exists */

	memset(&u, 0, sizeof(u));
	str_lcpy(u.name, name, sizeof(u.name));
	user_algo_tag(&argon2_current, u.algo, sizeof(u.algo));
	token_generate(u.salt, sizeof(u.salt));

	if (user_hash(&argon2_current, u.salt, password, u.hash,
	    sizeof(u.hash)) == -1)
		return (-1);

	u.created = time(NULL);

	if ((fp = fopen(USERS_FILE, "a")) == NULL)
		return (-1);

	fprintf(fp, "%s\t%s\t%s\t%s\t%lld\n", u.name, u.algo, u.salt, u.hash,
	    (long long)u.created);

	if (fclose(fp) != 0)
		return (-1);

	return (0);
}

int
user_verify(const char *name, const char *password, struct user *out)
{
	struct user		u;
	struct argon2_params	p;
	char			hash[(ARGON2_HASH_LEN * 2) + 1];

	if (password == NULL || user_lookup(name, &u) == -1)
		return (-1);

	/* Cost comes from the stored row, not from the current defaults. */
	if (user_algo_parse(u.algo, &p) == -1) {
		app_log(LOG_ERR, "user '%s': unrecognised algo tag '%s'",
		    u.name, u.algo);
		return (-1);
	}

	if (user_hash(&p, u.salt, password, hash, sizeof(hash)) == -1)
		return (-1);

	if (!str_timingsafe_equal(u.hash, hash))
		return (-1);

	if (out != NULL)
		*out = u;

	return (0);
}

/* Rewrites users.tsv with `updated` replacing the entry named `name`. */
static int
user_rewrite(const char *name, const struct user *updated)
{
	struct wbuf	 buf;
	struct user	*users;
	size_t		 i, n;
	int		 ret, found;

	if (user_list(&users, &n) == -1)
		return (-1);

	wbuf_init(&buf, 1024);
	found = 0;

	for (i = 0; i < n; i++) {
		const struct user	*u = &users[i];

		if (!strcmp(u->name, name)) {
			found = 1;
			if (updated == NULL)
				continue;	/* delete */
			u = updated;
		}

		wbuf_appendf(&buf, "%s\t%s\t%s\t%s\t%lld\n", u->name, u->algo,
		    u->salt, u->hash, (long long)u->created);
	}

	free(users);

	if (!found) {
		wbuf_cleanup(&buf);
		return (-1);
	}

	ret = store_write(USERS_FILE, buf.data, buf.offset);
	wbuf_cleanup(&buf);

	return (ret);
}

int
user_set_password(const char *name, const char *password)
{
	struct user	u;

	if (password == NULL || strlen(password) < 8)
		return (-1);

	if (user_lookup(name, &u) == -1)
		return (-1);

	/* A password change is also when a row picks up the current cost. */
	user_algo_tag(&argon2_current, u.algo, sizeof(u.algo));
	token_generate(u.salt, sizeof(u.salt));

	if (user_hash(&argon2_current, u.salt, password, u.hash,
	    sizeof(u.hash)) == -1)
		return (-1);

	return (user_rewrite(name, &u));
}

int
user_delete(const char *name)
{
	return (user_rewrite(name, NULL));
}
