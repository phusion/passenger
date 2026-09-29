# Apache WebSocket build and test environment

Builds Passenger's Apache module in a Rocky Linux 10 container and exercises
its protocol upgrade support. The container exists because the Apache module
needs a recent Apache and its development headers, which a macOS workstation
generally does not have.

`dev/ci/run-tests-with-docker` already runs this suite in Docker, but it
refuses to start on anything other than Linux, and it is wired to the CI
image and cache layout. This environment is the equivalent for a workstation.

Everything runs from the host:

```bash
# The protocol upgrade examples from the Apache integration suite.
dev/e2e/apache-websocket/run

# A real WebSocket client against a real echo application behind Apache.
DEMO=1 dev/e2e/apache-websocket/run

# The whole Apache integration suite.
ALL=1 dev/e2e/apache-websocket/run

# A single example.
dev/e2e/apache-websocket/run -e 'carries data in both directions'

# A shell in the prepared container, after the build.
SHELL_ONLY=1 dev/e2e/apache-websocket/run
```

The first run takes several minutes: it installs the distribution packages,
resolves the bundle and compiles Passenger from scratch. Later runs reuse all
three and only rebuild what changed. To reclaim the space:

```bash
docker volume rm passenger-e2e-apache-websocket-{buildout,bundle,node-modules}
```

## How it fits together

`run` builds the image and starts the container with the source tree bind
mounted at `/passenger`. Build products, gems and `node_modules` live in
Docker volumes mounted over the corresponding paths, so a container build and
a build on the host cannot overwrite each other's binaries, and the tracked
lockfiles stay untouched. `container-entrypoint` prepares the environment as
root and hands over to `container-build-and-test` as an unprivileged user,
which installs dependencies, runs `rake apache2` and then rspec or the demo.

`test/config.json` is written on first use if it does not exist, and removed
again afterwards. It names accounts that exist on Rocky Linux but not on
macOS, so a copy left behind in the source tree would break a later test run
on the host.

## The WebSocket demo

`websocket-demo/` is a WebSocket echo server written against nothing but the
Ruby standard library, plus a client for it. Both implement just enough of
RFC 6455 to send and receive whole unfragmented messages.
`run-websocket-demo` puts Apache in front of the server using
`websocket-demo/httpd.conf.template` — an ordinary `mod_passenger`
configuration, with no proxying and no second application server — and then
runs the client against it.

The integration suite already covers the handshake and a byte round trip. The
demo adds what it leaves out: real frames, a message large enough to need the
64-bit length encoding, and an idle period longer than the configured
`RequestReadTimeout`. It is the check to run when you want to convince
yourself a real client works, rather than that the tunnel moves bytes.

It runs everything twice, once over plain HTTP and once over TLS against a
self-signed certificate generated per run. The TLS pass is the more
interesting of the two: the tunnel goes through Apache's connection filter
chain rather than the socket specifically so that mod_ssl keeps working, and
mod_ssl hands over decrypted bytes a TLS record at a time in a way `poll()`
cannot see. The large message is what exercises that.

## Limitations

Each open WebSocket occupies one Apache worker thread for as long as it lives,
so `MaxRequestWorkers` bounds how many can be open at once. Nothing here tests
that boundary, or what happens under many concurrent connections.
