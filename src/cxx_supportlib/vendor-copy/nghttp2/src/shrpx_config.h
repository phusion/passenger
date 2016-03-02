/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef SHRPX_CONFIG_H
#define SHRPX_CONFIG_H

#include "shrpx.h"

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif // HAVE_SYS_SOCKET_H
#include <sys/un.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif // HAVE_NETINET_IN_H
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif // HAVE_ARPA_INET_H
#include <cinttypes>
#include <cstdio>
#include <vector>
#include <memory>
#include <set>

#include <openssl/ssl.h>

#include <ev.h>

#include <nghttp2/nghttp2.h>

#include "shrpx_router.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

struct LogFragment;

namespace ssl {

class CertLookupTree;

} // namespace ssl

constexpr char SHRPX_UNIX_PATH_PREFIX[] = "unix:";

constexpr char SHRPX_OPT_PRIVATE_KEY_FILE[] = "private-key-file";
constexpr char SHRPX_OPT_PRIVATE_KEY_PASSWD_FILE[] = "private-key-passwd-file";
constexpr char SHRPX_OPT_CERTIFICATE_FILE[] = "certificate-file";
constexpr char SHRPX_OPT_DH_PARAM_FILE[] = "dh-param-file";
constexpr char SHRPX_OPT_SUBCERT[] = "subcert";
constexpr char SHRPX_OPT_BACKEND[] = "backend";
constexpr char SHRPX_OPT_FRONTEND[] = "frontend";
constexpr char SHRPX_OPT_WORKERS[] = "workers";
constexpr char SHRPX_OPT_HTTP2_MAX_CONCURRENT_STREAMS[] =
    "http2-max-concurrent-streams";
constexpr char SHRPX_OPT_LOG_LEVEL[] = "log-level";
constexpr char SHRPX_OPT_DAEMON[] = "daemon";
constexpr char SHRPX_OPT_HTTP2_PROXY[] = "http2-proxy";
constexpr char SHRPX_OPT_HTTP2_BRIDGE[] = "http2-bridge";
constexpr char SHRPX_OPT_CLIENT_PROXY[] = "client-proxy";
constexpr char SHRPX_OPT_ADD_X_FORWARDED_FOR[] = "add-x-forwarded-for";
constexpr char SHRPX_OPT_STRIP_INCOMING_X_FORWARDED_FOR[] =
    "strip-incoming-x-forwarded-for";
constexpr char SHRPX_OPT_NO_VIA[] = "no-via";
constexpr char SHRPX_OPT_FRONTEND_HTTP2_READ_TIMEOUT[] =
    "frontend-http2-read-timeout";
constexpr char SHRPX_OPT_FRONTEND_READ_TIMEOUT[] = "frontend-read-timeout";
constexpr char SHRPX_OPT_FRONTEND_WRITE_TIMEOUT[] = "frontend-write-timeout";
constexpr char SHRPX_OPT_BACKEND_READ_TIMEOUT[] = "backend-read-timeout";
constexpr char SHRPX_OPT_BACKEND_WRITE_TIMEOUT[] = "backend-write-timeout";
constexpr char SHRPX_OPT_STREAM_READ_TIMEOUT[] = "stream-read-timeout";
constexpr char SHRPX_OPT_STREAM_WRITE_TIMEOUT[] = "stream-write-timeout";
constexpr char SHRPX_OPT_ACCESSLOG_FILE[] = "accesslog-file";
constexpr char SHRPX_OPT_ACCESSLOG_SYSLOG[] = "accesslog-syslog";
constexpr char SHRPX_OPT_ACCESSLOG_FORMAT[] = "accesslog-format";
constexpr char SHRPX_OPT_ERRORLOG_FILE[] = "errorlog-file";
constexpr char SHRPX_OPT_ERRORLOG_SYSLOG[] = "errorlog-syslog";
constexpr char SHRPX_OPT_BACKEND_KEEP_ALIVE_TIMEOUT[] =
    "backend-keep-alive-timeout";
constexpr char SHRPX_OPT_FRONTEND_HTTP2_WINDOW_BITS[] =
    "frontend-http2-window-bits";
constexpr char SHRPX_OPT_BACKEND_HTTP2_WINDOW_BITS[] =
    "backend-http2-window-bits";
constexpr char SHRPX_OPT_FRONTEND_HTTP2_CONNECTION_WINDOW_BITS[] =
    "frontend-http2-connection-window-bits";
constexpr char SHRPX_OPT_BACKEND_HTTP2_CONNECTION_WINDOW_BITS[] =
    "backend-http2-connection-window-bits";
constexpr char SHRPX_OPT_FRONTEND_NO_TLS[] = "frontend-no-tls";
constexpr char SHRPX_OPT_BACKEND_NO_TLS[] = "backend-no-tls";
constexpr char SHRPX_OPT_BACKEND_TLS_SNI_FIELD[] = "backend-tls-sni-field";
constexpr char SHRPX_OPT_PID_FILE[] = "pid-file";
constexpr char SHRPX_OPT_USER[] = "user";
constexpr char SHRPX_OPT_SYSLOG_FACILITY[] = "syslog-facility";
constexpr char SHRPX_OPT_BACKLOG[] = "backlog";
constexpr char SHRPX_OPT_CIPHERS[] = "ciphers";
constexpr char SHRPX_OPT_CLIENT[] = "client";
constexpr char SHRPX_OPT_INSECURE[] = "insecure";
constexpr char SHRPX_OPT_CACERT[] = "cacert";
constexpr char SHRPX_OPT_BACKEND_IPV4[] = "backend-ipv4";
constexpr char SHRPX_OPT_BACKEND_IPV6[] = "backend-ipv6";
constexpr char SHRPX_OPT_BACKEND_HTTP_PROXY_URI[] = "backend-http-proxy-uri";
constexpr char SHRPX_OPT_READ_RATE[] = "read-rate";
constexpr char SHRPX_OPT_READ_BURST[] = "read-burst";
constexpr char SHRPX_OPT_WRITE_RATE[] = "write-rate";
constexpr char SHRPX_OPT_WRITE_BURST[] = "write-burst";
constexpr char SHRPX_OPT_WORKER_READ_RATE[] = "worker-read-rate";
constexpr char SHRPX_OPT_WORKER_READ_BURST[] = "worker-read-burst";
constexpr char SHRPX_OPT_WORKER_WRITE_RATE[] = "worker-write-rate";
constexpr char SHRPX_OPT_WORKER_WRITE_BURST[] = "worker-write-burst";
constexpr char SHRPX_OPT_NPN_LIST[] = "npn-list";
constexpr char SHRPX_OPT_TLS_PROTO_LIST[] = "tls-proto-list";
constexpr char SHRPX_OPT_VERIFY_CLIENT[] = "verify-client";
constexpr char SHRPX_OPT_VERIFY_CLIENT_CACERT[] = "verify-client-cacert";
constexpr char SHRPX_OPT_CLIENT_PRIVATE_KEY_FILE[] = "client-private-key-file";
constexpr char SHRPX_OPT_CLIENT_CERT_FILE[] = "client-cert-file";
constexpr char SHRPX_OPT_FRONTEND_HTTP2_DUMP_REQUEST_HEADER[] =
    "frontend-http2-dump-request-header";
constexpr char SHRPX_OPT_FRONTEND_HTTP2_DUMP_RESPONSE_HEADER[] =
    "frontend-http2-dump-response-header";
constexpr char SHRPX_OPT_HTTP2_NO_COOKIE_CRUMBLING[] =
    "http2-no-cookie-crumbling";
constexpr char SHRPX_OPT_FRONTEND_FRAME_DEBUG[] = "frontend-frame-debug";
constexpr char SHRPX_OPT_PADDING[] = "padding";
constexpr char SHRPX_OPT_ALTSVC[] = "altsvc";
constexpr char SHRPX_OPT_ADD_REQUEST_HEADER[] = "add-request-header";
constexpr char SHRPX_OPT_ADD_RESPONSE_HEADER[] = "add-response-header";
constexpr char SHRPX_OPT_WORKER_FRONTEND_CONNECTIONS[] =
    "worker-frontend-connections";
constexpr char SHRPX_OPT_NO_LOCATION_REWRITE[] = "no-location-rewrite";
constexpr char SHRPX_OPT_NO_HOST_REWRITE[] = "no-host-rewrite";
constexpr char SHRPX_OPT_BACKEND_HTTP1_CONNECTIONS_PER_HOST[] =
    "backend-http1-connections-per-host";
constexpr char SHRPX_OPT_BACKEND_HTTP1_CONNECTIONS_PER_FRONTEND[] =
    "backend-http1-connections-per-frontend";
constexpr char SHRPX_OPT_LISTENER_DISABLE_TIMEOUT[] =
    "listener-disable-timeout";
constexpr char SHRPX_OPT_TLS_TICKET_KEY_FILE[] = "tls-ticket-key-file";
constexpr char SHRPX_OPT_RLIMIT_NOFILE[] = "rlimit-nofile";
constexpr char SHRPX_OPT_BACKEND_REQUEST_BUFFER[] = "backend-request-buffer";
constexpr char SHRPX_OPT_BACKEND_RESPONSE_BUFFER[] = "backend-response-buffer";
constexpr char SHRPX_OPT_NO_SERVER_PUSH[] = "no-server-push";
constexpr char SHRPX_OPT_BACKEND_HTTP2_CONNECTIONS_PER_WORKER[] =
    "backend-http2-connections-per-worker";
constexpr char SHRPX_OPT_FETCH_OCSP_RESPONSE_FILE[] =
    "fetch-ocsp-response-file";
constexpr char SHRPX_OPT_OCSP_UPDATE_INTERVAL[] = "ocsp-update-interval";
constexpr char SHRPX_OPT_NO_OCSP[] = "no-ocsp";
constexpr char SHRPX_OPT_HEADER_FIELD_BUFFER[] = "header-field-buffer";
constexpr char SHRPX_OPT_MAX_HEADER_FIELDS[] = "max-header-fields";
constexpr char SHRPX_OPT_INCLUDE[] = "include";
constexpr char SHRPX_OPT_TLS_TICKET_KEY_CIPHER[] = "tls-ticket-key-cipher";
constexpr char SHRPX_OPT_HOST_REWRITE[] = "host-rewrite";
constexpr char SHRPX_OPT_TLS_SESSION_CACHE_MEMCACHED[] =
    "tls-session-cache-memcached";
constexpr char SHRPX_OPT_TLS_TICKET_KEY_MEMCACHED[] =
    "tls-ticket-key-memcached";
constexpr char SHRPX_OPT_TLS_TICKET_KEY_MEMCACHED_INTERVAL[] =
    "tls-ticket-key-memcached-interval";
constexpr char SHRPX_OPT_TLS_TICKET_KEY_MEMCACHED_MAX_RETRY[] =
    "tls-ticket-key-memcached-max-retry";
constexpr char SHRPX_OPT_TLS_TICKET_KEY_MEMCACHED_MAX_FAIL[] =
    "tls-ticket-key-memcached-max-fail";
constexpr char SHRPX_OPT_MRUBY_FILE[] = "mruby-file";
constexpr char SHRPX_OPT_ACCEPT_PROXY_PROTOCOL[] = "accept-proxy-protocol";
constexpr char SHRPX_OPT_FASTOPEN[] = "fastopen";
constexpr char SHRPX_OPT_TLS_DYN_REC_WARMUP_THRESHOLD[] =
    "tls-dyn-rec-warmup-threshold";
constexpr char SHRPX_OPT_TLS_DYN_REC_IDLE_TIMEOUT[] =
    "tls-dyn-rec-idle-timeout";
constexpr char SHRPX_OPT_ADD_FORWARDED[] = "add-forwarded";
constexpr char SHRPX_OPT_STRIP_INCOMING_FORWARDED[] =
    "strip-incoming-forwarded";
constexpr static char SHRPX_OPT_FORWARDED_BY[] = "forwarded-by";
constexpr char SHRPX_OPT_FORWARDED_FOR[] = "forwarded-for";

constexpr size_t SHRPX_OBFUSCATED_NODE_LENGTH = 8;

union sockaddr_union {
  sockaddr_storage storage;
  sockaddr sa;
  sockaddr_in6 in6;
  sockaddr_in in;
  sockaddr_un un;
};

struct Address {
  size_t len;
  union sockaddr_union su;
};

enum shrpx_proto { PROTO_HTTP2, PROTO_HTTP };

enum shrpx_forwarded_param {
  FORWARDED_NONE = 0,
  FORWARDED_BY = 0x1,
  FORWARDED_FOR = 0x2,
  FORWARDED_HOST = 0x4,
  FORWARDED_PROTO = 0x8,
};

enum shrpx_forwarded_node_type {
  FORWARDED_NODE_OBFUSCATED,
  FORWARDED_NODE_IP,
};

// Used inside function if it has to return const reference to empty
// string without defining empty string each time.
extern std::string EMPTY_STRING;

struct AltSvc {
  AltSvc() : port(0) {}

  std::string protocol_id, host, origin, service;

  uint16_t port;
};

struct DownstreamAddr {
  DownstreamAddr() : addr{}, port(0), host_unix(false) {}
  DownstreamAddr(const DownstreamAddr &other);
  DownstreamAddr(DownstreamAddr &&) = default;
  DownstreamAddr &operator=(const DownstreamAddr &other);
  DownstreamAddr &operator=(DownstreamAddr &&other) = default;

  Address addr;
  // backend address.  If |host_unix| is true, this is UNIX domain
  // socket path.
  ImmutableString host;
  ImmutableString hostport;
  // backend port.  0 if |host_unix| is true.
  uint16_t port;
  // true if |host| contains UNIX domain socket path.
  bool host_unix;
};

struct DownstreamAddrGroup {
  DownstreamAddrGroup(const std::string &pattern) : pattern(strcopy(pattern)) {}
  DownstreamAddrGroup(const DownstreamAddrGroup &other);
  DownstreamAddrGroup(DownstreamAddrGroup &&) = default;
  DownstreamAddrGroup &operator=(const DownstreamAddrGroup &other);
  DownstreamAddrGroup &operator=(DownstreamAddrGroup &&) = default;

  std::unique_ptr<char[]> pattern;
  std::vector<DownstreamAddr> addrs;
};

struct TicketKey {
  const EVP_CIPHER *cipher;
  const EVP_MD *hmac;
  size_t hmac_keylen;
  struct {
    // name of this ticket configuration
    std::array<uint8_t, 16> name;
    // encryption key for |cipher|
    std::array<uint8_t, 32> enc_key;
    // hmac key for |hmac|
    std::array<uint8_t, 32> hmac_key;
  } data;
};

struct TicketKeys {
  ~TicketKeys();
  std::vector<TicketKey> keys;
};

struct HttpProxy {
  Address addr;
  // host in http proxy URI
  std::string host;
  // userinfo in http proxy URI, not percent-encoded form
  std::string userinfo;
  // port in http proxy URI
  uint16_t port;
};

struct TLSConfig {
  // RFC 5077 Session ticket related configurations
  struct {
    struct {
      Address addr;
      uint16_t port;
      std::unique_ptr<char[]> host;
      ev_tstamp interval;
      // Maximum number of retries when getting TLS ticket key from
      // mamcached, due to network error.
      size_t max_retry;
      // Maximum number of consecutive error from memcached, when this
      // limit reached, TLS ticket is disabled.
      size_t max_fail;
    } memcached;
    std::vector<std::string> files;
    const EVP_CIPHER *cipher;
    // true if --tls-ticket-key-cipher is used
    bool cipher_given;
  } ticket;

  // Session cache related configurations
  struct {
    struct {
      Address addr;
      uint16_t port;
      std::unique_ptr<char[]> host;
    } memcached;
  } session_cache;

  // Dynamic record sizing configurations
  struct {
    size_t warmup_threshold;
    ev_tstamp idle_timeout;
  } dyn_rec;

  // OCSP realted configurations
  struct {
    ev_tstamp update_interval;
    std::unique_ptr<char[]> fetch_ocsp_response_file;
    bool disabled;
  } ocsp;

  // Client verification configurations
  struct {
    // Path to file containing CA certificate solely used for client
    // certificate validation
    std::unique_ptr<char[]> cacert;
    bool enabled;
  } client_verify;

  // Client private key and certificate used in backend connections.
  struct {
    std::unique_ptr<char[]> private_key_file;
    std::unique_ptr<char[]> cert_file;
  } client;

  // The list of (private key file, certificate file) pair
  std::vector<std::pair<std::string, std::string>> subcerts;
  std::vector<unsigned char> alpn_prefs;
  // list of supported NPN/ALPN protocol strings in the order of
  // preference.
  std::vector<std::string> npn_list;
  // list of supported SSL/TLS protocol strings.
  std::vector<std::string> tls_proto_list;
  // Bit mask to disable SSL/TLS protocol versions.  This will be
  // passed to SSL_CTX_set_options().
  long int tls_proto_mask;
  std::string backend_sni_name;
  std::chrono::seconds session_timeout;
  std::unique_ptr<char[]> private_key_file;
  std::unique_ptr<char[]> private_key_passwd;
  std::unique_ptr<char[]> cert_file;
  std::unique_ptr<char[]> dh_param_file;
  std::unique_ptr<char[]> ciphers;
  std::unique_ptr<char[]> cacert;
  bool insecure;
};

struct HttpConfig {
  struct {
    // obfuscated value used in "by" parameter of Forwarded header
    // field.  This is only used when user defined static obfuscated
    // string is provided.
    std::string by_obfuscated;
    // bitwise-OR of one or more of shrpx_forwarded_param values.
    uint32_t params;
    // type of value recorded in "by" parameter of Forwarded header
    // field.
    shrpx_forwarded_node_type by_node_type;
    // type of value recorded in "for" parameter of Forwarded header
    // field.
    shrpx_forwarded_node_type for_node_type;
    bool strip_incoming;
  } forwarded;
  struct {
    bool add;
    bool strip_incoming;
  } xff;
  std::vector<AltSvc> altsvcs;
  std::vector<std::pair<std::string, std::string>> add_request_headers;
  std::vector<std::pair<std::string, std::string>> add_response_headers;
  StringRef server_name;
  size_t header_field_buffer;
  size_t max_header_fields;
  bool no_via;
  bool no_location_rewrite;
  bool no_host_rewrite;
};

struct Http2Config {
  struct {
    struct {
      struct {
        std::unique_ptr<char[]> request_header_file;
        std::unique_ptr<char[]> response_header_file;
        FILE *request_header;
        FILE *response_header;
      } dump;
      bool frame_debug;
    } debug;
    nghttp2_option *option;
    nghttp2_session_callbacks *callbacks;
    size_t window_bits;
    size_t connection_window_bits;
  } upstream;
  struct {
    nghttp2_option *option;
    nghttp2_session_callbacks *callbacks;
    size_t window_bits;
    size_t connection_window_bits;
    size_t connections_per_worker;
  } downstream;
  struct {
    ev_tstamp stream_read;
    ev_tstamp stream_write;
  } timeout;
  size_t max_concurrent_streams;
  bool no_cookie_crumbling;
  bool no_server_push;
};

struct LoggingConfig {
  struct {
    std::vector<LogFragment> format;
    std::unique_ptr<char[]> file;
    // Send accesslog to syslog, ignoring accesslog_file.
    bool syslog;
  } access;
  struct {
    std::unique_ptr<char[]> file;
    // Send errorlog to syslog, ignoring errorlog_file.
    bool syslog;
  } error;
  int syslog_facility;
};

struct RateLimitConfig {
  size_t rate;
  size_t burst;
};

struct ConnectionConfig {
  struct {
    struct {
      ev_tstamp sleep;
    } timeout;
    // address of frontend connection.  This could be a path to UNIX
    // domain socket.  In this case, |host_unix| must be true.
    std::unique_ptr<char[]> host;
    // frontend listening port.  0 if frontend listens on UNIX domain
    // socket, in this case |host_unix| must be true.
    uint16_t port;
    // true if host contains UNIX domain socket path
    bool host_unix;
    int backlog;
    // TCP fastopen.  If this is positive, it is passed to
    // setsockopt() along with TCP_FASTOPEN.
    int fastopen;
  } listener;

  struct {
    struct {
      ev_tstamp http2_read;
      ev_tstamp read;
      ev_tstamp write;
    } timeout;
    struct {
      RateLimitConfig read;
      RateLimitConfig write;
    } ratelimit;
    size_t worker_connections;
    bool no_tls;
    bool accept_proxy_protocol;
  } upstream;

  struct {
    struct {
      ev_tstamp read;
      ev_tstamp write;
      ev_tstamp idle_read;
    } timeout;
    std::vector<DownstreamAddrGroup> addr_groups;
    // The index of catch-all group in downstream_addr_groups.
    size_t addr_group_catch_all;
    size_t connections_per_host;
    size_t connections_per_frontend;
    size_t request_buffer_size;
    size_t response_buffer_size;
    // downstream protocol; this will be determined by given options.
    shrpx_proto proto;
    bool no_tls;
    // true if IPv4 only; ipv4 and ipv6 are mutually exclusive; and
    // (ipv4 && ipv6) must be false.
    bool ipv4;
    // true if IPv6 only
    bool ipv6;
  } downstream;
};

struct Config {
  Router router;
  HttpProxy downstream_http_proxy;
  HttpConfig http;
  Http2Config http2;
  TLSConfig tls;
  LoggingConfig logging;
  ConnectionConfig conn;
  std::unique_ptr<char[]> pid_file;
  std::unique_ptr<char[]> conf_path;
  std::unique_ptr<char[]> user;
  std::unique_ptr<char[]> mruby_file;
  char **original_argv;
  char **argv;
  char *cwd;
  size_t num_worker;
  size_t padding;
  size_t rlimit_nofile;
  int argc;
  uid_t uid;
  gid_t gid;
  pid_t pid;
  bool verbose;
  bool daemon;
  bool http2_proxy;
  bool http2_bridge;
  bool client_proxy;
  bool client;
  // true if --client or --client-proxy are enabled.
  bool client_mode;
};

const Config *get_config();
Config *mod_config();
void create_config();

// Parses option name |opt| and value |optarg|.  The results are
// stored into statically allocated Config object. This function
// returns 0 if it succeeds, or -1.  The |included_set| contains the
// all paths already included while processing this configuration, to
// avoid loop in --include option.
int parse_config(const char *opt, const char *optarg,
                 std::set<std::string> &included_set);

// Loads configurations from |filename| and stores them in statically
// allocated Config object. This function returns 0 if it succeeds, or
// -1.  See parse_config() for |include_set|.
int load_config(const char *filename, std::set<std::string> &include_set);

// Read passwd from |filename|
std::string read_passwd_from_file(const char *filename);

// Parses header field in |optarg|.  We expect header field is formed
// like "NAME: VALUE".  We require that NAME is non empty string.  ":"
// is allowed at the start of the NAME, but NAME == ":" is not
// allowed.  This function returns pair of NAME and VALUE.
std::pair<std::string, std::string> parse_header(const char *optarg);

std::vector<LogFragment> parse_log_format(const char *optarg);

// Returns string for syslog |facility|.
const char *str_syslog_facility(int facility);

// Returns integer value of syslog |facility| string.
int int_syslog_facility(const char *strfacility);

FILE *open_file_for_write(const char *filename);

// Reads TLS ticket key file in |files| and returns TicketKey which
// stores read key data.  The given |cipher| and |hmac| determine the
// expected file size.  This function returns TicketKey if it
// succeeds, or nullptr.
std::unique_ptr<TicketKeys>
read_tls_ticket_key_file(const std::vector<std::string> &files,
                         const EVP_CIPHER *cipher, const EVP_MD *hmac);

// Selects group based on request's |hostport| and |path|.  |hostport|
// is the value taken from :authority or host header field, and may
// contain port.  The |path| may contain query part.  We require the
// catch-all pattern in place, so this function always selects one
// group.  The catch-all group index is given in |catch_all|.  All
// patterns are given in |groups|.
size_t match_downstream_addr_group(
    const Router &router, const std::string &hostport, const std::string &path,
    const std::vector<DownstreamAddrGroup> &groups, size_t catch_all);

} // namespace shrpx

#endif // SHRPX_CONFIG_H
