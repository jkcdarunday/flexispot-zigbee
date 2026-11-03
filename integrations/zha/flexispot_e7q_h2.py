"""Native ZHA command buttons and restart diagnostics for the Flexispot H2."""

import enum

import zigpy.types as t
from zigpy.zcl.clusters.general import AnalogInput, OnOff
from zigpy.zcl.foundation import ZCLAttributeDef
from zigpy.zcl import ReportingConfig
from zhaquirks.clusters import CustomCluster
from zha.application import EntityPlatform
from zha.application.platforms.sensor.const import SensorDeviceClass, SensorStateClass

try:
    # Current ZHA versions keep builders in zha-quirks.
    from zhaquirks.builder import EntityType, QuirkBuilder
except ImportError:
    # Earlier Home Assistant versions used zigpy's v2 quirk builder.
    from zigpy.quirks.v2 import EntityType, QuirkBuilder


def hide_original_control(entity):
    """Suppress automatic light/switch entities while retaining our buttons."""
    return not (getattr(entity, "translation_key", None) or "").startswith("flexispot_")


CONTROLS = (
    (1, "stand", "Stand"),
    (2, "sit", "Sit"),
    (3, "preset_1", "Preset 1"),
    (4, "preset_2", "Preset 2"),
    (5, "up", "Up"),
    (6, "down", "Down"),
    (7, "memory", "Memory"),
    (8, "release", "Release keys"),
)

builder = QuirkBuilder("JKCD", "Flexispot-E7Q-H2")
for endpoint, key, name in CONTROLS:
    builder.prevent_default_entity_creation(
        endpoint_id=endpoint,
        cluster_id=OnOff.cluster_id,
        function=hide_original_control,
    ).command_button(
        command_name=OnOff.ServerCommandDefs.on.name,
        cluster_id=OnOff.cluster_id,
        endpoint_id=endpoint,
        entity_type=EntityType.CONFIG if key == "memory" else EntityType.STANDARD,
        initially_disabled=key == "memory",
        unique_id_suffix=f"flexispot_{key}",
        translation_key=f"flexispot_{key}",
        fallback_name=name,
    )


class ResetReason(enum.IntEnum):
    unknown = 0
    power_on = 1
    external_reset = 2
    software = 3
    panic = 4
    interrupt_watchdog = 5
    task_watchdog = 6
    watchdog = 7
    deep_sleep = 8
    brownout = 9
    sdio = 10
    usb = 11
    jtag = 12
    efuse = 13
    power_glitch = 14
    cpu_lockup = 15


class DiagnosticAnalogInput(CustomCluster, AnalogInput):
    class AttributeDefs(AnalogInput.AttributeDefs):
        boot_count = ZCLAttributeDef(id=0xf000, type=t.uint32_t, access="rp")
        reset_reason = ZCLAttributeDef(id=0xf001, type=t.enum8, access="rp")


def boot_count_or_unknown(value):
    return None if value == 0 else value


builder.replaces(DiagnosticAnalogInput, endpoint_id=10)
builder.prevent_default_entity_creation(
    endpoint_id=10, cluster_id=AnalogInput.cluster_id, function=hide_original_control,
)
builder.sensor(
    attribute_name="present_value", cluster_id=AnalogInput.cluster_id, endpoint_id=10,
    unit="s", device_class=SensorDeviceClass.DURATION,
    reporting_config=ReportingConfig(min_interval=30, max_interval=60, reportable_change=30),
    state_class=SensorStateClass.MEASUREMENT, suggested_display_precision=0,
    entity_type=EntityType.DIAGNOSTIC, unique_id_suffix="flexispot_uptime",
    translation_key="flexispot_uptime", fallback_name="Uptime",
).sensor(
    attribute_name="boot_count", cluster_id=AnalogInput.cluster_id, endpoint_id=10,
    attribute_converter=boot_count_or_unknown,
    reporting_config=ReportingConfig(min_interval=30, max_interval=60, reportable_change=1),
    state_class=SensorStateClass.TOTAL_INCREASING, suggested_display_precision=0,
    entity_type=EntityType.DIAGNOSTIC, unique_id_suffix="flexispot_boot_count",
    translation_key="flexispot_boot_count", fallback_name="Boot count",
).enum(
    attribute_name="reset_reason", cluster_id=AnalogInput.cluster_id, endpoint_id=10,
    enum_class=ResetReason, entity_platform=EntityPlatform.SENSOR,
    reporting_config=ReportingConfig(min_interval=30, max_interval=60, reportable_change=1),
    entity_type=EntityType.DIAGNOSTIC, unique_id_suffix="flexispot_reset_reason",
    translation_key="flexispot_reset_reason", fallback_name="Last restart reason",
)

QUIRK = builder.add_to_registry()
