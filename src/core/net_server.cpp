// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link's transport (task 0172, ADR 0055): see net_server.h. WinSock on Windows, BSD sockets elsewhere; an accept
// thread that polls the listening socket every 100 ms, and a sender thread per client draining its queue.
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <netinet/in.h>
#  include <fcntl.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

#include "net_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace bl::net {

namespace {

#ifdef _WIN32
using sock_t = SOCKET;
const sock_t kBad = INVALID_SOCKET;
void close_sock(sock_t s) { closesocket(s); }
void shut_sock(sock_t s) { shutdown(s, SD_BOTH); }
const int kSendFlags = 0;
bool sockets_ready() {
  static const bool ok = [] {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  return ok;
}
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
/* Not inherited by a program the editor starts, and sends that never block (they wait for room in send_all). */
void own_socket(sock_t s) {
  SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
  u_long one = 1;
  ioctlsocket(s, FIONBIO, &one);
}
void keep_private(sock_t s) { SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0); }
#else
using sock_t = int;
const sock_t kBad = -1;
void close_sock(sock_t s) { ::close(s); }
void shut_sock(sock_t s) { shutdown(s, SHUT_RDWR); }
#  ifdef MSG_NOSIGNAL
const int kSendFlags = MSG_NOSIGNAL;
#  else
const int kSendFlags = 0;  // (macOS: SO_NOSIGPIPE on the socket instead)
#  endif
bool sockets_ready() { return true; }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }
/* Not inherited by a program the editor starts (xdg-open, a renderer: they would keep the port), and sends that never
 * block (they wait for room in send_all). */
void keep_private(sock_t s) { fcntl(s, F_SETFD, fcntl(s, F_GETFD) | FD_CLOEXEC); }
void own_socket(sock_t s) {
  keep_private(s);
  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
}
#endif

sock_t to_sock(intptr_t v) { return (sock_t)v; }

/* One socket readable (or closed) within `timeout_ms`: 1, 0 on a timeout, -1 on an error. */
int wait_readable(sock_t s, int timeout_ms) {
#ifdef _WIN32
  WSAPOLLFD p{s, POLLRDNORM, 0};
  const int r = WSAPoll(&p, 1, timeout_ms);
#else
  pollfd p{s, POLLIN, 0};
  const int r = ::poll(&p, 1, timeout_ms);  // (not select: a descriptor past FD_SETSIZE would overrun its set)
#endif
  return r < 0 ? -1 : r > 0 ? 1 : 0;
}

/* Room to send within `timeout_ms`: true, or false on a timeout. */
bool wait_writable(sock_t s, int timeout_ms) {
#ifdef _WIN32
  WSAPOLLFD p{s, POLLWRNORM, 0};
  return WSAPoll(&p, 1, timeout_ms) > 0;
#else
  pollfd p{s, POLLOUT, 0};
  return ::poll(&p, 1, timeout_ms) > 0;
#endif
}

void set_socket_options(sock_t s) {
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
  int buf = 1 << 20;
  setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&buf, sizeof(buf));
#ifdef SO_NOSIGPIPE
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof(one));
#endif
  own_socket(s);
}

/* All of `data`, waiting for room as long as `go` says (a slow client is waited for, never timed out: only stop() and
 * the queue limit close one). False when the peer is gone or `go` turned false. */
template<class Go> bool send_all(sock_t s, const std::string &data, Go go) {
  size_t off = 0;
  while (off < data.size()) {
    const int chunk = (int)std::min<size_t>(data.size() - off, 1u << 20);
    const int n = (int)::send(s, data.data() + off, chunk, kSendFlags);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && would_block() && go()) {
      wait_writable(s, 100);
      continue;
    }
    return false;
  }
  return true;
}

/* True when the peer has closed. What it sent is read on the way: the first line (up to 512 bytes, a bridge's hello -
 * task 0274) is kept in `first`, everything else dropped. */
bool peer_closed(sock_t s, std::string &first, bool &first_done) {
  char drain[4096];
  for (int i = 0; i < 16; i++) {
    const int r = wait_readable(s, 0);
    if (r < 0) return true;
    if (r == 0) return false;
    const int n = (int)::recv(s, drain, sizeof(drain), 0);
    if (n < 0 && would_block()) return false;
    if (n <= 0) return true;
    for (int k = 0; k < n && !first_done; k++) {
      if (drain[k] == '\n' || first.size() >= 512) first_done = true;
      else first += drain[k];
    }
  }
  return false;
}

/* The line with ,"bin":N added before its closing brace when there is an attachment, its newline, the attachment. */
std::shared_ptr<const std::string> frame(const std::string &line, const std::string &bin) {
  auto out = std::make_shared<std::string>();
  out->reserve(line.size() + 24 + bin.size());
  if (bin.empty() || line.empty() || line.back() != '}') *out = line;
  else {
    const bool empty_obj = line.size() >= 2 && line[line.size() - 2] == '{';
    char tail[40];
    std::snprintf(tail, sizeof(tail), "%s\"bin\":%zu}", empty_obj ? "" : ",", bin.size());
    out->assign(line, 0, line.size() - 1);
    *out += tail;
  }
  *out += '\n';
  *out += bin;
  return out;
}

struct Msg {
  std::shared_ptr<const std::string> data;
  uint64_t key = 0;
};

struct Client {
  int id = 0;
  sock_t s = kBad;
  std::thread th;
  std::mutex m;
  std::condition_variable cv;
  std::deque<Msg> q;
  size_t queued = 0;
  /* Coalescing without a search: the queue's entries are numbered head, head + 1, ...; `where` holds the number a key
   * was last queued at, and a keyed message replaces it only while it is still queued and after `barrier` (the
   * number after the last message without a key). */
  uint64_t head = 0, tail = 0, barrier = 0;
  std::unordered_map<uint64_t, uint64_t> where;
  bool ready = false;  // (guarded by the server's mutex)
  std::string first;   // the client's first line, read by its thread (task 0274); `hello` holds it once whole
  bool first_done = false;
  std::string hello;   // (guarded by m)
  std::atomic<bool> alive{true};
  std::atomic<bool> done{false};
};

}  // namespace

struct Server::Impl {
  sock_t listen = kBad;
  int port = 0;
  std::thread accept_th;
  std::atomic<bool> stopping{false}, paused{false};
  mutable std::mutex m;
  std::vector<std::shared_ptr<Client>> clients;
  std::vector<int> fresh;
  int next_id = 1;
  std::atomic<size_t> limit{(size_t)256 << 20};
  std::atomic<uint64_t> messages{0}, bytes{0}, coalesced{0}, dropped{0}, accepted{0};

  void accept_loop();
  void sender(const std::shared_ptr<Client> &c);
  void enqueue(Client &c, const std::shared_ptr<const std::string> &data, uint64_t key);
  void enqueue_locked(Client &c, const std::shared_ptr<const std::string> &data, uint64_t key);
  bool try_coalesce(Client &c, const std::shared_ptr<const std::string> &data, uint64_t key);
  bool take_next(Client &c, Msg &out);
  void reap_locked();
  std::vector<std::shared_ptr<Client>> targets(const std::vector<int> &ids, bool and_ready) const;
};

void Server::Impl::accept_loop() {
  while (!stopping) {
    const int r = wait_readable(listen, 100);
    if (r <= 0) continue;
    const sock_t s = ::accept(listen, nullptr, nullptr);
    if (s == kBad) continue;
    keep_private(s);
    set_socket_options(s);
    auto c = std::make_shared<Client>();
    c->s = s;
    std::lock_guard<std::mutex> lk(m);
    c->id = next_id++;
    clients.push_back(c);
    fresh.push_back(c->id);
    accepted++;
    c->th = std::thread([this, c] { sender(c); });
  }
}

/* The next message for the client, or false when it should stop (closed, the server stopping). Between messages
 * every 100 ms it checks that the peer is still there. */
bool Server::Impl::take_next(Client &c, Msg &out) {
  for (;;) {
    std::unique_lock<std::mutex> lk(c.m);
    c.cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return (!c.q.empty() && !paused) || !c.alive || stopping; });
    if (!c.alive || stopping) return false;
    if (!c.q.empty() && !paused) {
      out = std::move(c.q.front());
      c.q.pop_front();
      c.head++;
      c.queued -= out.data->size();
      return true;
    }
    lk.unlock();
    const bool was_done = c.first_done;
    if (peer_closed(c.s, c.first, c.first_done)) return false;
    if (c.first_done && !was_done) {
      std::lock_guard<std::mutex> hk(c.m);
      c.hello = c.first;
    }
  }
}

void Server::Impl::sender(const std::shared_ptr<Client> &c) {
  Msg msg;
  while (take_next(*c, msg)) {
    if (!send_all(c->s, *msg.data, [&] { return c->alive && !stopping; })) break;
    messages++;
    bytes += msg.data->size();
    msg = Msg();
  }
  {
    std::lock_guard<std::mutex> lk(c->m);  // (enqueue and stop shut the socket only while alive, under this lock)
    c->alive = false;
  }
  shut_sock(c->s);
  close_sock(c->s);
  c->done = true;
}

void Server::Impl::enqueue(Client &c, const std::shared_ptr<const std::string> &data, uint64_t key) {
  std::lock_guard<std::mutex> lk(c.m);
  enqueue_locked(c, data, key);
  c.cv.notify_one();
}

/* Replaces the queued message with this key, when it is still queued and after the last message without a key. */
bool Server::Impl::try_coalesce(Client &c, const std::shared_ptr<const std::string> &data, uint64_t key) {
  auto it = c.where.find(key);
  if (it == c.where.end() || it->second < c.head || it->second < c.barrier) return false;
  Msg &old = c.q[(size_t)(it->second - c.head)];
  c.queued = c.queued + data->size() - old.data->size();
  old.data = data;
  coalesced++;
  return true;
}

void Server::Impl::enqueue_locked(Client &c, const std::shared_ptr<const std::string> &data, uint64_t key) {
  if (!c.alive) return;
  if (key && try_coalesce(c, data, key)) return;
  if (!key) c.where.clear();  // (nothing before a barrier can be replaced: its places are forgotten)
  const uint64_t seq = c.tail++;
  if (key) c.where[key] = seq;
  else c.barrier = seq + 1;
  c.q.push_back({data, key});
  c.queued += data->size();
  if (c.queued > limit) {  // too far behind: closed (its thread wakes from the shutdown)
    c.alive = false;
    dropped++;
    shut_sock(c.s);
  }
}

/* Drops the clients whose threads have ended (never waits for one still sending). */
void Server::Impl::reap_locked() {
  for (size_t i = 0; i < clients.size();) {
    if (!clients[i]->done) {
      i++;
      continue;
    }
    if (clients[i]->th.joinable()) clients[i]->th.join();
    clients.erase(clients.begin() + (ptrdiff_t)i);
  }
}

std::vector<std::shared_ptr<Client>> Server::Impl::targets(const std::vector<int> &ids, bool and_ready) const {
  std::vector<std::shared_ptr<Client>> out;
  std::lock_guard<std::mutex> lk(m);
  for (const auto &c : clients) {
    const bool named = std::find(ids.begin(), ids.end(), c->id) != ids.end();
    if (c->alive && (named || (and_ready && c->ready))) out.push_back(c);
  }
  return out;
}

Server::Server() : d_(std::make_unique<Impl>()) {}
Server::~Server() { stop(); }

bool Server::start(int port, std::string *error) {
  stop();
  auto fail = [&](const char *why) {
    if (error) *error = why;
    if (d_->listen != kBad) close_sock(d_->listen);
    d_->listen = kBad;
    return false;
  };
  if (!sockets_ready()) return fail("sockets aren't available");
  d_->listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (d_->listen == kBad) return fail("couldn't open a socket");
  keep_private(d_->listen);
#ifdef _WIN32
  BOOL excl = TRUE;  // (no other program can bind the port over this one)
  setsockopt(d_->listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof(excl));
#endif
#ifndef _WIN32
  int one = 1;  // (a restart right after a stop reuses the port)
  setsockopt(d_->listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons((uint16_t)port);
  if (::bind(d_->listen, (sockaddr *)&a, sizeof(a)) != 0) return fail("the port is in use");
  if (::listen(d_->listen, 8) != 0) return fail("couldn't listen on the port");
  socklen_t len = sizeof(a);
  getsockname(d_->listen, (sockaddr *)&a, &len);
  d_->port = ntohs(a.sin_port);
  d_->stopping = false;
  d_->accept_th = std::thread([this] { d_->accept_loop(); });
  return true;
}

void Server::stop() {
  if (d_->listen == kBad) return;
  d_->stopping = true;
  if (d_->accept_th.joinable()) d_->accept_th.join();
  close_sock(d_->listen);
  d_->listen = kBad;
  std::vector<std::shared_ptr<Client>> all;
  {
    std::lock_guard<std::mutex> lk(d_->m);
    all.swap(d_->clients);
    d_->fresh.clear();
  }
  for (auto &c : all) {
    std::lock_guard<std::mutex> cl(c->m);
    if (c->alive) shut_sock(c->s);  // (wakes a send in progress; a closed one's socket is never touched)
    c->alive = false;
    c->cv.notify_one();
  }
  for (auto &c : all)
    if (c->th.joinable()) c->th.join();
  d_->port = 0;
}

bool Server::running() const { return d_->listen != kBad; }
int Server::port() const { return d_->port; }

std::vector<int> Server::take_new_clients() {
  std::lock_guard<std::mutex> lk(d_->m);
  d_->reap_locked();
  std::vector<int> out;
  out.swap(d_->fresh);
  return out;
}

void Server::ready(int client) {
  std::lock_guard<std::mutex> lk(d_->m);
  for (auto &c : d_->clients)
    if (c->id == client) c->ready = true;
}

std::vector<int> Server::ready_clients() const {
  std::vector<int> out;
  std::lock_guard<std::mutex> lk(d_->m);
  for (const auto &c : d_->clients)
    if (c->alive && c->ready) out.push_back(c->id);
  return out;
}

std::vector<std::string> Server::client_hellos() const {
  std::vector<std::shared_ptr<Client>> list;
  {
    std::lock_guard<std::mutex> lk(d_->m);
    for (const auto &c : d_->clients)
      if (c->alive) list.push_back(c);
  }
  std::vector<std::string> out;
  for (const auto &c : list) {
    std::lock_guard<std::mutex> hk(c->m);
    out.push_back(c->hello);
  }
  return out;
}

int Server::client_count() const {
  std::lock_guard<std::mutex> lk(d_->m);
  d_->reap_locked();
  return (int)std::count_if(d_->clients.begin(), d_->clients.end(), [](const std::shared_ptr<Client> &c) { return (bool)c->alive; });
}

void Server::broadcast(const std::string &line, const std::string &bin, uint64_t coalesce) { send_to({}, true, line, bin, coalesce); }

void Server::send_to(int client, const std::string &line, const std::string &bin, uint64_t coalesce) {
  send_to(std::vector<int>{client}, false, line, bin, coalesce);
}

void Server::send_to(const std::vector<int> &clients, bool and_ready, const std::string &line, const std::string &bin, uint64_t coalesce) {
  const auto to = d_->targets(clients, and_ready);
  if (to.empty()) return;
  const auto data = frame(line, bin);
  for (const auto &c : to) d_->enqueue(*c, data, coalesce);
}

void Server::send_batch(const std::vector<int> &clients, bool and_ready, const std::vector<Out> &msgs) {
  if (msgs.empty()) return;
  const auto to = d_->targets(clients, and_ready);
  if (to.empty()) return;
  std::vector<std::shared_ptr<const std::string>> data;
  data.reserve(msgs.size());
  for (const Out &m : msgs) data.push_back(frame(m.line, m.bin));
  for (const auto &c : to) {
    std::lock_guard<std::mutex> lk(c->m);
    for (size_t i = 0; i < msgs.size(); i++) d_->enqueue_locked(*c, data[i], msgs[i].coalesce);
    c->cv.notify_one();
  }
}

Server::Stats Server::stats() const {
  Stats s;
  s.messages = d_->messages, s.bytes = d_->bytes, s.coalesced = d_->coalesced, s.dropped = d_->dropped, s.accepted = d_->accepted;
  std::lock_guard<std::mutex> lk(d_->m);
  for (const auto &c : d_->clients) {
    std::lock_guard<std::mutex> cl(c->m);
    s.queued_bytes += c->queued;
  }
  return s;
}

void Server::set_queue_limit(size_t bytes) { d_->limit = bytes; }

void Server::pause_sending_for_test(bool paused) {
  d_->paused = paused;
  std::lock_guard<std::mutex> lk(d_->m);
  for (auto &c : d_->clients) c->cv.notify_one();
}

/* ===================================================================== */
/* The client                                                             */
/* ===================================================================== */

size_t line_bin_size(const std::string &line) {
  const size_t k = line.rfind("\"bin\":");
  if (k == std::string::npos || line.empty() || line.back() != '}') return 0;
  size_t n = 0;
  for (size_t i = k + 6; i + 1 < line.size(); i++) {
    if (line[i] < '0' || line[i] > '9') return 0;
    n = n * 10 + (size_t)(line[i] - '0');
  }
  return n;
}

LineClient::~LineClient() { close(); }

bool LineClient::connect(int port, int timeout_ms) {
  close();
  if (!sockets_ready()) return false;
  const sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kBad) return false;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons((uint16_t)port);
  (void)timeout_ms;  // (a refused loopback connect fails at once; a listening one is accepted by the backlog)
  if (::connect(s, (sockaddr *)&a, sizeof(a)) != 0) {
    close_sock(s);
    return false;
  }
  sock_ = (intptr_t)s;
  buf_.clear();
  return true;
}

/* Reads what has arrived into the buffer, waiting up to `timeout_ms` for something: false on a timeout or close. */
bool LineClient::fill(int timeout_ms) {
  if (sock_ == -1) return false;
  const sock_t s = to_sock(sock_);
  if (wait_readable(s, timeout_ms) <= 0) return false;
  char tmp[65536];
  const int n = (int)::recv(s, tmp, sizeof(tmp), 0);
  if (n <= 0) {
    close();
    return false;
  }
  buf_.append(tmp, (size_t)n);
  return true;
}

bool LineClient::read(std::string &line, std::string &bin, int timeout_ms) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  auto left = [&] { return (int)std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count()); };
  size_t nl;
  while ((nl = buf_.find('\n')) == std::string::npos)
    if (!fill(left())) return false;
  line.assign(buf_, 0, nl);
  const size_t need = line_bin_size(line);
  while (buf_.size() < nl + 1 + need)
    if (!fill(left())) return false;
  bin.assign(buf_, nl + 1, need);
  buf_.erase(0, nl + 1 + need);
  return true;
}

/* A line to the server (its newline added): a bridge's hello. */
bool LineClient::send(const std::string &line) {
  if (sock_ == -1) return false;
  return send_all(to_sock(sock_), line + "\n", [] { return true; });
}

void LineClient::close() {
  if (sock_ == -1) return;
  close_sock(to_sock(sock_));
  sock_ = -1;
}

}  // namespace bl::net
