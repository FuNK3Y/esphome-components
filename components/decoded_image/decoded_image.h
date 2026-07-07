#pragma once

#include "esphome/components/runtime_image/runtime_image.h"

namespace esphome {
namespace decoded_image {

class DecodedImage : public runtime_image::RuntimeImage {
 public:
  using runtime_image::RuntimeImage::RuntimeImage;

  // Fast path for GRAYSCALE JPEG: JPEGDEC outputs 8-bit gray natively and
  // rows are memcpy'd into the buffer — ~0.7s at 1872x1408 vs ~16s through
  // the generic decoder (RGB8888 + per-pixel Color + float luminance). The
  // stock begin_decode()/feed_data()/end_decode() path stays available for
  // other formats. Main-loop only, like the stock decode path.
  bool decode_jpeg_grayscale(uint8_t *jpeg, size_t len);
};

}  // namespace decoded_image
}  // namespace esphome
