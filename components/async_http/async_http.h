#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <atomic>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace async_http {

struct PendingRequest {
  std::string method;
  std::string url;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  // The response body is a plain std::string — deliberately. Megabyte-scale
  // bodies rely on CONFIG_SPIRAM_USE_MALLOC (set in the device YAML) to land
  // in PSRAM; a manual RAMAllocator buffer was considered and rejected: it
  // trades a benign, standard sdkconfig flag for hand-managed lifetime on
  // every error path and a clunkier (ptr, len) lambda API.
  Trigger<int, std::string &> *on_response{nullptr};
  Trigger<std::string> *on_error{nullptr};
};

// One request in flight at a time; further requests queue FIFO (up to
// max_queue, then drop + warn). The worker task owns req_/body_/status_/
// error_ strictly between state RUNNING and the DONE/FAILED store; loop()
// and the queue are main-loop only, so no locking is needed.
class AsyncHttp : public Component {
 public:
  void loop() override;
  void set_timeout_ms(uint32_t t) { timeout_ms_ = t; }
  void set_max_response_size(size_t s) { max_size_ = s; }
  void set_max_queue(size_t n) { max_queue_ = n; }

  // Copies everything it needs and returns immediately: starts the worker if
  // idle, otherwise queues (FIFO). A full queue drops the request with a
  // warning. Main-loop only.
  void request(PendingRequest req);

  void run_worker();  // task entry, not for external use

 protected:
  enum State : int { ST_IDLE, ST_RUNNING, ST_DONE, ST_FAILED };

  void maybe_start_();  // pop the next queued request if idle

  std::atomic<int> state_{ST_IDLE};
  uint32_t timeout_ms_{180000};
  size_t max_size_{1024 * 1024};
  size_t max_queue_{4};

  std::deque<PendingRequest> queue_;
  PendingRequest req_;
  std::string body_;
  int status_{0};
  std::string error_;
};

template<typename... Ts> class RequestAction : public Action<Ts...> {
 public:
  RequestAction(AsyncHttp *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, url)
  TEMPLATABLE_VALUE(std::string, body)
  void set_method(const std::string &m) { method_ = m; }
  void add_header(const char *key, TemplatableValue<std::string, Ts...> value) {
    headers_.emplace_back(key, std::move(value));
  }
  Trigger<int, std::string &> *get_response_trigger() { return &response_trigger_; }
  Trigger<std::string> *get_error_trigger() { return &error_trigger_; }

  // Kicks off (or queues) the transfer and returns; the action chain
  // continues immediately. Completion is signaled via the triggers, not
  // play_next_.
  void play(const Ts &...x) override {
    PendingRequest r;
    r.method = method_;
    r.url = this->url_.value(x...);
    r.body = this->body_.value(x...);
    r.headers.reserve(headers_.size());
    for (auto &h : headers_)
      r.headers.emplace_back(h.first, h.second.value(x...));
    r.on_response = &response_trigger_;
    r.on_error = &error_trigger_;
    parent_->request(std::move(r));
  }

 protected:
  AsyncHttp *parent_;
  std::string method_{"GET"};
  std::vector<std::pair<const char *, TemplatableValue<std::string, Ts...>>> headers_;
  Trigger<int, std::string &> response_trigger_;
  Trigger<std::string> error_trigger_;
};

}  // namespace async_http
}  // namespace esphome
