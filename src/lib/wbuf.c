/*
 * didimdwiki - growable byte buffer.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wbuf.h"
#include "xmalloc.h"

#define WBUF_GROW_MIN	1024

/*
 * memmem() is a GNU extension. Naive search is fine here: the needles are
 * short template keys such as "$BODY$", and the haystack is one page.
 */
static void *
mem_find(const void *haystack, size_t hlen, const void *needle, size_t nlen)
{
	const char	*h = haystack;
	size_t		 i;

	if (nlen == 0 || hlen < nlen)
		return (NULL);

	for (i = 0; i <= hlen - nlen; i++) {
		if (h[i] == *(const char *)needle &&
		    !memcmp(h + i, needle, nlen))
			return ((void *)(h + i));
	}

	return (NULL);
}

static void
wbuf_reserve(struct wbuf *buf, size_t needed)
{
	size_t		want;

	if (buf->offset + needed <= buf->length)
		return;

	want = buf->length ? buf->length : WBUF_GROW_MIN;
	while (want < buf->offset + needed) {
		/* Guard the doubling itself, not just the result. */
		if (want > (SIZE_MAX / 2))
			fatal("wbuf: buffer too large");
		want *= 2;
	}

	buf->data = xrealloc(buf->data, want);
	buf->length = want;
}

void
wbuf_init(struct wbuf *buf, size_t initial)
{
	buf->data = (initial > 0) ? xmalloc(initial) : NULL;
	buf->length = initial;
	buf->offset = 0;
	buf->owner_api = 0;
}

struct wbuf *
wbuf_alloc(size_t initial)
{
	struct wbuf	*buf;

	buf = xmalloc(sizeof(*buf));
	wbuf_init(buf, initial);
	buf->owner_api = 1;

	return (buf);
}

void
wbuf_append(struct wbuf *buf, const void *data, size_t len)
{
	if (len == 0)
		return;

	wbuf_reserve(buf, len);
	memcpy(buf->data + buf->offset, data, len);
	buf->offset += len;
}

/*
 * vsnprintf twice -- measure, then format into a buffer of the right size --
 * rather than vasprintf, which is not POSIX. The common case fits the stack
 * buffer and only formats once.
 */
void
wbuf_appendv(struct wbuf *buf, const char *fmt, va_list args)
{
	int		 l;
	va_list		 copy;
	char		*heap, stack[BUFSIZ];

	va_copy(copy, args);

	l = vsnprintf(stack, sizeof(stack), fmt, args);
	if (l < 0)
		fatal("wbuf_appendv: vsnprintf failed");

	if ((size_t)l >= sizeof(stack)) {
		heap = xmalloc((size_t)l + 1);

		if (vsnprintf(heap, (size_t)l + 1, fmt, copy) != l)
			fatal("wbuf_appendv: vsnprintf length mismatch");

		wbuf_append(buf, heap, (size_t)l);
		free(heap);
	} else {
		wbuf_append(buf, stack, (size_t)l);
	}

	va_end(copy);
}

void
wbuf_appendf(struct wbuf *buf, const char *fmt, ...)
{
	va_list		args;

	va_start(args, fmt);
	wbuf_appendv(buf, fmt, args);
	va_end(args);
}

void
wbuf_reset(struct wbuf *buf)
{
	buf->offset = 0;
}

void
wbuf_cleanup(struct wbuf *buf)
{
	free(buf->data);
	buf->data = NULL;
	buf->length = 0;
	buf->offset = 0;
}

void
wbuf_free(struct wbuf *buf)
{
	if (buf == NULL)
		return;

	wbuf_cleanup(buf);
	free(buf);
}

char *
wbuf_stringify(struct wbuf *buf, size_t *len)
{
	char		c;

	if (len != NULL)
		*len = buf->offset;

	c = '\0';
	wbuf_append(buf, &c, sizeof(c));

	return ((char *)buf->data);
}

/*
 * Replaces every occurrence of src with len bytes of dst, in place.
 * Resumes scanning past the replacement, so a substitution that reintroduces
 * the key cannot loop.
 */
void
wbuf_replace_string(struct wbuf *b, const char *src, const void *dst,
    size_t len)
{
	char		*key, *end, *tmp, *p;
	size_t		 blen, off, tail, nlen, klen;

	klen = strlen(src);
	if (klen == 0)
		return;

	off = 0;
	for (;;) {
		if (b->offset < off + klen)
			break;

		p = (char *)b->data;
		key = mem_find(p + off, b->offset - off, src, klen);
		if (key == NULL)
			break;

		blen = b->offset;
		nlen = blen + len;
		end = key + klen;
		off = (size_t)(key - p);
		tail = (size_t)((p + blen) - end);

		tmp = xmalloc(nlen ? nlen : 1);
		memcpy(tmp, p, off);
		if (dst != NULL && len > 0)
			memcpy(tmp + off, dst, len);
		memcpy(tmp + off + len, end, tail);

		free(b->data);
		b->data = (uint8_t *)tmp;
		b->offset = off + len + tail;
		b->length = nlen;

		off = off + len;
	}
}
