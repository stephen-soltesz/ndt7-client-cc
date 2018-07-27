// Part of Measurement Kit <https://measurement-kit.github.io/>.
// Measurement Kit is free software under the BSD license. See AUTHORS
// and LICENSE for more information on the copying conditions.
#ifndef LIBNDT_HPP
#define LIBNDT_HPP

/// \file libndt.hpp
///
/// \brief Public header of measurement-kit/libndt. The basic usage is a simple
/// as creating a `libndt::Client c` instance and then calling `c.run()`. More
/// advanced usage may require you to create a subclass of `libndt::Client` and
/// override specific virtual methods to customize the behaviour.
///
/// This implementation provides the C2S and S2C NDT subtests. We implement
/// NDT over TLS and NDT over websocket. For more information on the NDT
/// protocol, \see https://github.com/ndt-project/ndt/wiki/NDTProtocol.
///
/// \remark As a general rule, what is not documented using Doxygen comments
/// inside of this file is considered either internal or experimental. We
/// recommend you to only use documented interfaces.
///
/// Usage example:
///
/// ```
/// #include <libndt.hpp>
/// measurement_kit::libndt::Client client;
/// client.run();
/// ```

#ifndef _WIN32
#include <sys/socket.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#ifndef _WIN32
#include <netdb.h>
#include <poll.h>
#endif
#include <stddef.h>
#include <stdint.h>  // IWYU pragma: export

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

/// Contains measurement-kit/libndt code.
namespace libndt {

/// Type containing a version number.
using Version = unsigned int;

/// Major API version number of measurement-kit/libndt.
constexpr Version version_major = Version{0};

/// Minor API version number of measurement-kit/libndt.
constexpr Version version_minor = Version{26};

/// Patch API version number of measurement-kit/libndt.
constexpr Version version_patch = Version{0};

/// Flags that indicate what subtests to run.
using NettestFlags = unsigned char;

constexpr NettestFlags nettest_flag_middlebox = NettestFlags{1U << 0};

/// Run the upload subtest.
constexpr NettestFlags nettest_flag_upload = NettestFlags{1U << 1};

/// Run the download subtest.
constexpr NettestFlags nettest_flag_download = NettestFlags{1U << 2};

constexpr NettestFlags nettest_flag_simple_firewall = NettestFlags{1U << 3};

constexpr NettestFlags nettest_flag_status = NettestFlags{1U << 4};

constexpr NettestFlags nettest_flag_meta = NettestFlags{1U << 5};

constexpr NettestFlags nettest_flag_upload_ext = NettestFlags{1U << 6};

/// Run the multi-stream download subtest.
constexpr NettestFlags nettest_flag_download_ext = NettestFlags{1U << 7};

/// Library's logging verbosity.
using Verbosity = unsigned int;

/// Do not emit any log message.
constexpr Verbosity verbosity_quiet = Verbosity{0};

/// Emit only warning messages.
constexpr Verbosity verbosity_warning = Verbosity{1};

/// Emit warning and informational messages.
constexpr Verbosity verbosity_info = Verbosity{2};

/// Emit all log messages.
constexpr Verbosity verbosity_debug = Verbosity{3};

constexpr const char *ndt_version_compat = "v3.7.0";

using Size = uint64_t;

constexpr Size SizeMax = UINT64_MAX;

using Ssize = int64_t;

#ifdef _WIN32
using Socket = SOCKET;
#else
using Socket = int;
#endif

constexpr bool is_socket_valid(Socket s) noexcept {
#ifdef _WIN32
  return s != INVALID_SOCKET;
#else
  return s >= 0;
#endif
}

/// Flags to select what protocol should be used.
using ProtocolFlags = unsigned int;

/// When this flag is set we use JSON messages. This specifically means that
/// we send and receive JSON messages (as opposed to raw strings).
constexpr ProtocolFlags protocol_flag_json = ProtocolFlags{1 << 0};

/// When this flag is set we use TLS. This specifically means that we will
/// use TLS channels for the control and the measurement connections.
constexpr ProtocolFlags protocol_flag_tls = ProtocolFlags{1 << 1};

/// When this flag is set we use WebSocket. This specifically means that
/// we use the WebSocket framing to encapsulate NDT messages.
constexpr ProtocolFlags protocol_flag_websocket = ProtocolFlags{1 << 2};

enum class Err;  // Forward declaration (see bottom of this file)

/// Timeout expressed in seconds.
using Timeout = unsigned int;

/// Flags modifying the behavior of mlab-ns.
using MlabnsPolicy = unsigned short;

/// Request just the closest NDT server.
constexpr MlabnsPolicy mlabns_policy_closest = MlabnsPolicy{0};

/// Request for a random NDT server.
constexpr MlabnsPolicy mlabns_policy_random = MlabnsPolicy{1};

/// Return a list of nearby NDT servers.
constexpr MlabnsPolicy mlabns_policy_geo_options = MlabnsPolicy{2};

/// NDT client settings. If you do not customize the settings when creating
/// a Client, the defaults listed below will be used instead.
class Settings {
 public:
  /// Base URL to be used to query the mlab-ns service. If you specify an
  /// explicit hostname, mlab-ns won't be used. Note that the URL specified
  /// here MUST NOT end with a final slash.
  std::string mlabns_base_url = "https://mlab-ns.appspot.com";

  /// Flags that modify the behavior of mlabn-ns. By default we use the
  /// geo_options policy that is the most robust to random server failures.
  MlabnsPolicy mlabns_policy = mlabns_policy_geo_options;

  /// Timeout used for I/O operations.
  Timeout timeout = Timeout{7} /* seconds */;

  /// Host name of the NDT server to use. If this is left blank (the default),
  /// we will use mlab-ns to discover a nearby server.
  std::string hostname;

  /// Port of the NDT server to use. If this is not specified, we will use
  /// the most correct port depending on the configuration.
  std::string port;

  /// The tests you want to run with the NDT server.
  NettestFlags nettest_flags = nettest_flag_download;

  /// Verbosity of the client. By default no message is emitted. Set to other
  /// values to get more messages (useful when debugging).
  Verbosity verbosity = verbosity_quiet;

  /// Metadata to include in the server side logs. By default we just identify
  /// the NDT version and the application.
  std::map<std::string, std::string> metadata{
      {"client.version", ndt_version_compat},
      {"client.application", "measurement-kit/libndt"},
  };

  /// Type of NDT protocol that you want to use. Depending on the requested
  /// protocol, you may need to change also the port. By default, NDT listens
  /// on port 3001 for in-clear communications and port 3010 for TLS ones.
  ProtocolFlags protocol_flags = ProtocolFlags{0};

  /// Maximum time for which a nettest (i.e. download) is allowed to run. After
  /// this time has elapsed, the code will stop downloading (or uploading). It
  /// is meant as a safeguard to prevent the test for running for much more time
  /// than anticipated, due to buffering and/or changing network conditions.
  Timeout max_runtime = Timeout{14} /* seconds */;

  /// SOCKSv5h port to use for tunnelling traffic using, e.g., Tor. If non
  /// empty, all DNS and TCP traffic should be tunnelled over such port.
  std::string socks5h_port;

  /// CA bundle path to be used to verify TLS connections. If you do not
  /// set this variable and you're on Unix, we'll attempt to use some reasonable
  /// default value. Otherwise, the test will fail.
  std::string ca_bundle_path;

  /// Whether to use the CA bundle and OpenSSL's builtin hostname validation to
  /// make sure we are talking to the correct host. Enabled by default, but it
  /// may be useful sometimes to disable it for testing purposes.
  bool tls_verify_peer = true;
};

using MsgType = unsigned char;

/// NDT client. In the typical usage, you just need to construct a Client,
/// optionally providing settings, and to call the run() method. More advanced
/// usage may require you to override methods in a subclass to customize the
/// default behavior. For instance, you may probably want to override the
/// on_result() method that is called when processing NDT results to either
/// show such results to a user or store them on the disk.
class Client {
 public:
  // Implementation note: this is the classic implementation of the pimpl
  // pattern where we use a unique pointer, constructor and destructor are
  // defined in the ndt.cpp file so the code compiles, and copy/move
  // constructors and operators are not defined, thus resulting deleted.
  //
  // See <https://herbsutter.com/gotw/_100/>.

  /// Constructs a Client with default settings.
  Client() noexcept;

  /// Constructs a Client with the specified @p settings.
  explicit Client(Settings settings) noexcept;

  /// Destroys a Client.
  virtual ~Client() noexcept;

  /// Runs a NDT test using the configured (or default) settings.
  bool run() noexcept;

  // Implementation note: currently SWIG does not propagate `noexcept` even
  // though that is implemented in master [1], hence we have removed this
  // qualifiers from the functions that SWIG needs to wrap.
  //
  // .. [1] https://github.com/swig/swig/issues/526

  /// Called when a warning message is emitted. The default behavior is to write
  /// the warning onto the `std::clog` standard stream.
  virtual void on_warning(const std::string &s);

  /// Called when an informational message is emitted. The default behavior is
  /// to write the warning onto the `std::clog` standard stream.
  virtual void on_info(const std::string &s);

  /// Called when a debug message is emitted. The default behavior is
  /// to write the warning onto the `std::clog` standard stream.
  virtual void on_debug(const std::string &s);

  /// Called to inform you about the measured speed. The default behavior is
  /// to write the provided information as an info message. @param tid is either
  /// nettest_download or nettest_upload. @param nflows is the number of flows
  /// that we're using. @param measured_bytes is the number of bytes received
  /// or sent since the previous measurement. @param measurement_interval is the
  /// number of seconds elapsed since the previous measurement. @param elapsed
  /// is the number of seconds elapsed since the beginning of the nettest.
  /// @param max_runtime is the maximum runtime of this nettest, as copied from
  /// the Settings. @remark By dividing @p elapsed by @p max_runtime, you can
  /// get the percentage of completion of the current nettest. @remark We
  /// provide you with @p tid, so you know whether the nettest is downloading
  /// bytes from the server or uploading bytes to the server.
  virtual void on_performance(NettestFlags tid, uint8_t nflows,
                              double measured_bytes,
                              double measurement_interval, double elapsed,
                              double max_runtime);

  /// Called to provide you with NDT results. The default behavior is
  /// to write the provided information as an info message. @param scope is
  /// either "web100", when we're passing you Web 100 variables, "tcp_info" when
  /// we're passing you TCP info variables, or "summary" when we're passing you
  /// summary variables. @param name is the name of the variable. @param value
  /// is the variable value (variables are typically int, float, or string).
  virtual void on_result(std::string scope, std::string name,
                         std::string value);

  /// Called when the server is busy. The default behavior is to write a
  /// warning message. @param msg is the reason why the server is busy, encoded
  /// according to the NDT protocol. @remark when Settings::hostname is empty,
  /// we will autodiscover one or more servers, depending on the configured
  /// policy; in the event in which we autodiscover more than one server, we
  /// will attempt to use each of them, hence, this method may be called more
  /// than once if some of these servers happen to be busy.
  virtual void on_server_busy(std::string msg);

  /*
               _        __             _    _ _                _
   ___ _ _  __| |  ___ / _|  _ __ _  _| |__| (_)__   __ _ _ __(_)
  / -_) ' \/ _` | / _ \  _| | '_ \ || | '_ \ | / _| / _` | '_ \ |
  \___|_||_\__,_| \___/_|   | .__/\_,_|_.__/_|_\__| \__,_| .__/_|
                            |_|                          |_|
  */
  // If you're just interested to use measurement-kit/libndt, you can stop
  // reading right here. All the remainder of this file is not documented on
  // purpose and contains functionality that you'll typically don't care about
  // unless you're looking into heavily customizing this library.
  //
  // High-level API
#ifdef SWIG
 private:
#endif

  virtual bool query_mlabns(std::vector<std::string> *) noexcept;
  virtual bool connect() noexcept;
  virtual bool send_login() noexcept;
  virtual bool recv_kickoff() noexcept;
  virtual bool wait_in_queue() noexcept;
  virtual bool recv_version() noexcept;
  virtual bool recv_tests_ids() noexcept;
  virtual bool run_tests() noexcept;
  virtual bool recv_results_and_logout() noexcept;
  virtual bool wait_close() noexcept;

  // Mid-level API

  virtual bool run_download() noexcept;
  virtual bool run_meta() noexcept;
  virtual bool run_upload() noexcept;

  // NDT protocol API
  // ````````````````
  //
  // This API allows to send and receive NDT messages. At the bottom of the
  // abstraction layer lie functions to send and receive NDT's binary protocol
  // which here is called "legacy". It's called like this because it's still
  // the original protocol, AFAIK, even though several additions were layered
  // on top of it over the years (i.e. websocket, JSON, and TLS).

  bool msg_write_login(const std::string &version) noexcept;

  virtual bool msg_write(MsgType code, std::string &&msg) noexcept;

  virtual bool msg_write_legacy(MsgType code, std::string &&msg) noexcept;

  virtual bool msg_expect_test_prepare(  //
      std::string *pport, uint8_t *pnflows) noexcept;

  virtual bool msg_expect_empty(MsgType code) noexcept;

  virtual bool msg_expect(MsgType code, std::string *msg) noexcept;

  virtual bool msg_read(MsgType *code, std::string *msg) noexcept;

  virtual bool msg_read_legacy(MsgType *code, std::string *msg) noexcept;

  // WebSocket
  // `````````
  //
  // This section contain a WebSocket implementation.

  // Send @p line over @p fd.
  virtual Err ws_sendln(Socket fd, std::string line) noexcept;

  // Receive shorter-than @p maxlen @p *line over @p fd.
  virtual Err ws_recvln(Socket fd, std::string *line, size_t maxlen) noexcept;

  // Perform websocket handshake. @param fd is the socket to use. @param
  // ws_flags specifies what headers to send and to expect (for more information
  // see the ws_f_xxx constants defined below). @param ws_protocol specifies
  // what protocol to specify as Sec-WebSocket-Protocol in the upgrade request.
  // @param port is used to construct the Host header.
  virtual Err ws_handshake(Socket fd, std::string port, uint64_t ws_flags,
                           std::string ws_protocol) noexcept;

  // Send @p count bytes from @p base over @p sock as a frame whose first byte
  // @p first_byte should contain the opcode and possibly the FIN flag.
  virtual Err ws_send_frame(Socket sock, uint8_t first_byte, uint8_t *base,
                            Size count) noexcept;

  // Receive a frame from @p sock. Puts the opcode in @p *opcode. Puts whether
  // there is a FIN flag in @p *fin. The buffer starts at @p base and it
  // contains @p total bytes. Puts in @p *count the actual number of bytes
  // in the message. @return The error that occurred or Err::none.
  Err ws_recv_any_frame(Socket sock, uint8_t *opcode, bool *fin, uint8_t *base,
                        Size total, Size *count) noexcept;

  // Receive a frame. Automatically and transparently responds to PING, ignores
  // PONG, and handles CLOSE frames. Arguments like ws_recv_any_frame().
  Err ws_recv_frame(Socket sock, uint8_t *opcode, bool *fin, uint8_t *base,
                    Size total, Size *count) noexcept;

  // Receive a message consisting of one or more frames. Transparently handles
  // PING and PONG frames. Handles CLOSE frames. @param sock is the socket to
  // use. @param opcode is where the opcode is returned. @param base is the
  // beginning of the buffer. @param total is the size of the buffer. @param
  // count contains the actual message size. @return An error on failure or
  // Err::none in case of success.
  Err ws_recvmsg(Socket sock, uint8_t *opcode, uint8_t *base, Size total,
                 Size *count) noexcept;

  // Networking layer
  // ````````````````
  //
  // This section contains network functionality used by NDT. The functionality
  // to connect to a remote host is layered to comply with the websocket spec
  // as follows:
  //
  // - netx_maybews_dial() calls netx_maybessl_dial() and, if that succeeds, it
  //   then attempts to negotiate a websocket channel (if enabled);
  //
  // - netx_maybessl_dial() calls netx_maybesocks5h_dial() and, if that
  //   suceeds, it then attempts to establish a TLS connection (if enabled);
  //
  // - netx_maybesocks5h_dial() possibly creates the connection through a
  //   SOCKSv5h proxy (if the proxy is enabled).
  //
  // By default with TLS we use a CA and we perform SNI validation. That can be
  // disabled for debug reasons. Doing that breaks compliancy with the websocket
  // spec. See <https://tools.ietf.org/html/rfc6455#section-4.1>.

  // Connect to @p hostname and @p port possibly using WebSocket,
  // SSL, and SOCKSv5. This depends on the Settings. See the documentation
  // of ws_handshake() for more info on @p ws_flags and @p ws_protocol.
  virtual Err netx_maybews_dial(const std::string &hostname,
                                const std::string &port, uint64_t ws_flags,
                                std::string ws_protocol,
                                Socket *sock) noexcept;

  // Connect to @p hostname and @p port possibly using SSL and SOCKSv5. This
  // depends on the Settings you configured.
  virtual Err netx_maybessl_dial(const std::string &hostname,
                                 const std::string &port,
                                 Socket *sock) noexcept;

  // Connect to @p hostname and @port possibly using SOCKSv5. This depends
  // on the Settings you configured.
  virtual Err netx_maybesocks5h_dial(const std::string &hostname,
                                     const std::string &port,
                                     Socket *sock) noexcept;

  // Map errno code into a Err value.
  static Err netx_map_errno(int ec) noexcept;

  // Map getaddrinfo return value into a Err value.
  Err netx_map_eai(int ec) noexcept;

  // Connect to @p hostname and @p port.
  virtual Err netx_dial(const std::string &hostname, const std::string &port,
                        Socket *sock) noexcept;

  // Receive from the network.
  virtual Err netx_recv(Socket fd, void *base, Size count,
                        Size *actual) noexcept;

  // Receive from the network without blocking.
  virtual Err netx_recv_nonblocking(Socket fd, void *base, Size count,
                                    Size *actual) noexcept;

  // Receive exactly N bytes from the network.
  virtual Err netx_recvn(Socket fd, void *base, Size count) noexcept;

  // Send data to the network.
  virtual Err netx_send(Socket fd, const void *base, Size count,
                        Size *actual) noexcept;

  // Send to the network without blocking.
  virtual Err netx_send_nonblocking(Socket fd, const void *base, Size count,
                                    Size *actual) noexcept;

  // Send exactly N bytes to the network.
  virtual Err netx_sendn(Socket fd, const void *base, Size count) noexcept;

  // Resolve hostname into a list of IP addresses.
  virtual Err netx_resolve(const std::string &hostname,
                           std::vector<std::string> *addrs) noexcept;

  // Set socket non blocking.
  virtual Err netx_setnonblocking(Socket fd, bool enable) noexcept;

  // Pauses until the socket becomes readable.
  virtual Err netx_wait_readable(Socket, Timeout timeout) noexcept;

  // Pauses until the socket becomes writeable.
  virtual Err netx_wait_writeable(Socket, Timeout timeout) noexcept;

  // Main function for dealing with I/O patterned after poll(2).
  virtual Err netx_poll(std::vector<pollfd> *fds, int timeout_msec) noexcept;

  // Shutdown both ends of a socket.
  virtual Err netx_shutdown_both(Socket fd) noexcept;

  // Close a socket.
  virtual Err netx_closesocket(Socket fd) noexcept;

  // Dependencies (cURL)

  Verbosity get_verbosity() const noexcept;

  virtual bool query_mlabns_curl(const std::string &url, long timeout,
                                 std::string *body) noexcept;

  // Other helpers

  std::mutex &get_mutex() noexcept;

  // Dependencies (system)
  // `````````````````````
  //
  // This section contains wrappers for system calls used in regress tests.

  // Access the value of errno in a portable way.
  virtual int sys_get_last_error() noexcept;

  // Set the value of errno in a portable way.
  virtual void sys_set_last_error(int err) noexcept;

  // getaddrinfo() wrapper that can be mocked in tests.
  virtual int sys_getaddrinfo(const char *domain, const char *port,
                              const addrinfo *hints, addrinfo **res) noexcept;

  // getnameinfo() wrapper that can be mocked in tests.
  virtual int sys_getnameinfo(const sockaddr *sa, socklen_t salen, char *host,
                              socklen_t hostlen, char *serv, socklen_t servlen,
                              int flags) noexcept;

  // freeaddrinfo() wrapper that can be mocked in tests.
  virtual void sys_freeaddrinfo(addrinfo *aip) noexcept;

  // socket() wrapper that can be mocked in tests.
  virtual Socket sys_socket(int domain, int type, int protocol) noexcept;

  // connect() wrapper that can be mocked in tests.
  virtual int sys_connect(Socket fd, const sockaddr *sa, socklen_t n) noexcept;

  // recv() wrapper that can be mocked in tests.
  virtual Ssize sys_recv(Socket fd, void *base, Size count) noexcept;

  // send() wrapper that can be mocked in tests.
  virtual Ssize sys_send(Socket fd, const void *base, Size count) noexcept;

  // shutdown() wrapper that can be mocked in tests.
  virtual int sys_shutdown(Socket fd, int shutdown_how) noexcept;

  // Portable wrapper for closing a socket descriptor.
  virtual int sys_closesocket(Socket fd) noexcept;

  // poll() wrapper that can be mocked in tests.
#ifdef _WIN32
  virtual int sys_poll(LPWSAPOLLFD fds, ULONG nfds, INT timeout) noexcept;
#else
  virtual int sys_poll(pollfd *fds, nfds_t nfds, int timeout) noexcept;
#endif

  // If strtonum() is available, wrapper that can be mocked in tests, else
  // implementation of strtonum() borrowed from OpenBSD.
  virtual long long sys_strtonum(const char *s, long long minval,
                                 long long maxval, const char **err) noexcept;

#ifdef _WIN32
  // ioctlsocket() wrapper that can be mocked in tests.
  virtual int sys_ioctlsocket(Socket s, long cmd, u_long *argp) noexcept;
#else
  // Wrapper for fcntl() taking just two arguments. Good for getting the
  // currently value of the socket flags.
  virtual int sys_fcntl(Socket s, int cmd) noexcept;

  // Wrapper for fcntl() taking three arguments with the third argument
  // being an integer value. Good for setting O_NONBLOCK on a socket.
  virtual int sys_fcntl(Socket s, int cmd, int arg) noexcept;
#endif

  // getsockopt() wrapper that can be mocked in tests.
  virtual int sys_getsockopt(Socket socket, int level, int name, void *value,
                             socklen_t *len) noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

// Error codes
// ```````````

enum class Err {
  none,
  //
  // Error codes that map directly to errno values. Here we use the naming used
  // by the C++ library <https://en.cppreference.com/w/cpp/error/errc>.
  //
  broken_pipe,
  connection_aborted,
  connection_refused,
  connection_reset,
  function_not_supported,
  host_unreachable,
  interrupted,
  invalid_argument,
  io_error,
  message_size,
  network_down,
  network_reset,
  network_unreachable,
  operation_in_progress,
  operation_would_block,
  timed_out,
  value_too_large,
  //
  // Getaddrinfo() error codes. See <http://man.openbsd.org/gai_strerror>.
  //
  ai_generic,
  ai_again,
  ai_fail,
  ai_noname,
  //
  // SSL error codes. See <http://man.openbsd.org/SSL_get_error>.
  //
  ssl_generic,
  ssl_want_read,
  ssl_want_write,
  ssl_syscall,
  //
  // Libndt misc error codes.
  //
  eof,
  socks5h,
  ws_proto,
};

// NDT message types
// `````````````````
// See <https://github.com/ndt-project/ndt/wiki/NDTProtocol#message-types>.

constexpr MsgType msg_comm_failure = MsgType{0};
constexpr MsgType msg_srv_queue = MsgType{1};
constexpr MsgType msg_login = MsgType{2};
constexpr MsgType msg_test_prepare = MsgType{3};
constexpr MsgType msg_test_start = MsgType{4};
constexpr MsgType msg_test_msg = MsgType{5};
constexpr MsgType msg_test_finalize = MsgType{6};
constexpr MsgType msg_error = MsgType{7};
constexpr MsgType msg_results = MsgType{8};
constexpr MsgType msg_logout = MsgType{9};
constexpr MsgType msg_waiting = MsgType{10};
constexpr MsgType msg_extended_login = MsgType{11};

// WebSocket constants
// ```````````````````

// Opcodes. See <https://tools.ietf.org/html/rfc6455#section-11.8>.
constexpr uint8_t ws_opcode_continue = 0;
constexpr uint8_t ws_opcode_text = 1;
constexpr uint8_t ws_opcode_binary = 2;
constexpr uint8_t ws_opcode_close = 8;
constexpr uint8_t ws_opcode_ping = 9;
constexpr uint8_t ws_opcode_pong = 10;

// Constants useful to process the first octet of a websocket frame. For more
// info see <https://tools.ietf.org/html/rfc6455#section-5.2>.
constexpr uint8_t ws_fin_flag = 0x80;
constexpr uint8_t ws_reserved_mask = 0x70;
constexpr uint8_t ws_opcode_mask = 0x0f;

// Constants useful to process the second octet of a websocket frame. For more
// info see <https://tools.ietf.org/html/rfc6455#section-5.2>.
constexpr uint8_t ws_mask_flag = 0x80;
constexpr uint8_t ws_len_mask = 0x7f;

// Flags used to specify what HTTP headers are required and present into the
// websocket handshake where we upgrade from HTTP/1.1 to websocket.
constexpr uint64_t ws_f_connection = 1 << 0;
constexpr uint64_t ws_f_sec_ws_accept = 1 << 1;
constexpr uint64_t ws_f_sec_ws_protocol = 1 << 2;
constexpr uint64_t ws_f_upgrade = 1 << 3;

// Values of Sec-WebSocket-Protocol used by ndt-project/ndt.
constexpr const char *ws_proto_control = "ndt";
constexpr const char *ws_proto_c2s = "c2s";
constexpr const char *ws_proto_s2c = "s2c";

}  // namespace libndt
#endif
