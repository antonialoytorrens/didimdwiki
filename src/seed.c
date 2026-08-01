/*
 * didimdwiki - starter content, written once into an empty data directory.
 */

#include <string.h>

#include "app.h"
#include "models.h"
#include "seed.h"
#include "xmalloc.h"

static void	app_seed_file(const char *, const char *, const char *);

static const char	*seed_home =
"# Welcome to didimdwiki\n"
"\n"
"This is a small personal wiki written in C on the\n"
"[ecewo](https://github.com/ecewo/ecewo) framework. Pages are written in\n"
"**Markdown**.\n"
"\n"
"## Getting started\n"
"\n"
"- Edit this page with the *Edit* link above.\n"
"- Link to other pages with double brackets: [[WikiHelp]].\n"
"- Create a page by linking to it and following the link.\n"
"- Upload and manage files from the [Files](/files) page.\n"
"\n"
"See [[WikiHelp]] for the full markdown reference.\n";

static const char	*seed_help =
"# Markdown reference\n"
"\n"
"## Text\n"
"\n"
"    **bold**   *italic*   ~~strikethrough~~   `code`\n"
"\n"
"## Headings\n"
"\n"
"    # Heading 1\n"
"    ## Heading 2\n"
"    ### Heading 3\n"
"\n"
"## Lists\n"
"\n"
"    - bullet\n"
"      - nested bullet\n"
"    1. numbered\n"
"    - [ ] task to do\n"
"    - [x] task done\n"
"\n"
"## Links and images\n"
"\n"
"    [[PageName]]            link to a wiki page\n"
"    [[PageName|Label]]      link with your own text\n"
"    [text](https://url)     ordinary link\n"
"    ![alt](/files/pic.png)  image from the file store\n"
"\n"
"## Code\n"
"\n"
"Fence a block with three backticks and an optional language:\n"
"\n"
"    ```c\n"
"    int main(void) { return 0; }\n"
"    ```\n"
"\n"
"## Tables\n"
"\n"
"    | Column | Meaning |\n"
"    | ------ | ------- |\n"
"    | a      | first   |\n"
"    | b      | second  |\n"
"\n"
"## Quotes and rules\n"
"\n"
"    > a block quote\n"
"\n"
"    ---\n"
"\n"
"Raw HTML in page source is escaped and shown literally, never executed.\n";

static const char	*seed_tpl_meeting =
"# Meeting - YYYY-MM-DD\n"
"\n"
"**Present:**\n"
"\n"
"## Agenda\n"
"\n"
"1. \n"
"\n"
"## Notes\n"
"\n"
"## Actions\n"
"\n"
"- [ ] \n";

static const char	*seed_tpl_howto =
"# How to \n"
"\n"
"## Before you start\n"
"\n"
"- \n"
"\n"
"## Steps\n"
"\n"
"1. \n"
"\n"
"## If it goes wrong\n"
"\n"
"| Symptom | Cause | Fix |\n"
"| ------- | ----- | --- |\n"
"|         |       |     |\n"
"\n"
"See also: [[WikiHome]]\n";

void
app_seed(void)
{
	app_seed_file(PAGES_DIR, HOME_PAGE, seed_home);
	app_seed_file(PAGES_DIR, HELP_PAGE, seed_help);

	/* Starting points offered by the new-page form. */
	app_seed_file(TEMPLATES_DIR, "Meeting", seed_tpl_meeting);
	app_seed_file(TEMPLATES_DIR, "HowTo", seed_tpl_howto);
}

/* Writes the file only when absent, so local edits are never clobbered. */
static void
app_seed_file(const char *dir, const char *name, const char *body)
{
	char	path[PATH_MAX_LEN];

	if (store_path(path, sizeof(path), dir, name, PAGE_EXT) == -1)
		return;

	if (store_exists(path))
		return;

	if (store_write(path, body, strlen(body)) == -1)
		app_log(LOG_ERR, "failed to seed %s", name);
}
