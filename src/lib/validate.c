/*
 * didimdwiki - input validators. See validate.h for why these are explicit.
 */

#include <ctype.h>
#include <strings.h>
#include <string.h>

#include "app.h"
#include "models.h"
#include "validate.h"

int
v_pagename(const char *data)
{
	return (data != NULL && store_valid_name(data));
}

int
v_filename(const char *data)
{
	return (data != NULL && store_valid_name(data));
}

int
v_username(const char *data)
{
	size_t		i, len;

	if (data == NULL)
		return (0);

	len = strlen(data);
	if (len < 2 || len >= NAME_MAX_LEN)
		return (0);

	for (i = 0; i < len; i++) {
		if (!isalnum((unsigned char)data[i]) && data[i] != '.' &&
		    data[i] != '_' && data[i] != '-')
			return (0);
	}

	return (1);
}

int
v_password(const char *data)
{
	size_t		len;

	if (data == NULL)
		return (0);

	len = strlen(data);

	return (len >= 8 && len <= 128);
}

int
v_body(const char *data)
{
	size_t		i, len;
	unsigned char	c;

	if (data == NULL)
		return (0);

	len = strlen(data);
	if (len > MAX_PAGE_BYTES)
		return (0);

	for (i = 0; i < len; i++) {
		c = (unsigned char)data[i];
		if (c < 0x20 && c != '\n' && c != '\r' && c != '\t')
			return (0);
	}

	return (1);
}

int
v_token(const char *data)
{
	size_t		i;

	if (data == NULL || strlen(data) != TOKEN_HEX_LEN - 1)
		return (0);

	for (i = 0; i < TOKEN_HEX_LEN - 1; i++) {
		if (!isxdigit((unsigned char)data[i]))
			return (0);
	}

	return (1);
}

int
v_bool(const char *data)
{
	return (data != NULL && strlen(data) <= 8);
}

/* ^[0-9]{1,20}$ */
int
v_number(const char *data)
{
	size_t		i, len;

	if (data == NULL)
		return (0);

	len = strlen(data);
	if (len < 1 || len > 20)
		return (0);

	for (i = 0; i < len; i++) {
		if (!isdigit((unsigned char)data[i]))
			return (0);
	}

	return (1);
}

/* ^[a-z_]{1,24}$ */
int
v_action(const char *data)
{
	size_t		i, len;

	if (data == NULL)
		return (0);

	len = strlen(data);
	if (len < 1 || len > 24)
		return (0);

	for (i = 0; i < len; i++) {
		if ((data[i] < 'a' || data[i] > 'z') && data[i] != '_')
			return (0);
	}

	return (1);
}

/* ^([Pp][Uu][Tt]|[Pp][Aa][Tt][Cc][Hh]|[Dd][Ee][Ll][Ee][Tt][Ee])$ */
int
v_method(const char *data)
{
	if (data == NULL)
		return (0);

	return (!strcasecmp(data, "PUT") || !strcasecmp(data, "PATCH") ||
	    !strcasecmp(data, "DELETE"));
}

/* ^(name|mtime)$ */
int
v_sort(const char *data)
{
	if (data == NULL)
		return (0);

	return (!strcmp(data, "name") || !strcmp(data, "mtime"));
}
