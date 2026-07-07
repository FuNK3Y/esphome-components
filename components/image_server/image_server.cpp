#include "image_server.h"

#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace image_server {

static const char *const TAG = "image_server";

static inline void put32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

// 8-bit grayscale BMP, top-down (negative biHeight), 256-entry gray palette.
// Rows must be 4-byte aligned; when the width already is (1872 = 4*468), the
// pixel data streams straight from the source buffer in large chunks.
esp_err_t ImageServer::handler_(httpd_req_t *req) {
  auto *self = static_cast<ImageServer *>(req->user_ctx);
  image::Image *img = self->source_;
  const uint8_t *data = img != nullptr ? img->get_data_start() : nullptr;
  const int w = img != nullptr ? img->get_width() : 0;
  const int h = img != nullptr ? img->get_height() : 0;
  if (data == nullptr || w <= 0 || h <= 0 || img->get_type() != image::IMAGE_TYPE_GRAYSCALE) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no grayscale image available yet");
    return ESP_OK;
  }

  const int stride = (w + 3) & ~3;
  const uint32_t pix_bytes = (uint32_t) stride * h;
  const uint32_t data_offset = 14 + 40 + 256 * 4;

  uint8_t head[14 + 40];
  memset(head, 0, sizeof(head));
  head[0] = 'B';
  head[1] = 'M';
  put32(head + 2, data_offset + pix_bytes);  // file size
  put32(head + 10, data_offset);             // pixel data offset
  put32(head + 14, 40);                      // BITMAPINFOHEADER size
  put32(head + 18, (uint32_t) w);
  put32(head + 22, (uint32_t) (-h));  // negative height = top-down rows
  head[26] = 1;                       // planes
  head[28] = 8;                       // bits per pixel
  put32(head + 34, pix_bytes);
  put32(head + 46, 256);  // palette entries used

  httpd_resp_set_type(req, "image/bmp");
  if (httpd_resp_send_chunk(req, (const char *) head, sizeof(head)) != ESP_OK)
    return ESP_FAIL;

  uint8_t pal[256 * 4];
  for (int i = 0; i < 256; i++) {
    pal[4 * i + 0] = (uint8_t) i;  // B
    pal[4 * i + 1] = (uint8_t) i;  // G
    pal[4 * i + 2] = (uint8_t) i;  // R
    pal[4 * i + 3] = 0;
  }
  if (httpd_resp_send_chunk(req, (const char *) pal, sizeof(pal)) != ESP_OK)
    return ESP_FAIL;

  if (stride == w) {
    // Fast path: stream the buffer as-is in large slices.
    const size_t CHUNK = 32 * 1024;
    size_t remaining = (size_t) w * h, pos = 0;
    while (remaining > 0) {
      size_t n = remaining > CHUNK ? CHUNK : remaining;
      if (httpd_resp_send_chunk(req, (const char *) data + pos, n) != ESP_OK)
        return ESP_FAIL;
      pos += n;
      remaining -= n;
    }
  } else {
    // Row-by-row with padding for odd widths.
    static const uint8_t PAD[3] = {0, 0, 0};
    for (int y = 0; y < h; y++) {
      if (httpd_resp_send_chunk(req, (const char *) data + (size_t) y * w, w) != ESP_OK)
        return ESP_FAIL;
      if (httpd_resp_send_chunk(req, (const char *) PAD, stride - w) != ESP_OK)
        return ESP_FAIL;
    }
  }
  return httpd_resp_send_chunk(req, nullptr, 0);  // end of chunked response
}

esp_err_t ImageServer::raw_handler_(httpd_req_t *req) {
  auto *ep = static_cast<RawEndpoint *>(req->user_ctx);
  auto data = ep->get();
  if (data.first == nullptr || data.second == 0) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not available yet");
    return ESP_OK;
  }
  httpd_resp_set_type(req, ep->type.c_str());
  const size_t CHUNK = 32 * 1024;
  size_t pos = 0;
  while (pos < data.second) {
    size_t n = data.second - pos > CHUNK ? CHUNK : data.second - pos;
    if (httpd_resp_send_chunk(req, (const char *) data.first + pos, n) != ESP_OK)
      return ESP_FAIL;
    pos += n;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

void ImageServer::add_raw(const char *path, const char *content_type,
                          std::function<std::pair<const uint8_t *, size_t>()> getter) {
  raw_.emplace_back(new RawEndpoint{path, content_type, std::move(getter)});
  if (server_ != nullptr)
    register_raw_(raw_.back().get());
}

void ImageServer::register_raw_(RawEndpoint *ep) {
  httpd_uri_t uri = {};
  uri.uri = ep->path.c_str();
  uri.method = HTTP_GET;
  uri.handler = raw_handler_;
  uri.user_ctx = ep;
  httpd_register_uri_handler(server_, &uri);
}

void ImageServer::setup() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = port_;
  cfg.stack_size = 8192;
  cfg.max_uri_handlers = 4;
  if (httpd_start(&server_, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start on port %u failed", port_);
    this->mark_failed();
    return;
  }
  httpd_uri_t uri = {};
  uri.method = HTTP_GET;
  uri.handler = handler_;
  uri.user_ctx = this;
  uri.uri = "/image.bmp";
  httpd_register_uri_handler(server_, &uri);
  uri.uri = "/";
  httpd_register_uri_handler(server_, &uri);
  for (auto &ep : raw_)
    register_raw_(ep.get());  // endpoints added before setup()
  ESP_LOGCONFIG(TAG, "serving image on http://<device>:%u/image.bmp", port_);
}

}  // namespace image_server
}  // namespace esphome
