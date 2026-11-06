import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID

vebus_ns = cg.esphome_ns.namespace("vebus")
VebusHub = vebus_ns.class_("VebusHub", cg.Component)

CONF_DE_PIN = "de_pin"
CONF_SHELLY_IP = "shelly_ip"
CONF_BATTERY_CAPACITY = "battery_capacity"
CONF_LOOP_INTERVAL = "loop_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VebusHub),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_DE_PIN): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_SHELLY_IP, default=""): cv.string,
        cv.Optional(CONF_BATTERY_CAPACITY, default=305.0): cv.float_,
        cv.Optional(CONF_LOOP_INTERVAL, default=5000): cv.uint32_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    paren = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart_parent(paren))
    if CONF_DE_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_DE_PIN])
        cg.add(var.set_de_pin(pin))
    cg.add(var.set_shelly_ip(config[CONF_SHELLY_IP]))
    cg.add(var.set_battery_capacity(config[CONF_BATTERY_CAPACITY]))
    cg.add(var.set_loop_interval(config[CONF_LOOP_INTERVAL]))