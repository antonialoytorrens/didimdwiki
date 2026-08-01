/*
 * didimdwiki - application/x-www-form-urlencoded body parsing.
 *
 * ecewo exposes ecewo_query() for the URL query string but has no accessor
 * for form bodies, so this provides one.
 *
 * Nothing here validates. Every caller must run the value it reads through
 * the matching v_*() check in validate.h -- see the note there.
 */

#ifndef DIDIMDWIKI_FORM_H
#define DIDIMDWIKI_FORM_H

#include <stddef.h>

#define FORM_MAX_FIELDS	64

struct form_field {
	char	*key;
	char	*value;		/* never NULL; "" for a valueless pair */
};

struct form {
	struct form_field	fields[FORM_MAX_FIELDS];
	size_t			count;
	char			*scratch;	/* decoded copy of the body */
};

/*
 * Parses len bytes of urlencoded body. Returns 0 on success, -1 if the body
 * is unusable. Everything hangs off form->scratch; release with form_free().
 */
int		 form_parse(struct form *, const void *, size_t);
void		 form_free(struct form *);

/*
 * Builds a form by hand. Used to present multipart fields through the same
 * interface as an urlencoded body, so that csrf_verify(), method_override()
 * and the controllers do not have to care which encoding was used.
 *
 * form_add() stores the pointers without copying, so the strings must outlive
 * the form -- for multipart they are request-arena allocated, which they do.
 */
void		 form_init(struct form *);
int		 form_add(struct form *, const char *, const char *);

/* Returns NULL when absent. A present-but-empty field returns "". */
const char	*form_get(const struct form *, const char *);

/* Percent/plus decoding in place. Returns the new length. */
size_t		 form_urldecode(char *);

#endif /* !DIDIMDWIKI_FORM_H */
