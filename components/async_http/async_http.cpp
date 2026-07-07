#include "async_http.h"

#include "esphome/core/log.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace async_http {

static const char *const TAG = "async_http";

static esp_http_client_method_t method_from_string(const std::string &m) {
  if (m == "POST")
    return HTTP_METHOD_POST;
  if (m == "PUT")
    return HTTP_METHOD_PUT;
  if (m == "PATCH")
    return HTTP_METHOD_PATCH;
  if (m == "DELETE")
    return HTTP_METHOD_DELETE;
  return HTTP_METHOD_GET;
}

static void worker_entry(void *param) {
  static_cast<AsyncHttp *>(param)->run_worker();
  vTaskDelete(nullptr);
}

void AsyncHttp::request(PendingRequest req) {
  if (queue_.size() >= max_queue_) {
    ESP_LOGW(TAG, "queue full (%u), dropping request to %s", (unsigned) max_queue_, req.url.c_str());
    return;
  }
  queue_.push_back(std::move(req));
  this->maybe_start_();
}

void AsyncHttp::maybe_start_() {
  if (state_.load() != ST_IDLE || queue_.empty())
    return;
  req_ = std::move(queue_.front());
  queue_.pop_front();
  body_.clear();
  error_.clear();
  status_ = 0;
  state_.store(ST_RUNNING);
  // 32KB stack: the TLS handshake (mbedTLS) runs in this task. Proven size.
  if (xTaskCreatePinnedToCore(worker_entry, "async_http", 32 * 1024, this, 1, nullptr, 1) != pdPASS) {
    error_ = "task spawn failed";
    state_.store(ST_FAILED);
  }
}

void AsyncHttp::run_worker() {
  esp_http_client_config_t cfg = {};
  cfg.url = req_.url.c_str();
  cfg.method = method_from_string(req_.method);
  cfg.timeout_ms = (int) timeout_ms_;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    error_ = "HTTP init failed";
    state_.store(ST_FAILED);
    return;
  }
  for (const auto &h : req_.headers)
    esp_http_client_set_header(client, h.first.c_str(), h.second.c_str());

  esp_err_t err = esp_http_client_open(client, req_.body.size());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "connect to %s failed: %s", req_.url.c_str(), esp_err_to_name(err));
    error_ = "connect failed";
    esp_http_client_cleanup(client);
    state_.store(ST_FAILED);
    return;
  }
  if (!req_.body.empty() &&
      esp_http_client_write(client, req_.body.data(), req_.body.size()) < (int) req_.body.size()) {
    error_ = "write failed";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    state_.store(ST_FAILED);
    return;
  }

  int64_t content_length = esp_http_client_fetch_headers(client);  // <=0 if chunked
  status_ = esp_http_client_get_status_code(client);

  size_t cap = max_size_;
  if (content_length > 0) {
    if ((size_t) content_length > max_size_) {
      ESP_LOGE(TAG, "response %lld bytes exceeds max_response_size %u", (long long) content_length,
               (unsigned) max_size_);
      error_ = "response exceeds max_response_size";
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      state_.store(ST_FAILED);
      return;
    }
    cap = (size_t) content_length;
  }

  // With CONFIG_SPIRAM_USE_MALLOC this lands in PSRAM (allocs >16KB spill).
  body_.resize(cap);
  size_t len = 0;
  int r;
  bool overflow = false;
  while ((r = esp_http_client_read(client, &body_[len], cap - len)) > 0) {
    len += r;
    if (len == cap) {
      // Buffer full: EOF is fine, more data is an overflow.
      char probe;
      if (esp_http_client_read(client, &probe, 1) > 0)
        overflow = true;
      break;
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (overflow) {
    ESP_LOGE(TAG, "response exceeds max_response_size %u, truncated", (unsigned) max_size_);
    error_ = "response exceeds max_response_size";
    std::string().swap(body_);
    state_.store(ST_FAILED);
    return;
  }
  body_.resize(len);
  ESP_LOGD(TAG, "HTTP %d, %u bytes from %s", status_, (unsigned) len, req_.url.c_str());
  state_.store(ST_DONE);
}

void AsyncHttp::loop() {
  int s = state_.load();
  if (s == ST_DONE) {
    // Move everything local and go idle BEFORE firing, so the handler (or a
    // queued request) can start the next transfer without racing the members.
    std::string body = std::move(body_);
    std::string().swap(body_);
    int status = status_;
    auto *trig = req_.on_response;
    std::string().swap(req_.body);  // free the request copy too
    state_.store(ST_IDLE);
    if (trig != nullptr)
      trig->trigger(status, body);
  } else if (s == ST_FAILED) {
    std::string error = std::move(error_);
    auto *trig = req_.on_error;
    std::string().swap(req_.body);
    state_.store(ST_IDLE);
    ESP_LOGE(TAG, "%s", error.c_str());
    if (trig != nullptr)
      trig->trigger(error);
  }
  this->maybe_start_();
}

}  // namespace async_http
}  // namespace esphome
