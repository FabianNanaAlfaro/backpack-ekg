from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PublicContractTests(unittest.TestCase):
    def test_private_artifact_extensions_are_not_tracked_in_tree(self) -> None:
        blocked = {".csv", ".dat", ".cal", ".3d", ".pbix", ".edf", ".mat", ".h5", ".mp4", ".mov", ".avi", ".zip"}
        found = [path for path in ROOT.rglob("*") if path.is_file() and path.suffix.lower() in blocked and ".git" not in path.parts]
        self.assertEqual(found, [], f"private/generated artifacts present: {found}")

    def test_core_public_artifacts_exist(self) -> None:
        expected = [
            ROOT / "README.md",
            ROOT / "firmware" / "backpack_ekg" / "backpack_ekg.ino",
            ROOT / "firmware" / "backpack_ekg" / "BpmEstimator.h",
            ROOT / "analysis" / "bpm_estimator.py",
            ROOT / "docs" / "algorithm.md",
        ]
        for path in expected:
            self.assertTrue(path.is_file(), path)

    def test_site_image_references_have_local_assets(self) -> None:
        html = (ROOT / "site" / "index.html").read_text(encoding="utf-8")
        references = set(re.findall(r'(?:src|href)="(assets/[^"#]+)', html))
        for reference in references:
            self.assertTrue((ROOT / reference).is_file(), reference)
            self.assertTrue((ROOT / "site" / reference).is_file(), reference)


if __name__ == "__main__":
    unittest.main()
