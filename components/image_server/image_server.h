#pragma once

#include "esphome/core/component.h"
#include "esphome/components/image/image.h"

#include <esp_http_server.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace esphome {
namespace image_server {

// Serves the source image::Image as an 8-bit grayscale BMP on
// http://<device>:<port>/image.bmp (also on "/"). GRAYSCALE sources only —
// exactly what the e-paper pipeline produces. The HTTP task reads the buffer
// while the main loop may be rewriting it (decode/dither): worst case is a
// torn preview frame, never a crash — the buffer is allocate-once and stable.
class ImageServer : public Component {
 public:
  void set_source(image::Image *source) { source_ = source; }
  void set_port(uint16_t port) { port_ = port; }

  void setup() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Register an extra raw endpoint (e.g. the source JPEG, for judging what
  // the pipeline does to the model's output). Callable before or after setup.
  void add_raw(const char *path, const char *content_type,
               std::function<std::pair<const uint8_t *, size_t>()> getter);

 protected:
  struct RawEndpoint {
    std::string path;
    std::string type;
    std::function<std::pair<const uint8_t *, size_t>()> get;
  };

  static esp_err_t handler_(httpd_req_t *req);
  static esp_err_t raw_handler_(httpd_req_t *req);
  void register_raw_(RawEndpoint *ep);

  image::Image *source_{nullptr};
  uint16_t port_{8080};
  httpd_handle_t server_{nullptr};
  std::vector<std::unique_ptr<RawEndpoint>> raw_;
};

}  // namespace image_server
}  // namespace esphome
