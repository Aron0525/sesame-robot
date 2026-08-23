from __future__ import annotations

import shutil
import subprocess
import textwrap
import unittest
from pathlib import Path


_DASHBOARD_HTML = (
    Path(__file__).resolve().parents[1]
    / "apps/voice_gateway/src/sesame_voice_gateway/dashboard.html"
)


class DashboardLiveUpdateTest(unittest.TestCase):
    def test_trace_events_update_device_and_turn_cards_without_a_page_reload(self) -> None:
        node = shutil.which("node")
        if node is None:
            self.skipTest("Node.js is required to exercise the embedded dashboard script")

        script = textwrap.dedent(
            """
            const fs = require('node:fs');
            const vm = require('node:vm');

            const html = fs.readFileSync(process.argv[1], 'utf8');
            const match = html.match(/<script>([\\s\\S]*?)<\\/script>/);
            if (!match) throw new Error('dashboard script was not found');
            const source = match[1].replace('loadSnapshot().catch(() => {}).finally(connect);', '');

            const elements = new Map();
            const element = () => ({ dataset: {}, innerHTML: '', textContent: '' });
            const document = {
              getElementById(id) {
                if (!elements.has(id)) elements.set(id, element());
                return elements.get(id);
              },
            };
            const context = { document, console };
            vm.createContext(context);
            vm.runInContext(source + `
              state = { devices: [], turns: [], events: [], debug_content: false };
              applyEvent({
                id: 1,
                timestamp_ms: 1000,
                device_id: 'device-alpha',
                turn_id: null,
                stage: 'wss',
                status: 'connected',
                details: {},
              });
              if (!document.getElementById('devices').innerHTML.includes('device-alpha')) {
                throw new Error('new device was not rendered after a trace event');
              }
              applyEvent({
                id: 2,
                timestamp_ms: 2000,
                device_id: 'device-alpha',
                turn_id: 'turn-alpha',
                stage: 'listen',
                status: 'started',
                details: {},
              });
              if (!document.getElementById('turns').innerHTML.includes('turn-alpha')) {
                throw new Error('new turn was not rendered after a trace event');
              }
              applyEvent({
                id: 3,
                timestamp_ms: 3000,
                device_id: 'device-alpha',
                turn_id: null,
                stage: 'wss',
                status: 'disconnected',
                details: {},
              });
              if (!document.getElementById('devices').innerHTML.includes('OFFLINE')) {
                throw new Error('device disconnect was not rendered after a trace event');
              }
            `, context);
            """
        )
        result = subprocess.run(
            [node, "-e", script, str(_DASHBOARD_HTML)],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr or result.stdout)


if __name__ == "__main__":
    unittest.main()
