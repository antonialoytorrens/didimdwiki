/*
 * didimdwiki - input validators.
 *
 * There is no framework-level validation layer: nothing inspects a form
 * field or query parameter before a handler sees it. Every one of these must
 * therefore be called explicitly at each point a value is read. An omission
 * is an unvalidated input reaching the model layer, and it will not announce
 * itself -- so when adding a handler, check that each value it reads has a
 * matching call here.
 *
 * All return 1 when valid, 0 otherwise, and all treat NULL as invalid.
 */

#ifndef DIDIMDWIKI_VALIDATE_H
#define DIDIMDWIKI_VALIDATE_H

/* store_valid_name(); the traversal defence for page and file names. */
int	v_pagename(const char *);
int	v_filename(const char *);

/* 2..127 chars of [A-Za-z0-9._-] */
int	v_username(const char *);

/* 8..128 characters, any content. */
int	v_password(const char *);

/* Free text: page bodies, search queries. Rejects control characters other
 * than \n, \r and \t, and anything over MAX_PAGE_BYTES. */
int	v_body(const char *);

/* Exactly TOKEN_HEX_LEN-1 hex digits. */
int	v_token(const char *);

/* Presence flag such as ?edit=1; the value is ignored, length is capped. */
int	v_bool(const char *);

/* ^[0-9]{1,20}$ */
int	v_number(const char *);

/* ^[a-z_]{1,24}$ */
int	v_action(const char *);

/* PUT, PATCH or DELETE, any case. */
int	v_method(const char *);

/* ^(name|mtime)$ */
int	v_sort(const char *);

#endif /* !DIDIMDWIKI_VALIDATE_H */
