# Deploying Novgorod behind Apache

`install.sh` puts a Novgorod board with PostgreSQL storage at
`https://<your host>/board/` on an Ubuntu or Debian server that already runs
Apache for other sites. Apache keeps its certificate and its other sites;
Novgorod listens on `127.0.0.1:8480` only, and Apache passes `/board/` to it.

## Run it

As the user who will own the board (not root; it asks sudo for the system
parts):

```bash
cd ~ && { [ -d USSR ] || git clone https://github.com/notlibrary/USSR.git; } && cd USSR && git checkout main && git pull --ff-only && sh src/novgorod/deploy/install.sh
```

It takes a few minutes the first time. What it does:

1. Installs the build tools, OpenSSL headers, PostgreSQL and curl with apt.
2. Updates the checkout to `main` (`BRANCH=...` for another branch, or
   `BRANCH=` to leave it alone), builds `ussr` with TLS and runs Dubna's and
   Novgorod's tests. It stops if any fail.
3. Creates the PostgreSQL role `novgorod` with a random password, kept in
   `~/.novgorod/db.env` (mode 600), and the database `novgorod` that it owns.
   Running the script again keeps both.
4. Installs and starts the systemd service `novgorod`:
   `ussr src/novgorod/novgorod.su --http --host 127.0.0.1 --port 8480
   --prefix /board --data ~/novgorod-data --db "host=127.0.0.1 ..."`, with the
   password passed in the environment rather than on the command line.
5. Enables `mod_proxy` and `mod_proxy_http`, writes
   `/etc/apache2/conf-available/novgorod.conf`, enables it, checks the
   configuration and reloads Apache.

## Settings

Environment variables for `install.sh`: `PREFIX` (`/board`), `PORT`
(`8480`), `DATA` (`~/novgorod-data`), `DB_NAME` and `DB_USER`
(`novgorod`), `BRANCH` (`main`), `SKIP_APACHE=1` (configure Apache
yourself from `apache-novgorod.conf`), `NO_SYSTEMD=1` (start it in the
background instead of as a service).

Boards are in `~/novgorod-data/boards.txt`; edit it and
`sudo systemctl restart novgorod`.

## When it does not come up

- `journalctl -u novgorod -e` shows Novgorod's own messages, such as a
  database error.
- If one of your sites proxies all of `/` to another service
  (`ProxyPass / http://...`), the script prints a note. Add
  `ProxyPass /board/ !` before that line in the site's configuration, so
  `/board/` reaches the board's `<Location>`, then
  `sudo systemctl reload apache2`.
- `curl -i http://127.0.0.1:8480/` asks Novgorod directly;
  `curl -i https://<your host>/board/` goes through Apache.

## Update

Run the same command again. It pulls, rebuilds, tests and restarts. Posts
stay in PostgreSQL and images in `~/novgorod-data/img`.

## Backups

```bash
pg_dump -h 127.0.0.1 -U novgorod novgorod > novgorod.sql    # password: ~/.novgorod/db.env
tar czf novgorod-data.tgz -C ~ novgorod-data
```
