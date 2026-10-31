# SERVER_SETUP.md — setting up online.swos.eu from scratch

SWOS United's online server: matchmaking, the relay (browsers and players whose routers will
not connect them directly), the match recorder and the team check, all in one program,
`swos-matchmaker`, on **UDP and TCP 19602**. nginx beside it serves a small admin page, the
recordings and the match reports on **443**. The game finds it by the name in
`include/project_identity.h` (`PROJECT_MM_HOST`, `online.swos.eu`).

This is the whole setup for a fresh Debian 12 / Ubuntu 22.04+ machine, in order. Every step
says how to see that it worked. The full reference behind it is
[NETPLAY_SETUP.md](NETPLAY_SETUP.md) (sections are named below); the admin page's own README is
`tools/matchmaker-admin/README.md`.

What runs where, when it is done:

| | |
|---|---|
| `/home/swos/SWOS-United` | the checkout, owned by the user `swos`; built here |
| `/srv/swos-matchmaker` | the running server: `bin/`, `tls/`, `recordings/`, `reports/`, `matches.jsonl` |
| `swos-matchmaker` (systemd) | the server, as `swos` |
| `swos-matchmaker-admin` (systemd) | the admin page's gateway, as `swos`, on 127.0.0.1:8899 only |
| root's crontab | the worker that runs the admin page's install / restart buttons (they need root) |
| nginx | `https://online.swos.eu/cc/` (admin page), `/recordings/`, `/reports/` -- all behind one login |

## 1. DNS

An **A record** `online.swos.eu` -> the machine's public IPv4 address (and AAAA only if the
machine really answers on IPv6).

    dig +short online.swos.eu          # prints the machine's address

## 2. Ports

Open these to the internet -- on the machine's firewall AND any firewall the hosting provider
puts in front of it (a cloud "security group" or control-panel firewall is a separate layer):

| port | for |
|---|---|
| **UDP 19602** | the game: matchmaking, relay |
| **TCP 19602** | browsers (WebSocket) and match recording |
| TCP 80 | Let's Encrypt (getting and renewing the certificate) and the redirect to https |
| TCP 443 | the admin page, recordings, reports |
| TCP 22 | ssh (already) |

    sudo ufw allow 19602/udp && sudo ufw allow 19602/tcp && sudo ufw allow 80/tcp && sudo ufw allow 443/tcp

## 3. Packages and the user

    sudo apt update
    sudo apt install -y git cmake g++ libssl-dev python3 nginx certbot python3-certbot-nginx apache2-utils curl
    sudo adduser --disabled-password --gecos "" swos

`libssl-dev` must be there BEFORE the first build: it is what lets browsers connect over
`wss://` (a build without it serves only `ws://`, and needs `--rebuild` after installing it).

## 4. The checkout

The repository is public, so plain HTTPS needs no key and cannot push -- read-only by nature:

    sudo -u swos git clone https://github.com/SWOS-United/SWOS-United.git /home/swos/SWOS-United

(Or with the read-only deploy key "online.swos.eu" -- already added to the repository: put the
key in `/home/swos/.ssh/`, the `Host github-swos-united` alias in `/home/swos/.ssh/config`, and
clone `git@github-swos-united:SWOS-United/SWOS-United.git`.)

**Do not initialise the submodules** (no `--recursive`, no `git submodule update --init`): the
server needs none of them, and `third_party/swos-api` is private.

## 5. Build the server

    sudo -u swos /home/swos/SWOS-United/scripts/run_matchmaker.sh --build-only

It prints `-- swos-matchmaker: wss:// with OpenSSL 3.x` while configuring and leaves
`/home/swos/SWOS-United/build/matchmaker/swos-matchmaker`. (NETPLAY_SETUP.md §3.)

## 6. The certificate

Let's Encrypt checks the name through nginx on port 80, so nginx first gets a bare site for
the name, then certbot, then the real site:

    sudo tee /etc/nginx/sites-available/online.swos.eu >/dev/null <<'EOF'
    server { listen 80; listen [::]:80; server_name online.swos.eu; }
    EOF
    sudo ln -sf /etc/nginx/sites-available/online.swos.eu /etc/nginx/sites-enabled/online.swos.eu
    sudo nginx -t && sudo systemctl reload nginx
    sudo certbot certonly --nginx -d online.swos.eu --agree-tos -m YOUR@EMAIL --no-eff-email

It ends with the certificate in `/etc/letsencrypt/live/online.swos.eu/`. Renewal is automatic
(`sudo certbot renew --dry-run` proves it will work).

**The server reads its own copy** (it runs as `swos`, and `/etc/letsencrypt` is root's): a
certbot deploy hook copies the pair over on every renewal, and the server picks a new one up
within a minute, no restart. (NETPLAY_SETUP.md §10.)

    sudo mkdir -p /srv/swos-matchmaker/tls
    sudo tee /etc/letsencrypt/renewal-hooks/deploy/swos-matchmaker.sh >/dev/null <<'EOF'
    #!/bin/sh
    # Copy the renewed certificate where swos-matchmaker (user swos) reads it.
    set -e
    [ "$RENEWED_LINEAGE" = /etc/letsencrypt/live/online.swos.eu ] || exit 0
    install -o swos -g swos -m 0644 "$RENEWED_LINEAGE/fullchain.pem" /srv/swos-matchmaker/tls/fullchain.pem
    install -o swos -g swos -m 0600 "$RENEWED_LINEAGE/privkey.pem"   /srv/swos-matchmaker/tls/privkey.pem
    EOF
    sudo chmod 755 /etc/letsencrypt/renewal-hooks/deploy/swos-matchmaker.sh
    sudo RENEWED_LINEAGE=/etc/letsencrypt/live/online.swos.eu /etc/letsencrypt/renewal-hooks/deploy/swos-matchmaker.sh
    ls -l /srv/swos-matchmaker/tls          # fullchain.pem and privkey.pem, owned by swos

## 7. The server as a service

    sudo /home/swos/SWOS-United/scripts/swos-matchmaker-service.sh install
    journalctl -u swos-matchmaker -b --no-pager | head -20

The script makes `/srv/swos-matchmaker`, copies the binary there, writes and starts the
systemd unit (relay, recorder, reports, ledger, `wss://`) and runs it as the owner of the
checkout, `swos`. The journal should say the server is listening on 19602 and

    [web] wss:// on the same port, certificate /srv/swos-matchmaker/tls/fullchain.pem

-- not `[web] TLS: ...` (NETPLAY_SETUP.md §11). The script may warn that the recordings player
is not built: that is expected for now (§11 below).

## 8. The web side and its login

The site, from the checkout, replacing the bare one from §6:

    sudo cp /home/swos/SWOS-United/packaging/server/nginx/online.swos.eu.conf /etc/nginx/sites-available/online.swos.eu
    sudo htpasswd -B -c /etc/nginx/.htpasswd-matchmaker-admin redhair      # asks for a password
    sudo nginx -t && sudo systemctl reload nginx
    curl -s https://online.swos.eu/                                       # "SWOS United online server"

More logins: `sudo htpasswd -B /etc/nginx/.htpasswd-matchmaker-admin <name>` (without `-c`,
which would start the file again).

## 9. The admin page

The gateway, then root's worker for the buttons that need root:

    sudo /home/swos/SWOS-United/tools/matchmaker-admin/matchmaker-admin-service.sh install
    ( sudo crontab -l 2>/dev/null; echo '* * * * * /home/swos/SWOS-United/tools/matchmaker-admin/admin-cron-worker.sh >/dev/null 2>&1' ) | sudo crontab -

Open `https://online.swos.eu/cc/`, log in, press **Service: status**: the output pane shows
`swos-matchmaker` active (running).

## 10. Prove it

From any other computer with a SWOS United checkout, built (Linux or WSL; an OpenSWOS build is
refused by this server -- that is its tag working), two players through the real menus
and the deployed server, peer to peer and relayed, recorded:

    MM_HOST=online.swos.eu scripts/relay_test.sh

It must end `relay-test: PASSED`. (NETPLAY_SETUP.md §6 and §13 have more, including a browser
against the server over `wss://`.) Ben can run it from his machine -- tell him when §9 is done.

## 11. Later: the recordings player

`https://online.swos.eu/playback/` plays any recorded match in a browser. It needs emsdk and
the game's data on the server (NETPLAY_SETUP.md §12, docs/PLAYBACK.md); without them the
admin page's Rebuild simply skips it. Not needed to play.

## Updating it

From the admin page, after a change is pushed to the repository: **git pull**, then
**Rebuild**, then **Service: restart** (a match in progress is cut off by the restart, so pick
a quiet moment). `tools/matchmaker-admin/README.md` and NETPLAY_ADMIN.md say which buttons a
given change needs.
