# OuterSSH client starter for Linux

A minimal GTK 4 + WebKitGTK browser built on [OuterSSH](https://outerssh.org).
It connects to one SSH server, opens the server's homepage (such as Outer Shell),
and has a box for opening any port on the server.

Start here to build your own SSH-aware browser on Linux. It's deliberately small:
the configuration is code, and there are no settings, tabs, or bookmarks.

## What it does

1. Connects to the server in `src/config.h` with OuterSSH, and starts OuterSSH's
   HTTP proxy on a loopback port. The web view uses that proxy.
2. Opens the server's homepage, if the server has one configured (see
   [Server-defined homepage](https://github.com/outergroup/OuterSSH/blob/main/OuterSSH/README.md#server-defined-homepage)).
   Installing [Outer Shell](https://outershell.org) on a server makes it that
   server's homepage, on Linux, macOS, and Windows.
3. Typing a port number and pressing Enter opens `localhost:<port>` on the server.
   Pressing Enter in the empty box, or clicking Home, opens the homepage again.

## Setup

```sh
git clone --recurse-submodules <this repository>
cd outerssh-client-linux
```

Install a C17 compiler, CMake 3.24+, Ninja, OpenSSL 3, zlib, GTK 4 (4.10+), and
WebKitGTK 6.0. On Ubuntu 24.04:

```sh
sudo apt install build-essential cmake ninja-build pkg-config patch libssl-dev zlib1g-dev \
  libgtk-4-dev libwebkitgtk-6.0-dev
```

Edit `src/config.h` with your server, user, and private key (or password). Then:

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/outerssh-client
```

The first time it connects, the app prints the server's host key fingerprint.
Paste it into `TRUSTED_HOST_KEY` so later connections only accept that key.

### Server helpers

OuterSSH uploads a small helper to the server when it first needs one, such as to
open a Unix socket like Outer Shell's. Configuring the build downloads signed
helpers for Linux, macOS, and Windows servers from outerssh.org into `helpers/`
(git-ignored), checking each against `helpers.sha256`. To use helpers you build
yourself (see `OuterSSH/Release/README.md`), put them in `helpers/` and their
hashes in `helpers.sha256`; the build only downloads files whose hash doesn't
match.

## Where to look

Everything is in `src/main.c`:

- `connect_to_server` creates the OuterSSH session pool and HTTP proxy.
- `handle_event` answers OuterSSH's events and questions, on the GLib main loop.
- `go_home`, `homepage_received` and `proxy_url_for_homepage` open the server's
  homepage.
- `provide_helper` answers OuterSSH's request for its server-side helper.
- `authenticate_proxy` gives WebKit the proxy's credentials.
- `create_content_manager` adds OuterSSH's script that shows Unix socket paths in
  place of their encoded hostnames.

## Limitations

- Root-owned sockets need `sudo`, which the app declines.
- Supports `http+unix://`, `http+pipe://` and `http://localhost` homepages only.
