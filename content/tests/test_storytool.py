#!/usr/bin/env python3
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


TOOL_PATH = Path(__file__).parents[1] / "storytool.py"
SPEC = importlib.util.spec_from_file_location("storytool", TOOL_PATH)
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
STORY_PATH = Path(__file__).parents[1] / "stories" / "last_line" / "story.json"


class StoryToolTests(unittest.TestCase):
    def setUp(self):
        self.story = json.loads(STORY_PATH.read_text())

    def test_example_validates(self):
        self.assertEqual(TOOL.validate(self.story, STORY_PATH.parent), [])

    def test_unreachable_and_missing_target_are_rejected(self):
        story = copy.deepcopy(self.story)
        story["scenes"]["operator_intro"]["transitions"][0]["target"] = "absent"
        story["scenes"]["orphan"] = {"display": ["LOST", ""],
                                       "ending": "unused", "transitions": []}
        errors = TOOL.validate(story, STORY_PATH.parent)
        self.assertTrue(any("missing target" in item for item in errors))
        self.assertTrue(any("unreachable scene: orphan" in item for item in errors))

    def test_closed_cycle_is_rejected(self):
        story = {"id": "loop", "version": "1.0.0", "entry": "a",
                 "scenes": {
                     "a": {"display": ["A", ""], "transitions": [{"event": "key:1", "target": "b"}]},
                     "b": {"display": ["B", ""], "transitions": [{"event": "key:1", "target": "a"}]}}}
        errors = TOOL.validate(story, Path("/nonexistent"))
        self.assertTrue(any("cannot reach an ending" in item for item in errors))

    def test_unsigned_package_has_verified_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            TOOL.package(STORY_PATH, output)
            manifest = json.loads(next(output.glob("*.manifest.json")).read_text())
            archive = output / manifest["bundle"]
            self.assertEqual(manifest["sha256"], TOOL.sha256(archive))

    def test_packages_are_reproducible_and_include_runtime_form(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            TOOL.package(STORY_PATH, Path(first))
            TOOL.package(STORY_PATH, Path(second))
            archive_a = next(Path(first).glob("*.tar.gz"))
            archive_b = next(Path(second).glob("*.tar.gz"))
            self.assertEqual(archive_a.read_bytes(), archive_b.read_bytes())
            import tarfile
            with tarfile.open(archive_a) as bundle:
                self.assertIn("story.mst", bundle.getnames())


if __name__ == "__main__":
    unittest.main()
