# esphome-components

Custom [ESPHome](https://esphome.io) external components used by the
`esp-calendair` posters (AtomS3R / ESP32-P4).

## Components

- **`async_http`** — non-blocking HTTP client that buffers large responses in
  PSRAM (used for the OpenRouter image request, whose base64 JPEG body far
  exceeds the stock `http_request` limits).
- **`decoded_image`** — a `runtime_image` shim exposing a fast JPEG → 8-bit
  grayscale decode path (`decode_jpeg_grayscale`) for on-device poster decoding.
- **`image_server`** — serves any `image::Image` source as an 8-bit grayscale
  BMP over plain HTTP (`GET /image.bmp`) for full-resolution browser preview.

## Usage

```yaml
external_components:
  - source: github://FuNK3Y/esphome-components@main
    components: [async_http, decoded_image, image_server]
```
