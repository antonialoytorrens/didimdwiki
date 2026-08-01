#!/bin/bash
set -uo pipefail

APP_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
BASE=http://127.0.0.1:48888
JAR=$(mktemp)
PASS=0
FAIL=0

cd "$APP_DIR"

cleanup() {
	./test/e2e/server.sh stop >/dev/null 2>&1
	rm -f "$JAR"
}
trap cleanup EXIT

ok() { PASS=$((PASS + 1)); printf '  \033[32mok\033[0m   %s\n' "$1"; }
no() { FAIL=$((FAIL + 1)); printf '  \033[31mFAIL\033[0m %s\n' "$1"; }

is() {
	if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (want $2, got $3)"; fi
}

has() {
	if printf '%s' "$3" | grep -qF -- "$2"; then
		ok "$1"
	else
		no "$1 (missing '$2')"
	fi
}

hasnt() {
	if printf '%s' "$3" | grep -qF -- "$2"; then
		no "$1 (unexpected '$2')"
	else
		ok "$1"
	fi
}

code()  { curl -s -o /dev/null -w '%{http_code}' "$@"; }
scode() { curl -s -b "$JAR" -c "$JAR" -o /dev/null -w '%{http_code}' "$@"; }
sbody() { curl -s -b "$JAR" -c "$JAR" "$@"; }

csrf_for() {
	sbody "$1" | grep -oE 'name="csrf" value="[a-f0-9]{64}"' | head -1 |
	    grep -oE '[a-f0-9]{64}'
}

echo "==> starting server on a fresh data directory"
./test/e2e/server.sh restart --fresh >/dev/null || { echo "server failed"; exit 1; }

echo
echo "== public pages =="
is "GET /"                200 "$(code $BASE/)"
is "GET /wiki/WikiHome"   200 "$(code $BASE/wiki/WikiHome)"
is "GET /index"           200 "$(code $BASE/index)"
is "GET /changes"         200 "$(code $BASE/changes)"
is "GET /files"           200 "$(code $BASE/files)"
is "GET /search"          200 "$(code "$BASE/search?q=markdown")"
is "GET /public/css"      200 "$(code $BASE/public/css/style.css)"
is "GET missing page"     404 "$(code $BASE/wiki/NoSuchPage)"

echo
echo "== markdown rendering =="
HOME_HTML=$(curl -s $BASE/wiki/WikiHome)
has "renders headings"    "<h1>Welcome to didimdwiki</h1>" "$HOME_HTML"
has "renders wikilinks"   'href="/wiki/WikiHelp"'          "$HOME_HTML"
has "renders lists"       "<li>"                           "$HOME_HTML"

echo
echo "== path traversal is refused =="
# Two spellings, refused at two different layers. An unencoded ../ makes the
# path multi-segment, and /wiki/:name is a two-segment route, so nothing
# matches and no handler runs -- 404. Percent-encoded, it stays one segment,
# reaches the handler, and store_valid_name() rejects it -- 400. That second
# case is the one exercising the traversal defence itself.
is "dotdot in /wiki"      404 "$(code --path-as-is $BASE/wiki/../../../../etc/passwd)"
hasnt "no passwd leaked"  "root:" "$(curl -s --path-as-is $BASE/wiki/../../../../etc/passwd)"
is "encoded dotdot"       400 "$(code --path-as-is $BASE/wiki/..%2f..%2fetc%2fpasswd)"
is "dotdot in /files"     400 "$(code --path-as-is $BASE/files/..%2f..%2fetc%2fpasswd)"
is "hidden .rev dir"      400 "$(code $BASE/wiki/.rev)"
is "dotdot in api"        400 "$(code --path-as-is $BASE/api/v1/pages/..%2f..%2fetc%2fpasswd)"

echo
echo "== anonymous writes are refused =="
is "api DELETE page"      401 "$(code -X DELETE $BASE/api/v1/pages/WikiHome)"
is "api POST page"        401 "$(code -X POST $BASE/api/v1/pages -d '{"name":"Evil","body":"x"}')"
is "api PUT page"         401 "$(code -X PUT $BASE/api/v1/pages/WikiHome -d '{"body":"x"}')"
is "api POST file"        401 "$(code -X POST $BASE/api/v1/files -F 'file=@Makefile')"
has "page untouched"      "Welcome to didimdwiki" "$(curl -s $BASE/api/v1/pages/WikiHome)"

echo
echo "== registration and session =="
is "register alice"       303 "$(scode -X POST $BASE/register \
	-d 'username=alice&password=correcthorse&password2=correcthorse')"
has "session cookie set"  "didimdwiki_sid" "$(cat "$JAR")"
has "cookie is HttpOnly"  "HttpOnly" \
	"$(curl -si -X POST $BASE/register \
	    -d 'username=bob&password=correcthorse&password2=correcthorse' |
	    grep -i set-cookie)"
ACCOUNT=$(sbody $BASE/account)
has "account shows user" "<td>alice</td>" "$ACCOUNT"
hasnt "no broken markup"  "<tbody<" "$ACCOUNT"

echo
echo "== csrf =="
CSRF=$(csrf_for "$BASE/wiki/WikiHome?edit=1")
is "csrf token length"    64 "${#CSRF}"
is "save without token"   403 "$(scode -X POST $BASE/wiki/WikiHome \
	-d 'action=save&body=nope')"
has "content unchanged"   "Welcome to didimdwiki" \
	"$(curl -s $BASE/api/v1/pages/WikiHome)"

echo
echo "== page CRUD via forms =="
is "save with token"      303 "$(scode -X POST $BASE/wiki/WikiHome \
	--data-urlencode "csrf=$CSRF" -d 'action=save' \
	--data-urlencode 'body=# Edited by alice')"
has "new body served"     "Edited by alice" "$(curl -s $BASE/api/v1/pages/WikiHome)"
is "history has 1 rev"    1 "$(curl -s $BASE/api/v1/pages/WikiHome/revisions |
	grep -o '"stamp"' | wc -l)"

is "create page"          303 "$(scode -X POST $BASE/wiki \
	--data-urlencode "csrf=$CSRF" -d 'action=create&name=ScratchPage' \
	--data-urlencode 'body=# Scratch')"
is "new page reachable"   200 "$(code $BASE/wiki/ScratchPage)"

is "rename page"          303 "$(scode -X POST $BASE/wiki/ScratchPage \
	--data-urlencode "csrf=$CSRF" -d 'action=rename&newname=RenamedPage')"
is "old name redirects"   301 "$(code $BASE/wiki/ScratchPage)"
has "redirect content ok" "Scratch" "$(curl -sL $BASE/wiki/ScratchPage)"
is "new name works"       200 "$(code $BASE/wiki/RenamedPage)"

is "recreate old name"    303 "$(scode -X POST $BASE/wiki \
	--data-urlencode "csrf=$CSRF" -d 'action=create&name=ScratchPage' \
	--data-urlencode 'body=# Reborn')"
is "old name is now 200"  200 "$(code $BASE/wiki/ScratchPage)"
has "old name own content" "Reborn" "$(curl -s $BASE/wiki/ScratchPage)"
is "delete recreated"     303 "$(scode -X POST $BASE/wiki/ScratchPage \
	--data-urlencode "csrf=$CSRF" -d '_method=delete')"

is "delete page"          303 "$(scode -X POST $BASE/wiki/RenamedPage \
	--data-urlencode "csrf=$CSRF" -d '_method=delete')"
is "deleted page gone"    404 "$(code $BASE/wiki/RenamedPage)"

echo
echo "== page CRUD via json api =="
is "api create"           201 "$(scode -X POST $BASE/api/v1/pages \
	-d '{"name":"ApiPage","body":"# Api\n\n- one\n- two"}')"
has "api read"            '"name":"ApiPage"' "$(sbody $BASE/api/v1/pages/ApiPage)"
is "api update"           200 "$(scode -X PUT $BASE/api/v1/pages/ApiPage \
	-d '{"body":"# Api edited"}')"
has "api update applied"  "Api edited" "$(sbody $BASE/api/v1/pages/ApiPage)"
is "api rename"           200 "$(scode -X PATCH $BASE/api/v1/pages/ApiPage \
	-d '{"name":"ApiRenamed"}')"
is "api renamed reachable" 200 "$(scode $BASE/api/v1/pages/ApiRenamed)"
is "api revisions listed" 2 "$(sbody $BASE/api/v1/pages/ApiRenamed/revisions |
	grep -o '"stamp"' | wc -l)"
is "api duplicate is 409" 409 "$(scode -X POST $BASE/api/v1/pages \
	-d '{"name":"ApiRenamed","body":"x"}')"
is "api delete"           204 "$(scode -X DELETE $BASE/api/v1/pages/ApiRenamed)"
is "api deleted gone"     404 "$(scode $BASE/api/v1/pages/ApiRenamed)"

echo
echo "== file CRUD =="
TMPF=$(mktemp /tmp/didimdwiki-XXXXXX.txt)
echo "hello from the file store" > "$TMPF"

is "api upload"           201 "$(scode -X POST $BASE/api/v1/files -F "file=@$TMPF")"
UPNAME=$(basename "$TMPF")
has "file listed"         "$UPNAME" "$(sbody $BASE/api/v1/files)"
has "file metadata"       '"owner":"alice"' "$(sbody $BASE/api/v1/files)"
has "file downloads"      "hello from the file store" "$(curl -s $BASE/files/$UPNAME)"
is "api file rename"      200 "$(scode -X PATCH $BASE/api/v1/files/$UPNAME \
	-d '{"name":"renamed.txt"}')"
is "old file gone"        404 "$(code $BASE/files/$UPNAME)"
has "renamed downloads"   "hello from the file store" "$(curl -s $BASE/files/renamed.txt)"
is "api file delete"      204 "$(scode -X DELETE $BASE/api/v1/files/renamed.txt)"
is "deleted file gone"    404 "$(code $BASE/files/renamed.txt)"

echo
echo "== upload hardening =="
cp "$TMPF" /tmp/didimdwiki-evil.html
is "html upload refused"  403 "$(scode -X POST $BASE/api/v1/files \
	-F 'file=@/tmp/didimdwiki-evil.html')"
cp "$TMPF" /tmp/didimdwiki-evil.js
is "js upload refused"    403 "$(scode -X POST $BASE/api/v1/files \
	-F 'file=@/tmp/didimdwiki-evil.js')"
scode -X POST $BASE/api/v1/files \
	-F "file=@$TMPF;filename=../../escaped.txt" >/dev/null
if [ -e "$APP_DIR/../escaped.txt" ] || [ -e "$APP_DIR/escaped.txt" ]; then
	no "traversal filename contained"
else
	ok "traversal filename contained"
fi
rm -f /tmp/didimdwiki-evil.html /tmp/didimdwiki-evil.js "$TMPF"

echo
echo "== search =="
scode -X POST $BASE/api/v1/pages -d '{"name":"Alpha","body":"unique-token-here"}' >/dev/null
scode -X POST $BASE/api/v1/pages -d '{"name":"Beta","body":"unique-token-here too"}' >/dev/null
scode -X POST $BASE/api/v1/pages -d '{"name":"BetaTwo","body":"nothing special"}' >/dev/null
has "content search"      "Alpha" "$(curl -s "$BASE/search?q=unique-token-here")"
is  "single hit redirects" 303 "$(code "$BASE/search?q=Alpha")"
NAMEHITS=$(curl -s "$BASE/search?q=%2FBeta")
has "name-only finds Beta"    ">Beta<"    "$NAMEHITS"
has "name-only finds BetaTwo" ">BetaTwo<" "$NAMEHITS"
hasnt "text-only skips names" ">BetaTwo<" "$(curl -s "$BASE/search?q=%40unique-token-here")"

echo
echo "== rss =="
RSS=$(sbody $BASE/changes.rss)
has "rss is xml"          "<rss version=\"2.0\">" "$RSS"
has "rss is utf-8"        'encoding="UTF-8"'      "$RSS"
has "rss has items"       "<item>"                "$RSS"
is  "rss needs auth"      403 "$(code $BASE/changes.rss)"

echo
echo "== rename security =="
is "html in newname rejected" 400 \
	"$(scode -X POST $BASE/wiki/WikiHome \
	   --data-urlencode "csrf=$CSRF" \
	   -d 'action=rename' \
	   --data-urlencode 'newname=<script>alert(1)</script>')"
is "quote in newname rejected" 400 \
	"$(scode -X POST $BASE/wiki/WikiHome \
	   --data-urlencode "csrf=$CSRF" \
	   -d 'action=rename' \
	   --data-urlencode 'newname=foo" onclick="evil')"
is "crlf in newname rejected" 400 \
	"$(scode -X POST $BASE/wiki/WikiHome \
	   --data-urlencode "csrf=$CSRF" \
	   -d 'action=rename' \
	   --data-urlencode $'newname=foo\r\nX-Injected: yes')"
is "traversal in newname rejected" 400 \
	"$(scode -X POST $BASE/wiki/WikiHome \
	   --data-urlencode "csrf=$CSRF" \
	   -d 'action=rename' \
	   --data-urlencode 'newname=../../etc/passwd')"
is "dotdot newname rejected" 400 \
	"$(scode -X POST $BASE/wiki/WikiHome \
	   --data-urlencode "csrf=$CSRF" \
	   -d 'action=rename' \
	   --data-urlencode 'newname=..')"
is "leading dash rejected" 400 \
	"$(scode -X POST $BASE/wiki/WikiHome \
	   --data-urlencode "csrf=$CSRF" \
	   -d 'action=rename' \
	   --data-urlencode 'newname=-evil')"
has "wikihome untouched" "Edited by alice" \
	"$(curl -s $BASE/api/v1/pages/WikiHome)"

echo
echo "== logout =="
is "logout"               303 "$(scode $BASE/logout)"
is "account after logout" 303 "$(scode $BASE/account)"

echo
echo "== portal: OFF (sin login_required, usuario anónimo puede leer) =="
is "portal OFF: anon home ok"  200 "$(code $BASE/)"
is "portal OFF: anon wiki ok"  200 "$(code $BASE/wiki/WikiHome)"
is "portal OFF: anon index ok" 200 "$(code $BASE/index)"

echo
echo "==> reiniciando servidor con login_required=1 (conservando datos de alice)"
./test/e2e/server.sh restart --login-required >/dev/null || { echo "server restart failed"; exit 1; }

echo
echo "== portal: ON, anónimo bloqueado =="
is "portal ON: anon home bloqueado"   303 "$(code $BASE/)"
is "portal ON: anon wiki bloqueado"   303 "$(code $BASE/wiki/WikiHome)"
is "portal ON: anon index bloqueado"  303 "$(code $BASE/index)"
is "portal ON: anon files bloqueado"  303 "$(code $BASE/files)"
is "portal ON: anon search bloqueado" 303 "$(code "$BASE/search?q=test")"

echo
echo "== portal: ON, usuario autenticado puede leer =="
PORTAL_JAR=$(mktemp)
is "portal ON: login alice" 303 "$(curl -s -b "$PORTAL_JAR" -c "$PORTAL_JAR" \
    -o /dev/null -w '%{http_code}' -X POST $BASE/login \
    -d 'username=alice&password=correcthorse&next=/')"
is "portal ON: authed home ok"   200 "$(curl -s -b "$PORTAL_JAR" -c "$PORTAL_JAR" \
    -o /dev/null -w '%{http_code}' $BASE/)"
is "portal ON: authed wiki ok"   200 "$(curl -s -b "$PORTAL_JAR" -c "$PORTAL_JAR" \
    -o /dev/null -w '%{http_code}' $BASE/wiki/WikiHome)"
is "portal ON: authed index ok"  200 "$(curl -s -b "$PORTAL_JAR" -c "$PORTAL_JAR" \
    -o /dev/null -w '%{http_code}' $BASE/index)"
rm -f "$PORTAL_JAR"

echo
echo "== process health =="
# Syscall filtering is applied by the systemd unit, not in-process, so what
# there is to check here is that nothing crashed or fatal'd during the run.
if grep -qiE "fatal|segmentation fault|killed by signal|assertion" data/didimdwiki.test.log; then
	no "no crashes or fatal errors in the log"
	grep -iE "fatal|segmentation fault|killed by signal|assertion" \
	    data/didimdwiki.test.log | head -5
else
	ok "no crashes or fatal errors in the log"
fi

echo
echo "-------------------------------------------"
printf 'passed: %d   failed: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
