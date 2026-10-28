# Secure headers

The Nginx and Apache modules forward each request to the Core over a Unix domain socket as an HTTP/1.1 request. Besides the client's own headers, that request carries metadata that the Core must be able to trust: the document root, the client address, the authenticated user name, the app's configuration options (start command, app type, user, environment), and some flags. These travel as *secure headers*: header fields whose name starts with `!~`, such as `!~DOCUMENT_ROOT`.

## How the Core decides what to trust

The modules first write the client's headers, then a `!~: <core password>` line, then the secure headers. The Core password is a random secret that the Watchdog shares with the module, so a client cannot produce that line. `ServerKit::HttpHeaderParser` enforces this layout: a `!~` header before the password line is rejected, a normal header after it is rejected, and every `!~` header after it goes into `req->secureHeaders`. The password line only proves that the module wrote the block. It does not protect how the block is split into header lines.

## Values are encoded

Some secure header values contain data from the client. For example, the document root contains part of the URI when Nginx's `root` uses a variable from a regex capture, and `REMOTE_USER` is whatever name an auth module accepted. Nginx and Apache URL-decode such data, so it can contain CR and LF. Written as-is, a CR/LF ends the value's header line and lets the client add arbitrary secure headers after the password line, up to choosing the app's start command.

So the modules percent-encode every secure header value: `%` and all control characters (bytes 0x00–0x1F and 0x7F) become `%XX`. `HttpHeaderParser` decodes each secure header value before storing it, so code that reads `req->secureHeaders` sees the original value. Decoding is strict, and a malformed `%` escape makes the Core reject the request with 400. The encoder and decoder live together in `src/cxx_supportlib/SecureHeaderEncoding.h`, which is plain C so that the Nginx module can use it.

Ordinary values contain none of the encoded bytes, so encoding normally changes nothing and decoding is skipped. The password line is not encoded. Base64 values such as `PASSENGER_ENV_VARS` are written unencoded because base64 never contains encoded bytes.

When adding a secure header to a module, write its value through the encoding helper: `PUSH_SECURE_VALUE` in `src/nginx_module/ContentHandler.c`, or `addHeader()`/`appendSecureHeaderValue()` in `src/apache2_module/Utils.h`. Configuration options declared in the config option definitions get this automatically through the generated header serialization code.

As an additional safeguard, both modules reject a request with 400 if the document root or user name contains CR or LF, before writing anything to the Core.

## Compatibility

The modules and the Core must come from the same Passenger version. There is no version negotiation. If they differ, a value that contains `%` or a control character is misread: an older module's literal `%` can make a newer Core reject the request or decode the value incorrectly, and an older Core passes a newer module's `%XX` escapes through literally.

Responses use `!~` headers too: an app can send `!~Request-OOB-Work` to the Core. That direction has no password line and no encoding.
