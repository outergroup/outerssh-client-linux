# OuterSSH client starter for Linux

A minimal GTK 4 + WebKitGTK browser built on [OuterSSH](https://outerssh.org).
It connects to one SSH server, opens the server's homepage (Outer Shell by
default), and has a box for opening any port on the server.

Start here to build your own SSH-aware browser on Linux. It's deliberately small:
the configuration is code, and there are no settings, tabs, or bookmarks.

## What it does

1. Connects to the server in `src/config.h` with OuterSSH, and starts OuterSSH's
   HTTP proxy on a loopback port. The web view uses that proxy.
2. Opens the server's homepage, if the server has one configured (see
   [Server-defined homepage](https://github.com/outergroup/OuterSSH/blob/main/OuterSSH/README.md#server-defined-homepage)).
   Otherwise it opens Outer Shell, if it's running on the server.
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

Build OuterSSH's server-side helpers, which the app uploads to the server when it
first opens a Unix socket such as Outer Shell's. This needs Docker:

```sh
OuterSSH/Scripts/build_outerssh_linux_helpers.sh helpers
```

Edit `src/config.h` with your server, user, and private key (or password). Then:

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/outerssh-client
```

The first time it connects, the app prints the server's host key fingerprint.
Paste it into `TRUSTED_HOST_KEY` so later connections only accept that key.

## Where to look

Everything is in `src/main.c`:

- `connect_to_server` creates the OuterSSH session pool and HTTP proxy.
- `handle_event` answers OuterSSH's events and questions, on the GLib main loop.
- `go_home`, `homepage_received`, `outer_shell_found` and
  `proxy_url_for_homepage` choose the first page.
- `provide_helper` answers OuterSSH's request for its server-side helper.
- `authenticate_proxy` gives WebKit the proxy's credentials.
- `create_content_manager` adds OuterSSH's script that shows Unix socket paths in
  place of their encoded hostnames.

## Limitations

- Opens Outer Shell only on Linux servers. The app bundles only the Linux helpers;
  macOS and Windows servers need their own helper builds for Outer Shell's launchd
  socket or named pipe.
- Root-owned sockets need `sudo`, which the app declines.
- Supports `http+unix://` and `http://localhost` homepages only.
