/*
 * didimdwiki - application/x-www-form-urlencoded body parsing.
 */

#include <stdlib.h>
#include <string.h>

#include "form.h"
#include "xmalloc.h"

static int
hexval(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);

	return (-1);
}

/*
 * Decodes in place. A '%' that is not followed by two hex digits is kept
 * literally rather than dropped, so a malformed escape cannot silently
 * change the meaning of the rest of the value.
 */
size_t
form_urldecode(char *str)
{
	char		*out;
	size_t		 i, len;
	int		 hi, lo;

	len = strlen(str);
	out = str;

	for (i = 0; i < len; i++) {
		if (str[i] == '+') {
			*out++ = ' ';
			continue;
		}

		if (str[i] == '%' && i + 2 < len) {
			hi = hexval((unsigned char)str[i + 1]);
			lo = hexval((unsigned char)str[i + 2]);
			if (hi >= 0 && lo >= 0) {
				*out++ = (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}

		*out++ = str[i];
	}

	*out = '\0';

	return ((size_t)(out - str));
}

int
form_parse(struct form *form, const void *body, size_t len)
{
	char	*p, *end, *pair, *eq;

	memset(form, 0, sizeof(*form));

	if (body == NULL || len == 0)
		return (0);

	form->scratch = xmalloc(len + 1);
	memcpy(form->scratch, body, len);
	form->scratch[len] = '\0';

	/*
	 * A NUL inside the body would truncate everything after it, so treat
	 * that as malformed rather than parsing a prefix.
	 */
	if (strlen(form->scratch) != len) {
		form_free(form);
		return (-1);
	}

	p = form->scratch;
	end = p + len;

	while (p < end && form->count < FORM_MAX_FIELDS) {
		pair = p;

		if ((p = memchr(pair, '&', (size_t)(end - pair))) != NULL)
			*p++ = '\0';
		else
			p = end;

		if (*pair == '\0')
			continue;

		if ((eq = strchr(pair, '=')) != NULL) {
			*eq = '\0';
			form_urldecode(eq + 1);
			form->fields[form->count].value = eq + 1;
		} else {
			/* Valueless pair: present, but empty. */
			form->fields[form->count].value = (char *)"";
		}

		form_urldecode(pair);
		if (*pair == '\0')
			continue;

		form->fields[form->count].key = pair;
		form->count++;
	}

	return (0);
}

void
form_init(struct form *form)
{
	memset(form, 0, sizeof(*form));
}

int
form_add(struct form *form, const char *key, const char *value)
{
	if (form->count == FORM_MAX_FIELDS)
		return (-1);

	if (key == NULL || value == NULL)
		return (-1);

	/* Not copied: see the note in form.h about lifetimes. */
	form->fields[form->count].key = (char *)key;
	form->fields[form->count].value = (char *)value;
	form->count++;

	return (0);
}

void
form_free(struct form *form)
{
	/*
	 * Only scratch is owned. A form built with form_add() borrows its
	 * strings, so there is nothing else to release.
	 */
	free(form->scratch);
	form->scratch = NULL;
	form->count = 0;
}

const char *
form_get(const struct form *form, const char *key)
{
	size_t		i;

	for (i = 0; i < form->count; i++) {
		if (!strcmp(form->fields[i].key, key))
			return (form->fields[i].value);
	}

	return (NULL);
}
