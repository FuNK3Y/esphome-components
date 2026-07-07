#include "decoded_image.h"

#include "esphome/core/log.h"

#include <JPEGDEC.h>
#include <cstring>

namespace esphome {
namespace decoded_image {

static const char *const TAG = "decoded_image";

struct GrayCtx {
  uint8_t *buf;
  int w, h;
};

// Straight 1:1 placement, clamped to the buffer: a JPEG a few pixels larger
// than a fixed-size buffer is top-left-cropped instead of failing.
static int gray_cb(JPEGDRAW *d) {
  auto *ctx = static_cast<GrayCtx *>(d->pUser);
  int w = d->iWidthUsed;
  if (d->x + w > ctx->w)
    w = ctx->w - d->x;
  if (w <= 0)
    return 1;
  for (int row = 0; row < d->iHeight; row++) {
    int ty = d->y + row;
    if (ty >= ctx->h)
      break;
    memcpy(ctx->buf + (size_t) ty * ctx->w + d->x,
           (const uint8_t *) d->pPixels + (size_t) row * d->iWidth, (size_t) w);
  }
  return 1;
}

bool DecodedImage::decode_jpeg_grayscale(uint8_t *jpeg, size_t len) {
  if (this->type_ != image::IMAGE_TYPE_GRAYSCALE) {
    ESP_LOGE(TAG, "fast JPEG path requires type: GRAYSCALE");
    return false;
  }
  // JPEGDEC carries ~23KB of internal buffers: static (like the retired
  // component), NOT on the loop-task stack — that overflows and reboots.
  // Main-loop only + one instance per firmware keeps this safe.
  static JPEGDEC dec;
  if (!dec.openRAM(jpeg, (int) len, gray_cb)) {
    ESP_LOGE(TAG, "jpeg open failed: %d", dec.getLastError());
    return false;
  }
  const int sw = dec.getWidth(), sh = dec.getHeight();
  // Fixed dimensions (resize: in YAML) win inside resize(); without them the
  // buffer is (re)allocated at the JPEG's size.
  if (this->resize(sw, sh) == 0) {
    dec.close();
    ESP_LOGE(TAG, "buffer allocation failed (%dx%d)", sw, sh);
    return false;
  }
  if (sw != this->buffer_width_ || sh != this->buffer_height_) {
    ESP_LOGW(TAG, "jpeg %dx%d vs buffer %dx%d: cropping", sw, sh, this->buffer_width_,
             this->buffer_height_);
  }
  dec.setPixelType(EIGHT_BIT_GRAYSCALE);
  GrayCtx ctx{this->buffer_, this->buffer_width_, this->buffer_height_};
  dec.setUserPointer(&ctx);
  bool ok = dec.decode(0, 0, 0) != 0;
  int last_error = dec.getLastError();
  dec.close();
  if (!ok) {
    ESP_LOGE(TAG, "jpeg decode failed: %d", last_error);
    return false;
  }
  // Publish: same as end_decode() on the generic path.
  this->width_ = this->buffer_width_;
  this->height_ = this->buffer_height_;
  this->data_start_ = this->buffer_;
  return true;
}

}  // namespace decoded_image
}  // namespace esphome
