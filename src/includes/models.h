/*
 * didimdwiki - storage layer.
 * Nothing here knows about HTTP; everything is plain C over the data
 * directory. Blocking POSIX I/O, called directly from handlers -- ecewo runs
 * one thread, so there is no concurrent writer and no locking.
 */

#ifndef DIDIMDWIKI_MODELS_H
#define DIDIMDWIKI_MODELS_H

#include <sys/types.h>
#include <time.h>

#include "app.h"
#include "wbuf.h"

/* ---------------------------------------------------------------- store */

#define STORE_SORT_NAME		0
#define STORE_SORT_MTIME	1	/* newest first */

struct store_ent {
	char		name[NAME_MAX_LEN];
	off_t		size;
	time_t		mtime;
};

struct store_list {
	struct store_ent	*ents;
	size_t			count;
};

int	store_verify_dirs(void);
int	store_valid_name(const char *);
int	store_path(char *, size_t, const char *, const char *, const char *);
int	store_exists(const char *);
int	store_stat(const char *, off_t *, time_t *);
int	store_read(const char *, struct wbuf *);
int	store_write(const char *, const void *, size_t);
int	store_copy(const char *, const char *);
int	store_rename(const char *, const char *);
int	store_unlink(const char *);
int	store_list(const char *, const char *, int, struct store_list *);
void	store_list_free(struct store_list *);

/* Slugify an untrusted filename into something safe as a path component. */
int	store_slugify(const char *, char *, size_t);

/* ---------------------------------------------------------------- pages */

int	page_exists(const char *);
int	page_read(const char *, struct wbuf *);
int	page_write(const char *, const char *, const void *, size_t);
int	page_rename(const char *, const char *, const char *);
int	page_redirect_lookup(const char *, char *, size_t);
int	page_delete(const char *, const char *);
int	page_list(int, struct store_list *);
int	page_stat(const char *, off_t *, time_t *);

/* ------------------------------------------------------------ revisions */

struct revision {
	time_t		stamp;
	char		author[NAME_MAX_LEN];
	off_t		size;
};

struct revision_list {
	struct revision	*revs;
	size_t		count;
};

int	revision_snapshot(const char *, const char *);
int	revision_list(const char *, struct revision_list *);
void	revision_list_free(struct revision_list *);
int	revision_read(const char *, time_t, struct wbuf *);
int	revision_purge(const char *);
int	revision_rename(const char *, const char *);

/* Unified-ish line diff, rendered as HTML into the buffer. */
void	diff_render_html(const char *, size_t, const char *, size_t,
	    struct wbuf *);

/* -------------------------------------------------------------- uploads */

struct upload_meta {
	char		name[NAME_MAX_LEN];
	char		original[NAME_MAX_LEN];
	char		mime[128];
	char		owner[NAME_MAX_LEN];
	off_t		size;
	time_t		mtime;
};

const char	*upload_mime_for(const char *);
int		 upload_mime_allowed(const char *);
int		 upload_exists(const char *);
int		 upload_meta_read(const char *, struct upload_meta *);
int		 upload_meta_write(const struct upload_meta *);
int		 upload_list(int, struct upload_meta **, size_t *);
int		 upload_finish(const char *, const char *, const char *, off_t);
int		 upload_rename(const char *, const char *);
int		 upload_delete(const char *);
int		 upload_path(char *, size_t, const char *);

/*
 * Moves a completed multipart upload from its staging path into place.
 * ecewo-multipart has already written the body to disk, so this is the
 * rename plus the size check.
 */
int		 upload_adopt(const char *, const char *, off_t *);

/* ---------------------------------------------------------------- users */

int	user_count(void);
int	user_lookup(const char *, struct user *);
int	user_create(const char *, const char *);
int	user_verify(const char *, const char *, struct user *);
int	user_set_password(const char *, const char *);
int	user_delete(const char *);
int	user_valid_name(const char *);

/* ------------------------------------------------------------- sessions */

int	session_create(const char *, struct session *);
int	session_lookup(const char *, struct session *);
int	session_destroy(const char *);

/* Random hex token, TOKEN_HEX_LEN bytes including the NUL. */
void	token_generate(char *, size_t);

#endif /* !DIDIMDWIKI_MODELS_H */
