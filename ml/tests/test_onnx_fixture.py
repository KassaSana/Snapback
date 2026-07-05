import os
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURE_PATH = REPO_ROOT / "fixtures" / "model.onnx"


class OnnxFixtureTests(unittest.TestCase):
    def test_fixture_model_exists(self) -> None:
        self.assertTrue(
            FIXTURE_PATH.is_file(),
            f"missing {FIXTURE_PATH}; run: python tools/generate_onnx_fixture.py",
        )
        self.assertGreater(FIXTURE_PATH.stat().st_size, 100)

    def test_fixture_model_has_expected_io(self) -> None:
        try:
            import onnx
        except ImportError:
            self.skipTest("onnx package not installed")

        model = onnx.load(FIXTURE_PATH)
        onnx.checker.check_model(model)

        inputs = {tensor.name: tensor for tensor in model.graph.input}
        outputs = {tensor.name: tensor for tensor in model.graph.output}
        self.assertIn("input", inputs)
        self.assertIn("output", outputs)


if __name__ == "__main__":
    unittest.main()
