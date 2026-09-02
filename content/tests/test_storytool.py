#!/usr/bin/env python3
import copy
import contextlib
import importlib.util
import io
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

    def test_explore_is_unique_and_honors_persistent_conditions(self):
        paths = TOOL.explore_paths(self.story)
        self.assertEqual(len(paths), len(set(paths)))
        self.assertEqual(
            {ending for ending, unused_path in paths},
            {"missed-call", "mara-left-the-house", "mara-answered-herself",
             "call-held", "the-loop-is-closed", "the-voice-crossed",
             "operator-message-waiting", "operator-voicemail-leave",
             "operator-voicemail-answer"})
        for ending, path in paths:
            if "send_leave" in path and ending in {
                    "mara-left-the-house", "mara-answered-herself"}:
                self.assertEqual(ending, "mara-left-the-house")
            if "send_answer" in path and ending in {
                    "mara-left-the-house", "mara-answered-herself"}:
                self.assertEqual(ending, "mara-answered-herself")

    def test_explore_prints_one_representative_per_ending(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            TOOL.explore(self.story)
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 10)
        self.assertEqual(sum(" variant(s)): " in line for line in lines), 9)
        self.assertIn("2981 unique acyclic path(s) reach 9 ending(s)", lines[-1])

    def test_callback_is_validated_compiled_and_explored(self):
        story = copy.deepcopy(self.story)
        story["scenes"]["leave_result"]["callback"] = {
            "after_seconds": 60, "target": "return_leave"}
        self.assertEqual(TOOL.validate(story, STORY_PATH.parent), [])
        runtime = TOOL.compile_runtime(story).decode()
        self.assertTrue(runtime.startswith("MSTORY\t2\t"))
        self.assertIn("CALLBACK\tleave_result\t60\treturn_leave\n", runtime)
        self.assertTrue(any("return_leave" in path for unused_ending, path
                            in TOOL.explore_paths(story)))

    def test_callback_rejects_bad_delay_or_target(self):
        story = copy.deepcopy(self.story)
        story["scenes"]["leave_result"]["callback"] = {
            "after_seconds": 0, "target": "absent"}
        errors = TOOL.validate(story, STORY_PATH.parent)
        self.assertTrue(any("callback must name a target" in item for item in errors))

    def test_ring_scene_is_compiled_and_must_be_boolean(self):
        story = copy.deepcopy(self.story)
        story["scenes"]["invitation"]["ring"] = True
        self.assertEqual(TOOL.validate(story, STORY_PATH.parent), [])
        self.assertIn("SCENE\tinvitation\tTHIS CALL IS FOR\tYOU. LIFT TO ANSWER\t"
                      "invitation.wav\t-\t35\t-\t1\n",
                      TOOL.compile_runtime(story).decode())
        story["scenes"]["invitation"]["ring"] = 1
        self.assertTrue(any("ring must be true or false" in item
                            for item in TOOL.validate(story, STORY_PATH.parent)))


if __name__ == "__main__":
    unittest.main()
