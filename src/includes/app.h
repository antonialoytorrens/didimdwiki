/*
 * didimdwiki - a markdown wiki on ecewo.
 * Application-wide configuration and shared types.
 */

#ifndef DIDIMDWIKI_APP_H
#define DIDIMDWIKI_APP_H

#include <sys/types.h>
#include <time.h>

#define APP_NAME		"didimdwiki"
#define APP_VERSION		"1.0.0"

/* Data directory layout, relative to the process working directory. */
#define DATA_DIR		"data"
#define PAGES_DIR		DATA_DIR "/pages"
#define REVS_DIR		DATA_DIR "/pages/.rev"
#define UPLOADS_DIR		DATA_DIR "/uploads"
#define UPLOADS_META_DIR	DATA_DIR "/uploads/.meta"
#define TEMPLATES_DIR		DATA_DIR "/templates"
#define USERS_FILE		DATA_DIR "/users.tsv"
#define SESSIONS_FILE		DATA_DIR "/sessions.tsv"
/* Staging directory for multipart uploads before they are renamed into place. */
#define BODY_TMP_DIR		DATA_DIR "/tmp"

/* HTML templates and static assets, also relative to the working directory. */
#define VIEWS_DIR		"src/templates"
#define PUBLIC_DIR		"public"

#define PAGE_EXT		".md"
#define HOME_PAGE		"WikiHome"
#define HELP_PAGE		"WikiHelp"

#define NAME_MAX_LEN		128		/* page and file names */
#define PATH_MAX_LEN		1024
/*
 * Kept below the framework's 1MB buffered-body cap, with room for the
 * urlencoding of a form POST to inflate the body -- non-ASCII text can
 * roughly triple. Past this the request would be refused before a handler
 * ran, so v_body() rejects it first and the user gets a real error page.
 */
#define MAX_PAGE_BYTES		(512 * 1024)
#define MAX_UPLOAD_BYTES	(64 * 1024 * 1024)
#define REVISIONS_KEPT		20

#define SESSION_COOKIE		"didimdwiki_sid"
#define SESSION_TTL		(12 * 60 * 60)	/* seconds */
#define TOKEN_BYTES		32
#define TOKEN_HEX_LEN		((TOKEN_BYTES * 2) + 1)

/*
 * argon2id parameters. The tag is stored per row in users.tsv so these can be
 * raised later without a file format change; user_verify() reads the stored
 * tag rather than assuming the current values.
 */
#define ARGON2_MEMORY_KIB	65536		/* 64 MiB */
#define ARGON2_ITERATIONS	3
#define ARGON2_PARALLELISM	1
#define ARGON2_HASH_LEN		32
#define ARGON2_TAG_MAX		64		/* "argon2id$v=19$m=...,t=..,p=." */

struct user {
	char		name[NAME_MAX_LEN];
	char		algo[ARGON2_TAG_MAX];
	char		salt[TOKEN_HEX_LEN];
	char		hash[(ARGON2_HASH_LEN * 2) + 1];
	time_t		created;
};

struct session {
	char		token[TOKEN_HEX_LEN];
	char		user[NAME_MAX_LEN];
	char		csrf[TOKEN_HEX_LEN];
	time_t		expires;
};

/* Per-request state, hung off the ecewo request context. */
struct req_ctx {
	int		authed;
	struct user	user;
	struct session	session;
};

/* Runtime configuration; see app_parse_config() in main.c. */
struct app_config {
	int		allow_registration;
	int		camelcase_links;
	int		tls_enabled;
	int		login_required;
	char		bind[64];
	unsigned short	port;
	/*
	 * Read-only program data. Relative by default, which suits running
	 * from a checkout; a package installs these away from the writable
	 * state directory and points at them absolutely.
	 */
	char		views_dir[PATH_MAX_LEN];
	char		public_dir[PATH_MAX_LEN];
};

extern struct app_config	app_cfg;

#endif /* !DIDIMDWIKI_APP_H */
