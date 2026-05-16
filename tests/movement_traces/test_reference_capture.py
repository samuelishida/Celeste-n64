import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "reference_capture" / "reference_capture.py"


class ReferenceCaptureTests(unittest.TestCase):
    def run_tool(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(TOOL), *args],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

    def test_committed_traces_compare_cleanly(self) -> None:
        result = self.run_tool("compare")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("matched 7 traces", result.stdout)

    def test_regenerate_is_deterministic_and_compare_detects_drift(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace_dir = Path(tmp)
            first = self.run_tool("regenerate", "--trace-dir", str(trace_dir))
            self.assertEqual(first.returncode, 0, first.stderr)
            snapshot = {path.name: path.read_text() for path in trace_dir.glob("*.json")}

            second = self.run_tool("regenerate", "--trace-dir", str(trace_dir))
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(snapshot, {path.name: path.read_text() for path in trace_dir.glob("*.json")})

            clean = self.run_tool("compare", "--trace-dir", str(trace_dir))
            self.assertEqual(clean.returncode, 0, clean.stderr)

            (trace_dir / "jump_full_hold.json").write_text("{}\n")
            drift = self.run_tool("compare", "--trace-dir", str(trace_dir))
            self.assertEqual(drift.returncode, 1)
            self.assertIn("jump_full_hold.json", drift.stderr)


if __name__ == "__main__":
    unittest.main()
