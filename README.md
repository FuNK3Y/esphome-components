# esphome-components

Custom [ESPHome](https://esphome.io) external components used by the
`esp-calendair` posters (AtomS3R / ESP32-P4).

## Components

- **`async_http`** — non-blocking HTTP client that buffers large responses in
  PSRAM (used for the OpenRouter image request, whose base64 JPEG body far
  exceeds the stock `http_request` limits).
- **`decoded_image`** — a `runtime_image` shim exposing a fast JPEG → 8-bit
  grayscale decode path (`decode_jpeg_grayscale`) for on-device poster decoding.
- **`it8951`** — fork of the core IT8951 e-paper driver adding staged frame
  presentation: `it8951.load` (transfer the framebuffer to controller RAM
  without displaying), `it8951.refresh` (display from controller RAM without
  transferring) and `is_idle()`. Submitted upstream as
  [esphome/esphome#17527](https://github.com/esphome/esphome/pull/17527);
  this copy carries us until that merges, then it should be dropped.

## Usage

```yaml
external_components:
  - source: github://FuNK3Y/esphome-components@main
    components: [async_http, decoded_image, it8951]
```
