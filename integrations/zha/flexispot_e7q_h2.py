"""Native ZHA command buttons for the existing Flexispot ESP32-H2 firmware."""

from zigpy.zcl.clusters.general import OnOff

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

QUIRK = builder.add_to_registry()
