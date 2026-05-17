import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "feel_spec" / "fixtures.json"


class FeelFixtureTaxonomyTests(unittest.TestCase):
    def test_required_feel_scenarios_exist(self) -> None:
        payload = json.loads(FIXTURES.read_text())
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(
            set(payload["families"]["feel-spec"]),
            {
                "run_stop_turn",
                "jump_short_vs_full",
                "coyote_window",
                "jump_buffer",
                "air_steering",
                "dash_hitstop",
                "long_jump",
                "climb_drain",
                "climb_exhaustion",
            },
        )
        self.assertEqual(payload["families"]["motor-regression"], ["slope", "ledge", "platform", "camera"])

    def test_spec_windows_are_locked(self) -> None:
        payload = json.loads(FIXTURES.read_text())
        self.assertEqual(payload["scenarios"]["coyote_window"]["accept_seconds"], 0.15)
        self.assertEqual(payload["scenarios"]["jump_buffer"]["accept_seconds"], 0.10)
        self.assertEqual(payload["scenarios"]["dash_hitstop"]["freeze_seconds"], 0.05)


if __name__ == "__main__":
    unittest.main()
