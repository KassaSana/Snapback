import json
import os
import tempfile
import unittest

from ml.export_onnx import is_majority_stub, try_export_onnx


class ExportOnnxTests(unittest.TestCase):
    def test_module_imports(self) -> None:
        from ml import export_onnx

        self.assertTrue(callable(export_onnx.main))
        self.assertTrue(callable(export_onnx.try_export_onnx))

    def test_is_majority_stub(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            majority_path = os.path.join(tmp, "model.json")
            with open(majority_path, "w", encoding="utf-8") as handle:
                json.dump({"type": "majority", "label": 1}, handle)
            self.assertTrue(is_majority_stub(majority_path))

    def test_try_export_skips_majority(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            majority_path = os.path.join(tmp, "model.json")
            with open(majority_path, "w", encoding="utf-8") as handle:
                json.dump({"type": "majority", "label": 1}, handle)
            exported, message = try_export_onnx(majority_path, os.path.join(tmp, "model.onnx"))
            self.assertFalse(exported)
            self.assertIn("majority", message)


if __name__ == "__main__":
    unittest.main()
