from esphome import automation
import esphome.codegen as cg
from esphome.components import nfc
import esphome.config_validation as cv
from esphome.const import (
    CONF_ON_FINISHED_WRITE,
    CONF_ON_TAG,
    CONF_ON_TAG_REMOVED,
    CONF_TRIGGER_ID,
)
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

CODEOWNERS = ["@OttoWinter", "@jesserockz"]
AUTO_LOAD = ["binary_sensor", "nfc"]
MULTI_CONF = True

CONF_PN532_ID = "pn532_id"
CONF_RX_GAIN = "rx_gain"
CONF_ACTIVATION_RETRIES = "activation_retries"
CONF_REMOVAL_MISSES = "removal_misses"
CONF_READ_ATTEMPTS = "read_attempts"
CONF_PAGE_RETRIES = "page_retries"

# CIU_RFCfg RxGain codes (PN512/PN532 CIU): 18/23 dB appear twice in the table; use the upper codes
RX_GAIN_CODES = {"18dB": 2, "23dB": 3, "33dB": 4, "38dB": 5, "43dB": 6, "48dB": 7}

pn532_ns = cg.esphome_ns.namespace("pn532")
PN532 = pn532_ns.class_("PN532", cg.PollingComponent)

PN532_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PN532),
        cv.Optional(CONF_ON_TAG): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(nfc.NfcOnTagTrigger),
            }
        ),
        cv.Optional(CONF_ON_FINISHED_WRITE): automation.validate_automation({}),
        cv.Optional(CONF_ON_TAG_REMOVED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(nfc.NfcOnTagTrigger),
            }
        ),
        # --- reliability tuning (local fork); defaults reproduce upstream behaviour ---
        cv.Optional(CONF_RX_GAIN): cv.enum(RX_GAIN_CODES),
        cv.Optional(CONF_ACTIVATION_RETRIES): cv.int_range(min=0, max=254),
        cv.Optional(CONF_REMOVAL_MISSES, default=1): cv.int_range(min=1, max=20),
        cv.Optional(CONF_READ_ATTEMPTS, default=1): cv.int_range(min=1, max=20),
        cv.Optional(CONF_PAGE_RETRIES, default=0): cv.int_range(min=0, max=5),
    }
).extend(cv.polling_component_schema("1s"))


def CONFIG_SCHEMA(conf: ConfigType) -> None:
    if conf:
        raise cv.Invalid(
            "This component has been moved in 1.16, please see the docs for updated "
            "instructions. https://esphome.io/components/binary_sensor/pn532/"
        )


_request_ontag_trigger_slot = cg.slot_counter("PN532_ON_TAG_TRIGGER_COUNT")
_request_ontagremoved_trigger_slot = cg.slot_counter(
    "PN532_ON_TAG_REMOVED_TRIGGER_COUNT"
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_FINISHED_WRITE, "add_on_finished_write_callback"
    ),
)


async def setup_pn532(var: MockObj, config: ConfigType) -> None:
    await cg.register_component(var, config)

    if CONF_RX_GAIN in config:
        cg.add(var.set_rx_gain(config[CONF_RX_GAIN]))
    if CONF_ACTIVATION_RETRIES in config:
        cg.add(var.set_activation_retries(config[CONF_ACTIVATION_RETRIES]))
    cg.add(var.set_removal_misses(config[CONF_REMOVAL_MISSES]))
    cg.add(var.set_read_attempts(config[CONF_READ_ATTEMPTS]))
    cg.add(var.set_page_retries(config[CONF_PAGE_RETRIES]))

    for conf in config.get(CONF_ON_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        _request_ontag_trigger_slot(str(var))
        cg.add(var.register_ontag_trigger(trigger))
        await automation.build_automation(
            trigger, [(cg.std_string, "x"), (nfc.NfcTagConstRef, "tag")], conf
        )

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        _request_ontagremoved_trigger_slot(str(var))
        cg.add(var.register_ontagremoved_trigger(trigger))
        await automation.build_automation(
            trigger, [(cg.std_string, "x"), (nfc.NfcTagConstRef, "tag")], conf
        )

    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)


automation.register_apply_condition(
    "pn532.is_writing",
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(PN532),
        }
    ),
    "is_writing()",
)
