// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link's transport (task 0172, ADR 0055): a TCP server on 127.0.0.1 that sends lines of JSON, each one optionally
// followed by a binary attachment, to every connected client - on threads of its own, so the editor's frame never
// waits for a socket. One way: what a client sends is read and dropped. LineClient is the other end (the tests, tools).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl::net {

class Server {
 public:
  Server();
  ~Server();
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  /* Listens on 127.0.0.1:`port` (0: a free port the system picks; port() says which). False with a reason when the
   * port is taken or sockets aren't there. Starting a running server stops it first. */
  bool start(int port, std::string *error = nullptr);
  void stop();  // closes every client and waits for the threads
  bool running() const;
  int port() const;

  /* Clients accepted since the last call: the caller sends each one its first messages (send_to) and then calls
   * ready(id); from then on broadcasts reach it too. */
  std::vector<int> take_new_clients();
  void ready(int client);
  std::vector<int> ready_clients() const;
  int client_count() const;  // connected now (a closed one leaves when its thread notices: within 100 ms)

  /* Queues `line` (one JSON object, no newline) and its attachment `bin`: the server adds ,"bin":<bytes> to the
   * line's object when there is one, and sends the bytes right after the line's newline. A nonzero `coalesce` key
   * replaces a queued, unsent message with the same key (one after the last message without a key) instead of adding
   * another: a transform superseded before it went out is never sent. */
  void broadcast(const std::string &line, const std::string &bin = {}, uint64_t coalesce = 0);
  void send_to(int client, const std::string &line, const std::string &bin = {}, uint64_t coalesce = 0);
  /* To these clients, plus every ready one when `and_ready` (each once). */
  void send_to(const std::vector<int> &clients, bool and_ready, const std::string &line, const std::string &bin = {},
               uint64_t coalesce = 0);
  /* Many messages at once, in order (each client's queue locked once): what one comparison of the scene sends. */
  struct Out {
    std::string line, bin;
    uint64_t coalesce = 0;
  };
  void send_batch(const std::vector<int> &clients, bool and_ready, const std::vector<Out> &msgs);

  struct Stats {
    uint64_t messages = 0, bytes = 0;  // sent (a message sent to two clients counts twice)
    uint64_t coalesced = 0;            // replaced in a queue before they went out
    uint64_t dropped = 0;              // clients closed for falling too far behind (the queue limit)
    uint64_t accepted = 0;             // clients since start()
    size_t queued_bytes = 0;           // waiting in the queues now
  };
  Stats stats() const;
  /* A client whose queue holds more than this is closed (default 256 MB): a stuck client can't eat the memory. */
  void set_queue_limit(size_t bytes);
  /* Tests: hold every client's queue (nothing is sent) until called with false. */
  void pause_sending_for_test(bool paused);

  struct Impl;

 private:
  std::unique_ptr<Impl> d_;
};

/* A client of the server above: connects to 127.0.0.1:`port`; read() gives the next line (without its newline) and
 * its attachment (the ,"bin":N the server put at the line's end), false on a timeout or a closed connection. */
class LineClient {
 public:
  LineClient() = default;
  ~LineClient();
  LineClient(const LineClient &) = delete;
  LineClient &operator=(const LineClient &) = delete;
  bool connect(int port, int timeout_ms = 2000);
  bool read(std::string &line, std::string &bin, int timeout_ms = 2000);
  void close();
  bool connected() const { return sock_ != -1; }

 private:
  bool fill(int timeout_ms);
  intptr_t sock_ = -1;
  std::string buf_;
};

/* The attachment's size a line announces (its trailing ,"bin":N}), 0 when none. */
size_t line_bin_size(const std::string &line);

}  // namespace bl::net
