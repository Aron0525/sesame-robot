# Console real-time rendering verification

## Baseline failure
- Command: `uv run pytest -q tests/test_console_render_budget.py` before the rendering change.
- Literal result: `AssertionError: 'const kRenderedTimelineEvents = 80;' not found` (exit 1).

## Final validation
- Command: `uv run pytest -q tests/test_console_render_budget.py tests/test_console_routes.py`.
- Literal result: `5 passed` (exit 0).
- Live HTTPS command: `curl --insecure https://127.0.0.1:8765/console`.
- Literal result includes `const kRenderedTimelineEvents = 80;`, `const renderEvents = events.slice(0, kRenderedTimelineEvents);`, and `function scheduleRender()`; captured at `/Users/mac/Documents/sesame robot/output/20260808-console-realtime-render-fix/live-console.html`.

## Changed behavior
- Incoming SSE snapshot and trace events request one animation-frame render instead of rendering directly for every event.
- The visible timeline is limited to the latest 80 records; device, current turn, metrics, controls, and latest response plan remain live.
- Button/input enable state updates only when the device online state changes.

## Rollback
- Script: `/Users/mac/Documents/sesame robot/output/20260808-console-realtime-render-fix/rollback.sh`.
- Dry-run was syntax-checked with `bash -n` and executed as `rollback.sh --dry-run` (exit 0).
