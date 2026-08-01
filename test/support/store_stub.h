/*
 * Recording/scripting interface for the store.c stand-in. See store_stub.c.
 */

#ifndef DIDIMDWIKI_STORE_STUB_H
#define DIDIMDWIKI_STORE_STUB_H

#include <stdio.h>

struct store_stub {
	/* Scripted return values. */
	int	write_result;
	int	exists_result;
	int	path_result;
	int	rename_result;

	/* Call counts. */
	int	write_calls;
	int	exists_calls;
	int	path_calls;
	int	unlink_calls;
	int	rename_calls;
};

extern struct store_stub	stub;

void	store_stub_reset(void);

#endif /* !DIDIMDWIKI_STORE_STUB_H */
