import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_ID, CONF_METHOD, CONF_TIMEOUT, CONF_URL

# Generic non-blocking HTTP(S) client: the request runs in a background
# FreeRTOS task (TLS included) while the main loop keeps running — display,
# API and OTA stay responsive. The action returns immediately; on_response /
# on_error fire from the main loop once the transfer finishes, so downstream
# actions (LVGL etc.) are thread-safe. One request in flight at a time;
# further requests queue FIFO up to max_queue, beyond which they are dropped
# with a warning. Mind that queued requests DO run eventually — for paid
# APIs, a small max_queue is the spam guard.
#
# Unlike the stock http_request action this trades streaming access for
# simplicity: the whole response body is buffered (PSRAM via std::string —
# needs CONFIG_SPIRAM_USE_MALLOC) and handed to on_response as `body`,
# with the HTTP status as `status`.

DEPENDENCIES = ["network", "esp32"]

CONF_REQUEST_HEADERS = "request_headers"
CONF_BODY = "body"
CONF_MAX_RESPONSE_SIZE = "max_response_size"
CONF_MAX_QUEUE = "max_queue"
CONF_ON_RESPONSE = "on_response"
CONF_ON_ERROR = "on_error"

async_http_ns = cg.esphome_ns.namespace("async_http")
AsyncHttp = async_http_ns.class_("AsyncHttp", cg.Component)
RequestAction = async_http_ns.class_("RequestAction", automation.Action)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AsyncHttp),
        # Covers the full server-side wait (headers may take minutes for
        # generation-style APIs) and the between-reads inactivity budget.
        cv.Optional(CONF_TIMEOUT, default="180s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MAX_RESPONSE_SIZE, default="1MB"): cv.validate_bytes,
        # Pending requests waiting behind the in-flight one (the total
        # admitted backlog). Keep small when the target API costs money.
        cv.Optional(CONF_MAX_QUEUE, default=4): cv.int_range(min=1, max=16),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT]))
    cg.add(var.set_max_response_size(config[CONF_MAX_RESPONSE_SIZE]))
    cg.add(var.set_max_queue(config[CONF_MAX_QUEUE]))
    esp32.include_builtin_idf_component("esp_http_client")
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)


ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(AsyncHttp),
        cv.Required(CONF_URL): cv.templatable(cv.url),
        cv.Optional(CONF_METHOD, default="GET"): cv.one_of(
            "GET", "POST", "PUT", "PATCH", "DELETE", upper=True
        ),
        cv.Optional(CONF_REQUEST_HEADERS): cv.Schema(
            {cv.string: cv.templatable(cv.string)}
        ),
        cv.Optional(CONF_BODY): cv.templatable(cv.string),
        cv.Optional(CONF_ON_RESPONSE): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_ERROR): automation.validate_automation(single=True),
    }
)


@automation.register_action("async_http.request", RequestAction, ACTION_SCHEMA)
async def request_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    cg.add(var.set_method(config[CONF_METHOD]))
    cg.add(var.set_url(await cg.templatable(config[CONF_URL], args, cg.std_string)))
    if CONF_BODY in config:
        cg.add(var.set_body(await cg.templatable(config[CONF_BODY], args, cg.std_string)))
    for key, value in config.get(CONF_REQUEST_HEADERS, {}).items():
        cg.add(var.add_header(key, await cg.templatable(value, args, cg.std_string)))
    if on_response := config.get(CONF_ON_RESPONSE):
        await automation.build_automation(
            var.get_response_trigger(),
            [(cg.int_, "status"), (cg.std_string_ref, "body")],
            on_response,
        )
    if on_error := config.get(CONF_ON_ERROR):
        await automation.build_automation(
            var.get_error_trigger(), [(cg.std_string, "error")], on_error
        )
    return var
