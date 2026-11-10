# The co-op server

Co-op sessions (Phase 9) need a small server that every participant's MICINSTATION
connects to: it puts everyone's edits in one order, holds the locks, passes presence on,
and keeps the session's project and audio files so that each copy can download what it
lacks. Audio is never streamed: each computer plays its own copy of the project.

This guide sets up that server: first on your own Mac (free, for trying and developing),
then on a rented server for playing with friends far away.

## What the server keeps, and what it logs

- **Kept** (in its data folder): for each session its project (as the app shares it:
  without anyone's personal settings), the transactions accepted since it was last
  written, the participants (their names, colours and the SHA-256 of their tokens, never
  the tokens), the invite code, and the audio files the project refers to, by the SHA-256
  of their content. Also its own key and certificate, and the host keys.
- **Deleted**: everything about a session when its host closes it; a session nobody
  joined for 30 days.
- **Logged**: what happens (a session opened, a participant joined or left, a transaction
  refused, a file stored), with random ids, numbers, sizes and reasons. Never names of
  projects, tracks, files or people, never contents, never IP addresses.
- **Limits** (all can be changed, see below): 20 sessions, 8 participants a session, 20
  GB of files a session, 100 GB in all, 4 GB a file, 64 connections; 10 wrong codes or
  keys from one address in 10 minutes block it for 15 minutes.

Connections are TLS 1.3. The server makes its own certificate the first time it starts;
the app checks the fingerprint of its key, which comes with every link, instead of
asking certificate authorities. So the server needs no domain name and no renewals, and
nobody can stand in between. Keep the data folder: a new key means new links.

## Links

- A **host link** (`micinstation://server?...&host=...`) lets someone open sessions on
  the server. Whoever runs the server hands it out, only to friends who may host. Each is
  a host key (see below); it can be revoked.
- An **invite link** (`micinstation://join?...&code=...`) is what the app gives a host
  for each session: it lets friends join that session, for 24 hours (the host can make a
  new one). Paste it into Discord or anywhere.

Both carry the server's address and the fingerprint of its key. Clicking one opens
MICINSTATION with the right window (Join a Session, Host a Session) and the link filled in;
pasting it there works as well.

## 1. On your Mac (local mode)

Build the project as the README says, then start the server from Terminal:

```bash
build/debug/server/micinstation-server --data ~/MICINSTATION-server --public 127.0.0.1:7443
```

The first time it prints a host link: copy it. Leave the window open (Ctrl+C stops the
server). Then in MICINSTATION: Co-op > Host a Session..., paste the host link, choose a
name. The app copies your project into a co-op folder and gives you the invite link.

**Two copies of the app on one Mac**, to try co-op alone: start a second copy with
settings of its own, then join with the invite link:

```bash
open -n build/debug/app/daw_app_artefacts/Debug/MICINSTATION.app --args --settings-dir="$HOME/MICINSTATION-second"
```

**Friends in your home network** can join too: start the server with your Mac's address
in the network (System Settings > Wi-Fi > Details, e.g. 192.168.1.20), and allow incoming
connections if macOS asks:

```bash
build/debug/server/micinstation-server --data ~/MICINSTATION-server --public 192.168.1.20:7443
```

Links made with another address (an earlier `--public`) still name the old one: print
the host links again with `--links` (below).

## 2. Where to run it for friends far away

| Where | Cost | Good | Less good |
|-------|------|------|-----------|
| **Hetzner Cloud CAX11** (default) | about 5-6 EUR a month with an IPv4 address (prices changed in 2026: check at hetzner.com/cloud) | in the EU (Germany or Finland), always on, 40 GB disk, plenty of upload bandwidth, Docker in five minutes | needs an account with a payment method |
| Fly.io (shared-cpu-1x, 256 MB, a volume) | about 2.2 USD for the machine, 2 USD for the dedicated IPv4 its raw TCP needs, 0.15 USD per GB of volume: about 6 USD a month with 10 GB | no server to look after | an app config of its own (fly.toml), the volume to size |
| One of you, at home (the server on that computer, or Docker Desktop) | free | nothing to rent | the computer must stay on; the router must forward a port to it; many providers give no public IPv4 (CGNAT); upload speed limits the files |

The default is the Hetzner server: cheap, always there, in the EU.

### What you do (accounts and access are yours)

1. **Create a Hetzner account** at <https://accounts.hetzner.com/signUp> and verify it (an
   ID or a payment method). Then open the Cloud Console and create a project
   ("MICINSTATION").
2. **An SSH key**, so that only you can log in. On your Mac, in Terminal:
   ```bash
   ssh-keygen -t ed25519 -C "micinstation server"
   cat ~/.ssh/id_ed25519.pub
   ```
   (press Enter at every question to keep the defaults; set a passphrase if you like).
   Copy the line `cat` prints.
3. **Create the server**: Add Server; Location: Falkenstein, Nuremberg or Helsinki;
   Image: Ubuntu 24.04; Type: Shared vCPU, Arm64 (Ampere), **CAX11**; Networking: public
   IPv4 and IPv6 on; SSH keys: Add SSH key, paste the line, select it; Firewalls: Create
   Firewall with two inbound rules, **TCP 22** (SSH) and **TCP 443** (the co-op server),
   from any address; Name: `micinstation`. Create & Buy now. Note the server's IPv4
   address (say `203.0.113.7`).
4. **Log in** from your Mac:
   ```bash
   ssh root@203.0.113.7
   ```
   (answer `yes` the first time).
5. **Install Docker** on the server:
   ```bash
   apt-get update && apt-get install -y docker.io docker-compose-v2
   ```
6. **Start the co-op server** (still on the server):
   ```bash
   mkdir -p /opt/micinstation && cd /opt/micinstation
   curl -fsSLO https://raw.githubusercontent.com/tavanzz/daw/main/server/docker-compose.yml
   echo "MICINSTATION_PUBLIC=203.0.113.7:443" > .env
   docker compose up -d
   docker compose logs
   ```
   The log shows the **host link** the first time: copy it, it is yours (it is also kept
   in the server's data). `docker compose logs` should end with `listening on port 7443`.
7. **Check it from your Mac** (optional), with the project's build:
   ```bash
   build/debug/server/micinstation-server --probe "<the host link>"
   ```
   It answers "The server answered (protocol 1, ...): This server does not accept the host
   key you entered." (the probe uses a wrong key on purpose): the server works and the
   fingerprint matches.
8. **The image must be public** on GitHub the first time it is published (the release
   workflow publishes `ghcr.io/tavanzz/micinstation-server` with each release): on
   GitHub, your profile > Packages > micinstation-server > Package settings > Change
   visibility > Public. (Or log the server in to the registry with a token; public is
   simpler: the image holds nothing secret.)
9. **Hand out the host link** to friends who may open sessions (others only need invite
   links from a host).

### Every day

- **Update** when a new MICINSTATION comes out (the app and the server must speak the same
  network protocol; the app says so if they do not):
  ```bash
  cd /opt/micinstation && docker compose pull && docker compose up -d
  ```
- **Host links**: print them again, add one, or revoke one (then restart):
  ```bash
  docker compose exec micinstation-server micinstation-server --links
  docker compose exec micinstation-server micinstation-server --new-host-key
  docker compose exec micinstation-server micinstation-server --revoke-host-key <first characters>
  docker compose restart
  ```
- **Logs**: `docker compose logs --tail 100` (kept small: 3 files of 10 MB).
- **Backups**: everything is in the Docker volume `micinstation_micinstation-data`; to copy
  it: `docker run --rm -v micinstation_micinstation-data:/data -v "$PWD":/backup ubuntu
  tar czf /backup/micinstation-data.tgz /data`.
- **Stop it**: `docker compose down` (the data stays in the volume).
- **Delete the server** in the Hetzner console when you no longer need it (billing stops).

### Options

Each can be given to the program (or in `.env`, and to `docker compose` via
`command:` in docker-compose.yml):

| Option | Default | |
|--------|---------|---|
| `--data <folder>` (`MICINSTATION_DATA`) | `./micinstation-data` (`/data` in the image) | where everything is kept |
| `--port <port>` (`MICINSTATION_PORT`) | 7443 | the port it listens on (the compose file maps 443 to it) |
| `--bind <address>` | every address | |
| `--public <host[:port]>` (`MICINSTATION_PUBLIC`) | `localhost` and `--port` | the address in links |
| `--max-sessions <n>` | 20 | |
| `--max-participants <n>` | 8 | a session's |
| `--session-gb <n>` | 20 | files a session keeps |
| `--total-gb <n>` | 100 | files of every session |

### Without Docker

The program builds alone, without the app (CMake option `DAW_SERVER_ONLY`; Linux, macOS or
Windows):

```bash
cmake -S . -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release -DDAW_SERVER_ONLY=ON
cmake --build build/server --target daw_server_program
build/server/server/micinstation-server --data /var/lib/micinstation --port 443 --public 203.0.113.7:443
```

(port 443 needs root or `setcap cap_net_bind_service=+ep` on the program; the image avoids
this with its 443-to-7443 mapping).

### Fly.io instead

`fly launch --image ghcr.io/tavanzz/micinstation-server:latest --no-deploy`, then in
`fly.toml`: a `[[services]]` block with `internal_port = 7443`, `protocol = "tcp"` and a
port 443 with no handlers (raw TCP: the server does its own TLS), a `[mounts]` volume at
`/data`, and `MICINSTATION_PUBLIC` set to the dedicated IPv4 address (`fly ips allocate-v4`)
with `:443`. Then `fly deploy` and `fly logs` for the host link.

### At home instead

Run the server on the computer that stays on (the program, or Docker Desktop with the
compose file), give that computer a fixed address in the router, forward TCP port 443 (or
7443) to it, and use your public address in `MICINSTATION_PUBLIC` (a dynamic DNS name if
it changes). If the router's WAN address is not your public one (CGNAT), this cannot work:
use a rented server.
