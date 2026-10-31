#!/bin/sh
# Installs or updates a Novgorod board with PostgreSQL storage behind an
# existing Apache, at https://<your host>/board/. Ubuntu or Debian; run it
# as the user who owns the USSR checkout (it asks sudo for the system parts):
#
#     sh src/novgorod/deploy/install.sh
#
# Safe to run again: it pulls the branch, rebuilds, keeps the database and
# its password, and restarts the service.
#
# Settings (environment variables):
#   BRANCH=main             git branch to run ("" leaves the checkout alone)
#   PREFIX=/board           the path on the web server
#   PORT=8480               local port Novgorod listens on (127.0.0.1 only)
#   DATA=$HOME/novgorod-data   boards.txt and images
#   DB_NAME=novgorod DB_USER=novgorod
#   SKIP_APACHE=1           leave Apache alone (configure it yourself)
#   NO_SYSTEMD=1            do not install the service (containers, tests)
set -eu

REPO=$(cd "$(dirname "$0")/../../.." && pwd)
BRANCH=${BRANCH-main}
PREFIX=${PREFIX:-/board}
PORT=${PORT:-8480}
DATA=${DATA:-$HOME/novgorod-data}
DB_NAME=${DB_NAME:-novgorod}
DB_USER=${DB_USER:-novgorod}
RUN_USER=$(id -un)
CONF_DIR=$HOME/.novgorod
ENV_FILE=$CONF_DIR/db.env

say() { printf '\n== %s\n' "$*"; }
fail() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

case "$PREFIX" in
    /*/) fail "PREFIX must not end with / (e.g. /board)" ;;
    /?*) ;;
    *) fail "PREFIX must start with / (e.g. /board)" ;;
esac
[ "$(id -u)" -ne 0 ] || fail "run this as the user who owns $REPO, not as root"
[ -f "$REPO/src/novgorod/novgorod.su" ] || fail "no Novgorod in $REPO"

say "packages"
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    build-essential flex bison pkg-config libssl-dev ca-certificates \
    postgresql postgresql-client curl git >/dev/null

say "source ($REPO)"
cd "$REPO"
if [ -n "$BRANCH" ] && [ -d .git ]; then
    git fetch --quiet origin "$BRANCH"
    if [ "$(git rev-parse --abbrev-ref HEAD)" != "$BRANCH" ]; then
        git checkout --quiet "$BRANCH" 2>/dev/null ||
            git checkout --quiet -b "$BRANCH" "origin/$BRANCH"
    fi
    git merge --quiet --ff-only "origin/$BRANCH"
    git log --oneline -1
fi

say "build"
make -f ussr-ubuntu.mk -j"$(nproc)" RYAZAN_REQUIRE_TLS=1 ussr >/dev/null
USSR_KIEV= ./ussr -e 'tls_backend(b): print(_): b'
USSR_KIEV= ./ussr tests/dubna_test.su | grep -c '^PASS' | sed 's/$/ dubna checks passed/'
if USSR_KIEV= ./ussr tests/dubna_test.su | grep -q '^FAIL'; then
    USSR_KIEV= ./ussr tests/dubna_test.su | grep '^FAIL'
    fail "Dubna's tests failed on this build"
fi
USSR_KIEV= ./ussr tests/novgorod_test.su | grep -c '^PASS' | sed 's/$/ novgorod checks passed/'
if USSR_KIEV= ./ussr tests/novgorod_test.su | grep -q '^FAIL'; then
    USSR_KIEV= ./ussr tests/novgorod_test.su | grep '^FAIL'
    fail "Novgorod's tests failed on this build"
fi

say "PostgreSQL"
if ! sudo -u postgres psql -qtAc 'select 1' >/dev/null 2>&1; then
    # containers have no init system: start the cluster by hand
    sudo pg_ctlcluster "$(ls /etc/postgresql | sort -V | tail -1)" main start
fi
mkdir -p "$CONF_DIR"
chmod 700 "$CONF_DIR"
if [ ! -f "$ENV_FILE" ]; then
    DB_PASSWORD=$(head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n')
    umask 077
    printf 'PGPASSWORD=%s\n' "$DB_PASSWORD" > "$ENV_FILE"
fi
DB_PASSWORD=$(sed -n 's/^PGPASSWORD=//p' "$ENV_FILE")
if sudo -u postgres psql -qtAc "select 1 from pg_roles where rolname = '$DB_USER'" | grep -q 1; then
    sudo -u postgres psql -q -c "alter role $DB_USER with login password '$DB_PASSWORD'"
else
    sudo -u postgres psql -q -c "create role $DB_USER with login password '$DB_PASSWORD'"
fi
if ! sudo -u postgres psql -qtAc "select 1 from pg_database where datname = '$DB_NAME'" | grep -q 1; then
    # the owner may create tables in public (PostgreSQL 15+ grants that
    # to the database owner only)
    sudo -u postgres createdb -O "$DB_USER" "$DB_NAME"
fi
CONNINFO="host=127.0.0.1 port=5432 dbname=$DB_NAME user=$DB_USER sslmode=disable"
PGPASSWORD=$DB_PASSWORD psql -h 127.0.0.1 -U "$DB_USER" -d "$DB_NAME" -qtAc 'select current_user' >/dev/null ||
    fail "cannot log in to PostgreSQL as $DB_USER over 127.0.0.1 (check pg_hba.conf)"
echo "database $DB_NAME, role $DB_USER, password in $ENV_FILE"

mkdir -p "$DATA"
COMMAND="$REPO/ussr src/novgorod/novgorod.su --http --host 127.0.0.1 --port $PORT --prefix $PREFIX --data $DATA --db"

if [ "${NO_SYSTEMD:-}" != 1 ]; then
    say "service novgorod"
    sed -e "s#@USER@#$RUN_USER#g" -e "s#@REPO@#$REPO#g" -e "s#@ENV_FILE@#$ENV_FILE#g" \
        -e "s#@COMMAND@#$COMMAND#g" -e "s#@CONNINFO@#$CONNINFO#g" \
        "$REPO/src/novgorod/deploy/novgorod.service" | sudo tee /etc/systemd/system/novgorod.service >/dev/null
    sudo systemctl daemon-reload
    sudo systemctl enable --quiet novgorod
    sudo systemctl restart novgorod
    sleep 2
    sudo systemctl --no-pager --lines=5 status novgorod || fail "the service did not start (journalctl -u novgorod)"
else
    say "starting Novgorod in the background (NO_SYSTEMD=1)"
    set -a; . "$ENV_FILE"; set +a
    USSR_KIEV= nohup $COMMAND "$CONNINFO" > "$DATA/novgorod.log" 2>&1 &
    sleep 2
fi
curl -fsS -o /dev/null "http://127.0.0.1:$PORT/" || fail "Novgorod does not answer on 127.0.0.1:$PORT"
echo "Novgorod answers on 127.0.0.1:$PORT"

if [ "${SKIP_APACHE:-}" != 1 ]; then
    say "Apache"
    command -v apache2ctl >/dev/null || fail "Apache is not installed (SKIP_APACHE=1 to skip)"
    sudo a2enmod -q proxy proxy_http >/dev/null
    sed -e "s#@PREFIX@#$PREFIX#g" -e "s#@PORT@#$PORT#g" \
        "$REPO/src/novgorod/deploy/apache-novgorod.conf" | sudo tee /etc/apache2/conf-available/novgorod.conf >/dev/null
    sudo a2enconf -q novgorod >/dev/null
    # a catch-all ProxyPass / in a site would swallow the board's path
    if grep -rEn '^[[:space:]]*ProxyPass[[:space:]]+/[[:space:]]' /etc/apache2/sites-enabled/ 2>/dev/null; then
        echo "note: a site above proxies all of / elsewhere. Put"
        echo "      ProxyPass $PREFIX/ !"
        echo "      before that line, or the board will not be reached."
    fi
    sudo apache2ctl configtest
    if command -v systemctl >/dev/null && [ "${NO_SYSTEMD:-}" != 1 ]; then
        sudo systemctl reload apache2
    else
        sudo apache2ctl graceful 2>/dev/null || sudo apache2ctl start
    fi
    sleep 1
    curl -fsS -o /dev/null "http://127.0.0.1$PREFIX/" -H "Host: localhost" &&
        echo "Apache passes $PREFIX/ to Novgorod"
fi

say "done"
echo "Board: https://<your host>$PREFIX/"
echo "Logs:  journalctl -u novgorod -f"
echo "Update later: sh $REPO/src/novgorod/deploy/install.sh"
