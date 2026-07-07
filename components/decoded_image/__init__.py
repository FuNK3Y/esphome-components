import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.image import CONF_TRANSPARENCY, add_metadata
from esphome.components.runtime_image import (
    RuntimeImage,
    process_runtime_image_config,
    runtime_image_schema,
    validate_runtime_image_settings,
)
from esphome.const import CONF_ID, CONF_TYPE

# Declares an esphome::runtime_image::RuntimeImage from YAML. The stock
# runtime_image component is codegen-only (consumed by online_image); this
# shim exposes it so lambdas can drive the decoder themselves:
#   id(img).begin_decode(len); id(img).feed_data(buf, len); id(img).end_decode();
# Schema, validation and construction all reuse the stock runtime_image
# helpers. The thin DecodedImage subclass adds decode_jpeg_grayscale(buf, len)
# — a fast GRAYSCALE JPEG path (~20x quicker than the generic decoder).

AUTO_LOAD = ["image", "runtime_image"]
MULTI_CONF = True

decoded_image_ns = cg.esphome_ns.namespace("decoded_image")
DecodedImage = decoded_image_ns.class_("DecodedImage", RuntimeImage)

CONFIG_SCHEMA = cv.All(
    runtime_image_schema(DecodedImage), validate_runtime_image_settings
)


async def to_code(config):
    settings = await process_runtime_image_config(config)
    # Consumers like LVGL resolve image dimensions/format at codegen time.
    add_metadata(
        config[CONF_ID],
        settings.width,
        settings.height,
        config[CONF_TYPE],
        config[CONF_TRANSPARENCY],
    )
    cg.new_Pvariable(
        config[CONF_ID],
        settings.format_enum,
        settings.image_type_enum,
        settings.transparent,
        settings.placeholder or cg.nullptr,
        settings.byte_order_big_endian,
        settings.width,
        settings.height,
    )
