/*
 * didimdwiki - shared test bootstrap.
 */

#ifndef DIDIMDWIKI_TEST_SUPPORT_H
#define DIDIMDWIKI_TEST_SUPPORT_H

/*
 * Nothing to bootstrap: allocation is plain malloc. Kept as a hook so a test
 * needing per-binary setup has somewhere to put it.
 */
#define TEST_BOOTSTRAP()	do { } while (0)

/* Creates a unique scratch data dir and chdir()s into it. Returns 0 on ok. */
int	test_chdir_scratch(char *, size_t);
void	test_rm_scratch(const char *);

/*
 * mkdir -p's the DATA_DIR skeleton store_verify_dirs() expects (pages/,
 * uploads/, templates/, tmp/, ...) inside the current (scratch) cwd.
 * A real deployment gets this from the checked-in data/ tree; tests build it
 * fresh each run instead. Returns 0 on ok.
 */
int	test_init_data_dirs(void);

#endif /* !DIDIMDWIKI_TEST_SUPPORT_H */
