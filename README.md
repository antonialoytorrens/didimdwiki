<a id="readme-top"></a>

<!-- PROJECT LOGO -->
<br />
<div align="center">
  <a href="https://github.com/antonialoytorrens/didimdwiki">
    <img src="public/img/favicon.svg" alt="Logo" width="80" height="80">
  </a>

  <h3 align="center">didimdwiki</h3>

  <p align="center">
    A small, fast personal wiki in C. Markdown pages, no database.
    <br />
    <a href="#usage"><strong>Explore the docs »</strong></a>
    <br />
    <br />
    <a href="https://github.com/antonialoytorrens/didimdwiki/issues">Report Bug</a>
    &middot;
    <a href="https://github.com/antonialoytorrens/didimdwiki/issues">Request Feature</a>
  </p>
</div>

## About The Project

didimdwiki is a wiki engine written in C, laid out the way an Express
application is: routes, controllers, middleware, models and views.

Everything lives in `data/` as ordinary files. Pages are Markdown, revisions
are snapshots on disk, and users and sessions are tab-separated text. There is
no database to install, back up or migrate: `tar` is the backup tool, and
`grep` works on the whole wiki.

What it does:

* Markdown pages with `[[WikiLinks]]`, tables, task lists and fenced code
* Revision history per page, with diffs and one-click restore
* Renames that leave a redirect behind, so old links keep working
* File uploads with server-side type checking
* Full-text and title search, an index, a recent-changes page and an RSS feed
* Accounts with argon2id password hashing and CSRF-protected forms
* A JSON API for pages and files
* Two access modes: fully public, or a private "portal" that requires a login
  to read anything

### Built With

* [ecewo](https://github.com/ecewo/ecewo) - asynchronous C web framework
* [md4c](https://github.com/mity/md4c) - Markdown parsing
* [cJSON](https://github.com/DaveGamble/cJSON) - the JSON API
* [argon2](https://github.com/P-H-C/phc-winner-argon2) - password hashing
* [Unity](https://github.com/ThrowTheSwitch/Unity) - unit tests

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Getting Started

### Prerequisites

Debian or Ubuntu:

```sh
sudo apt install build-essential \
    libecewo-dev libecewo-cookie-dev libecewo-fs-dev libecewo-static-dev \
    libecewo-multipart-dev libecewo-helmet-dev \
    libmd4c-dev libcjson-dev libargon2-dev
```

### Installation

1. Clone the repository
   ```sh
   git clone https://github.com/antonialoytorrens/didimdwiki.git
   cd didimdwiki
   ```
2. Build
   ```sh
   make build
   ```
3. Run it
   ```sh
   make run
   ```
4. Open <http://127.0.0.1:8888> and create the first account.

`make build` already produces a production build. For a debug build (assertions, no optimization):

```sh
make DEBUG=1 build
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Usage

```sh
./didimdwiki                                      # public wiki
./didimdwiki login_required=1 allow_registration=0  # private, closed
```

Paths are resolved relative to the working directory, so run it from the
project root.

### Configuration

Settings are `key=value` arguments, or `DIDIMDWIKI_<KEY>` environment
variables. An argument always wins over the environment.

| Key | Default | Effect |
|---|---|---|
| `login_required` | `0` | The "portal": require a session to *read* anything. Writing always requires one. |
| `allow_registration` | `1` | Gate `/register`. The first account is always allowed, so a fresh wiki can be bootstrapped. |
| `camelcase_links` | `0` | Autolink `CamelCase` words to pages. |
| `tls_enabled` | `0` | Add `Secure` to the session cookie and make RSS links `https://`. Set this when something in front terminates TLS. |
| `bind` | `127.0.0.1` | Listen address (numeric; hostnames are not resolved). |
| `port` | `8888` | Listen port. |
| `views_dir` | `src/templates` | Where the HTML templates are read from at startup. |
| `public_dir` | `public` | Directory served at `/public`. |

`views_dir` and `public_dir` are read-only program data. They default to
relative paths, which suits running from a checkout; an installed copy keeps
them alongside the binary and away from the writable state directory, so the
packaged service passes them absolutely.

Flags accept `1`/`yes`/`true` and `0`/`no`/`false`.

```sh
./didimdwiki login_required=1
DIDIMDWIKI_LOGIN_REQUIRED=1 ./didimdwiki
```

### Writing pages

| Syntax | Result |
|---|---|
| `[[PageName]]` | Link to a wiki page |
| `[[PageName\|Label]]` | Link with your own text |
| `![alt](/files/pic.png)` | Image from the file store |

Raw HTML in page source is escaped and shown literally, never executed.
`WikiHelp` in a fresh wiki has the full reference.

A single page is capped at 512 KB of Markdown, and an uploaded file at 64 MB.

### JSON API

Reads are open; anything that mutates needs a session cookie.

```sh
curl localhost:8888/api/v1/pages
curl localhost:8888/api/v1/pages/WikiHome
curl localhost:8888/api/v1/pages/WikiHome/revisions
curl -b jar -X PUT localhost:8888/api/v1/pages/WikiHome -d '{"body":"# Hello"}'
```

`/api/v1/files` mirrors it for uploads.

### Where things are kept

```
data/
├── users.tsv                 name, algorithm, salt, hash, created
├── sessions.tsv              token, user, csrf, expiry
├── pages/
│   ├── <Name>.md             the page
│   ├── <OldName>.redir       one line: the new name, left by a rename
│   └── .rev/<Name>/<stamp>.<author>.md
├── uploads/
│   ├── <slug>                the bytes
│   └── .meta/<slug>.tsv      name, original, mime, owner, size, mtime
├── templates/                starting points offered by the new-page form
└── tmp/                      upload staging
```

Writes go through a temporary file plus `rename(2)`, so a reader never sees a
half-written page, and a crash mid-save leaves the previous version intact.

### Tests

```sh
make test              # unit + integration
make test-unit         # pure functions, no fixtures
make test-integration  # models against a scratch directory
make test-e2e          # live server driven by curl
```

### Deployment

TLS is terminated by nginx in front; the application speaks plain HTTP.
`systemd/didimdwiki.service` creates the `data/` skeleton and confines the
process: read-only filesystem apart from its state directory, no capabilities,
no new privileges, and a syscall filter.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Contributing

Contributions are welcome.

1. Fork the project
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a pull request

Please run `make test` before opening a pull request, and add coverage for
anything touching `src/models/`.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## License

Distributed under the MIT License. See `LICENSE` for details.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Contact

Project Link:
[https://github.com/antonialoytorrens/didimdwiki.git](https://github.com/antonialoytorrens/didimdwiki.git)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Acknowledgments

* [ecewo](https://github.com/ecewo/ecewo) and its plugin ecosystem
* [md4c](https://github.com/mity/md4c) for a fast, standards-compliant Markdown parser
* [Best-README-Template](https://github.com/othneildrew/Best-README-Template)

<p align="right">(<a href="#readme-top">back to top</a>)</p>
