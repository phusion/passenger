# Protocol upgrades in the Apache module

A client can ask to switch a connection to a different protocol with the
`Upgrade` header (RFC 9110 section 7.8). A WebSocket handshake is by far the
most common case. If the application agrees it answers `101 Switching
Protocols`, and from that point the connection no longer carries HTTP
messages: it carries an opaque byte stream, in both directions at once, that
neither Apache nor Passenger interprets.

Passenger's core supports this — it is what makes WebSockets work under Nginx
and Standalone. The Apache module could not, because a request handler in
Apache reads the request, then writes the response, and never both at the
same time. So the Apache module handles upgraded connections by stepping
outside Apache's request machinery and taking over the connection.

All of it lives in `src/apache2_module/Hooks.cpp`, in the methods grouped
under the "Protocol upgrades" comment.

## What normally happens, and why it does not work here

`Hooks::handleRequest()` forwards a request to the Passenger core by reading
the request body from Apache, writing the request and body to the core,
reading the response, and passing it to Apache's output filters as a bucket
brigade — strictly in that order.

Two things then go wrong for an upgraded connection:

- The response travels through the *request-level* output filters. Those
  filters exist to frame an HTTP message body, so they will chunk the stream
  or buffer it to compute a `Content-Length`. Either corrupts it.
- Nothing ever reads from the client again after the request has been
  forwarded. The application's output may reach the client, but nothing the
  client sends afterwards reaches the application, so the connection appears
  to freeze.

## How the upgrade path works instead

For a request that asks for an upgrade, and only for such a request,
`handleRequest()` reads the core's response header block off the socket itself
before building the bucket brigade (`readResponseHeaderBlock()`). It has to:
the choice between the two response paths depends on the status code, and the
status code is only visible once the header block has been read.

If the status is not 101 the application declined the upgrade, and the bytes
that were read are prepended to the bucket brigade as an ordinary bucket. The
normal response path then proceeds unchanged. The cost of that read-ahead is
that any request carrying `Upgrade` — including ones nobody meant as an
upgrade, such as `Upgrade: h2c` — waits for its whole response header block
before a byte reaches the client. It is bounded at 128 KB.

If the status is 101, `tunnelUpgradedConnection()` takes over:

1. The response headers are copied into `r->headers_out` and written out with
   `ap_send_interim_response()`. That function writes directly to the
   connection's output filters, bypassing the HTTP header filter, which is
   what we want: the 101 is the last thing on this connection that is HTTP.
   `Content-Length` and `Transfer-Encoding` are dropped on the way, because
   what follows the 101 is not a message body.
2. The request's filter chains are replaced by the connection's
   (`r->output_filters = c->output_filters` and friends). This is what keeps
   the HTTP protocol filters away from the tunneled bytes. It also means the
   request finalisation that Apache performs after the handler returns has
   nothing left to write.
3. `c->keepalive` is set to `AP_CONN_CLOSE`. This does two jobs: Apache must
   not look for another request on this connection, and — less obviously —
   `ap_discard_request_body()`, which `ap_finalize_request_protocol()` calls
   after the handler returns, skips its blocking read only when the
   connection is marked for closing. Without it the worker would sit in a
   read on a finished connection until Apache's `Timeout` expired.
4. The `reqtimeout` input filter is removed, before the filter chains are
   swapped, because removing a connection filter only updates the
   connection's list. In practice mod_reqtimeout is inert here — it arms no
   deadline for a request that has no body — but an armed deadline would have
   no meaning once the request is over, and mod_proxy_wstunnel removes it for
   the same reason.
5. `pumpUpgradedConnection()` shuttles bytes until one side goes away.

The handler then returns `OK`. It must not return an error status after this
point: the 101 has gone out and the HTTP filters are gone with it, so there is
no way left to report an error to the client. Anything that goes wrong in the
tunnel is logged and ends the connection.

### Why client I/O goes through the filter chain

The pump polls two file descriptors: the raw client socket
(`ap_get_conn_socket()`) and the socket to the Passenger core. But it only
*polls* the client socket. The reading and writing go through
`c->input_filters` and `c->output_filters`, so that mod_ssl still decrypts and
encrypts, and so that anything else on the connection chain keeps working.

That split has one consequence worth knowing: mod_ssl decrypts a whole TLS
record at a time and buffers what the caller did not take, and `poll()` cannot
see that buffer. A readable notification is therefore not the only moment at
which client data may be waiting, and the same is true of anything the client
pipelined behind the handshake.

The pump handles this without draining the client in a loop. Each iteration
moves at most one read in each direction, and whenever a client read produced
data the next `poll()` is given a zero timeout, so buffered bytes are still
picked up promptly without either direction postponing the other.

The reason for that shape rather than a simple drain is fairness: a loop on
one side would keep the other waiting for as long as its peer kept talking.

Interleaving alone does not make the pump safe, though, because both writes
block, and a thread inside a write is a thread no poll timeout can reach.

`writeToClient()` is bounded already: it blocks in `ap_pass_brigade()` on the
connection filter chain, which is subject to Apache's `Timeout` and marks the
connection aborted when that expires. `writeToCore()` therefore takes an
explicit timeout too — `PassengerUpgradeIdleTimeout` when it is set, Apache's
`Timeout` otherwise — so that the tunnel has no unbounded blocking call left
in it. mod_proxy_wstunnel relies on socket timeouts in the same way.

The specific thing that bound guards against is a cycle in the backpressure:
the core stops reading from the tunnel once the application stops consuming
(`processClientDataWhenUpgraded()` in `ServerKit/HttpServer.h`), and the
application stops consuming once the core throttles it for buffering more
than `PassengerResponseBufferHighWatermark` of response
(`maybeThrottleAppSource()` in `Controller/ForwardResponse.cpp`). Only more
reading on the tunnel's side can break that, which is exactly what a thread
blocked in the write is not doing.

That cycle has not been reproduced. Every attempt — a synchronous echo
application, a lowered `PassengerResponseBufferHighWatermark`, a client that
stops reading entirely — ended with `writeToClient()` hitting its own bound
first, because the congestion that stops the core from accepting also stops
the client from accepting. So treat the timeout as insurance against an
unbounded syscall rather than as the fix for an observed hang. Giving the
core socket non-blocking writes with a pending-write buffer and `POLLOUT`
would break the cycle rather than time out of it, and is the shape to reach
for if this ever proves insufficient.

### Half close

When the client closes its sending side, the pump shuts down the writing side
of the socket to the core and stops reading the client, but keeps forwarding
what the application still has to say. When the core closes, the tunnel is
over. WebSocket clients do not normally half close — they exchange close
frames in band — but other upgraded protocols do.

When the core closes, the tunnel ends outright rather than half closing
towards the client. An application that shuts down only its write side while
still wanting to read therefore loses whatever the client sends afterwards.
mod_proxy_wstunnel behaves the same way, and no upgraded protocol in
practical use needs the distinction.

The client's descriptor stays in the poll set after a half close, with no
requested events, which catches a reset: POSIX reports `POLLERR` regardless
of what was asked for. It does **not** catch an ordinary close. Linux raises
`POLLHUP` only once both directions are shut down, and the pump never shuts
down its own write side of the client socket; macOS builds `poll()` on kqueue
and registers no filter at all for a descriptor requesting no events, so it
reports nothing. After a FIN there is no further TCP signal to wait for in
any case.

That means a half-closed client whose application then goes quiet can only be
reaped by a timeout. The pump uses `PassengerUpgradeIdleTimeout` if it is
set, and falls back to Apache's `Timeout` if it is not, rather than waiting
forever in that state.

## Configuration

`PassengerAllowUpgrade` (default on) turns the tunnel off per location.
Switching it off restores the old behaviour, which is to say a WebSocket
handshake that does not work; it exists as an escape hatch, not as a
supported mode.

`PassengerUpgradeIdleTimeout` (default 60 seconds; 0 means never) closes a
tunnel that has seen no traffic in either direction for that many seconds.
Without it a client that disappears without TCP noticing keeps a worker
occupied indefinitely. The default matches what Nginx
(`proxy_read_timeout`) and mod_proxy_wstunnel already do. Applications that
send periodic pings — ActionCable does, every three seconds — never come
near it; one that can be silent for longer needs the value raised.

## Costs and limits

**One Apache worker per open connection.** A tunneled connection occupies the
worker thread — or, under the prefork MPM, the whole process — for its entire
lifetime. This is inherent to handling the connection inside a request
handler; Apache's own mod_proxy_wstunnel has the same property, and not even
the event MPM can release the thread. `MaxRequestWorkers` is therefore the
ceiling on concurrently open WebSockets, and it has to be sized for the
expected number of connections rather than for the request rate. Nginx and
Standalone do not tie a connection to a worker thread this way.

**Setting `PassengerUpgradeIdleTimeout 0` removes the only bound on an idle
tunnel.** The two other ways a worker could be held indefinitely — a stalled
write to the core, and an abandoned half-closed client — fall back to
Apache's `Timeout` regardless. A merely idle connection does not: with the
timeout switched off the pump waits in `poll()` for as long as the peer keeps
the socket open, including a peer that has gone away without TCP noticing.

**HTTP/2 clients do not get a tunnel.** Over HTTP/2 a request lives on a
secondary connection that mod_http2 multiplexes, so there is no client socket
to poll, and the upgrade path declines such requests. This matters because
mod_http2 has supported WebSockets over HTTP/2 since httpd 2.4.55, through
RFC 8441 extended CONNECT and the `H2WebSockets` directive. That directive is
off by default, and with it off a browser negotiates WebSockets over
HTTP/1.1, which works. Turning it on makes the handshake arrive over HTTP/2
instead, where it will not.

**HTTP/1.0 clients do not get a tunnel** either. HTTP/1.0 has no upgrade
mechanism, and Apache refuses to write an interim response to such a client.

**Apache 2.2 does not get a tunnel.** The implementation needs
`ap_get_conn_socket()` and `ap_remove_input_filter_byhandle()`, both of which
arrived in 2.4, so it is compiled out below that. Older versions behave as
they did before: the upgrade request is forwarded and the connection is not
taken over.

## Testing

The Apache integration suite covers the upgrade path under "protocol
upgrades" in `test/integration_tests/apache2_tests.rb`. It drives the
`/switch_protocol` and `/websocket_handshake` endpoints of the Rack stub
application, which perform a Rack full hijack — the same mechanism ActionCable
and other real WebSocket stacks use.

Coverage is Rack-only. The upgrade path is in the Apache module and knows
nothing about the application's language, so duplicating the endpoints into
the Python and Node stubs would exercise the same code twice.

The parsing helpers (`findHeaderBlockEnd()`, `parseResponseStatusCode()`,
`applyUpgradeResponseHeaders()`) have no unit tests: they are private members
in `Hooks.cpp`, and `test/cxx` neither builds against the Apache headers nor
has any Apache module tests to extend. They are covered indirectly by the
integration examples.

Note that an application that switches protocols over a hijacked connection
must write a real status line. A CGI-style `Status: 101 Switching Protocols`
header is not enough for the core to recognise the switch, and the request
fails with a 502 instead.

`dev/e2e/apache-websocket/` builds the module and runs these tests in a
container, and additionally offers a demo that drives a real WebSocket client
against a real echo application, over plain HTTP and over TLS. The TLS pass is
the only coverage of the mod_ssl path, which is the whole reason client I/O
goes through the filter chain; the integration suite is plaintext only. See
its README.
