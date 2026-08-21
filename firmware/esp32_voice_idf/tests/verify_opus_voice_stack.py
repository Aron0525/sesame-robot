#!/usr/bin/env python3
"""Keep the Opus encoder's deep call stack out of TLS-critical internal RAM."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/sesame_voice/voice_controller.cpp"
HEADER = ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h"
STORE_SOURCE = ROOT / "components/sesame_voice/conversation_store.cpp"
STORE_HEADER = (
    ROOT / "components/sesame_voice/include/sesame_voice/conversation_store.h"
)


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    store_source = STORE_SOURCE.read_text(encoding="utf-8")
    store_header = STORE_HEADER.read_text(encoding="utf-8")
    start = source[source.index("esp_err_t VoiceController::start") : source.index(
        "void VoiceController::stop"
    )]
    stop = source[source.index("void VoiceController::stop") : source.index(
        "void VoiceController::outbound_task_entry"
    )]

    # Hardware backtraces show Opus encoding using more than the old 12 KiB.
    # Put a conservative stack in PSRAM so mbedTLS keeps its internal heap.
    assert "kVoiceTaskStackBytes = 32768" in header
    assert "xTaskCreatePinnedToCoreWithCaps(" in start
    assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in start
    assert '"sesame_voice", kVoiceTaskStackBytes' in " ".join(start.split())
    assert "xTaskCreatePinnedToCore(task_entry" not in start
    assert "vTaskDeleteWithCaps(task_)" in stop
    assert "vTaskDeleteWithCaps(nullptr)" in stop
    assert "voice stack free=" in source

    # First-boot Wi-Fi initialization may persist driver defaults in NVS.
    # It must finish on app_main's internal-RAM stack before the PSRAM-backed
    # voice task is created, or disabling the flash cache asserts at runtime.
    assert "gateway_.prepare_network(config_)" in start
    assert start.index("gateway_.prepare_network(config_)") < start.index(
        "xTaskCreatePinnedToCoreWithCaps("
    )

    # A PSRAM-backed task must never call an API that disables the flash
    # cache. The session-ready path therefore hands NVS persistence to a
    # small internal-RAM worker rather than writing flash on the voice stack.
    control = source[source.index("void VoiceController::process_control_json") :]
    assert "save_conversation_id(" not in control
    assert "conversation_store_.enqueue(conversation)" in control
    assert "ConversationStore conversation_store_" in header
    assert "xTaskCreatePinnedToCore(" in store_source
    assert "xTaskCreatePinnedToCoreWithCaps(" not in store_source
    compact_store = " ".join(store_source.split())
    assert (
        "save_conversation_id( request.conversation_id.data())" in compact_store
    )
    assert "kTaskStackBytes = 4096" in store_header
    print(
        "Opus voice task uses a measured 32-KiB PSRAM stack; "
        "flash persistence is isolated on internal RAM"
    )


if __name__ == "__main__":
    main()
