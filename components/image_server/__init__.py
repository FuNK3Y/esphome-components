import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.components.image import Image_
from esphome.const import CONF_ID, CONF_PORT, CONF_SOURCE

# Serves any image::Image source as an 8-bit grayscale BMP over plain HTTP
# (GET /image.bmp) so the full-resolution pipeline output can be viewed in a
# browser — the 128px dev LCD is only a thumbnail. Streams straight from the
# source's PSRAM buffer, no copy.

CODEOWNERS = ["@pierre"]
DEPENDENCIES = ["network", "esp32"]
AUTO_LOAD = ["image"]

image_server_ns = cg.esphome_ns.namespace("image_server")
ImageServer = image_server_ns.class_("ImageServer", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(ImageServer),
        cv.Required(CONF_SOURCE): cv.use_id(Image_),
        cv.Optional(CONF_PORT, default=8080): cv.port,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    source = await cg.get_variable(config[CONF_SOURCE])
    cg.add(var.set_source(source))
    cg.add(var.set_port(config[CONF_PORT]))
    esp32.include_builtin_idf_component("esp_http_server")
