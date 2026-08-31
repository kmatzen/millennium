#!/usr/bin/env python3
"""Validate, preview, explore, and package Millennium narrative content."""

import argparse
import gzip
import hashlib
import itertools
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile


EVENT_RE = re.compile(r"^(key:[0-9*#]|coin|card|hook_up|hook_down|timeout|resume|call_connected|call_ended|default)$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")


class StoryError(ValueError):
    pass


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sign_ed25519(private_key, message, signature):
    """Sign on OpenSSL 3 or Raspberry Pi OS's OpenSSL 1.1.1."""
    result = subprocess.run(
        ["openssl", "pkeyutl", "-sign", "-rawin", "-inkey",
         str(private_key), "-in", str(message), "-out", str(signature)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if result.returncode == 0:
        return
    try:
        from cryptography.hazmat.primitives import serialization
        key = serialization.load_pem_private_key(private_key.read_bytes(), password=None)
        signature.write_bytes(key.sign(message.read_bytes()))
    except Exception as exc:
        raise StoryError("Ed25519 signing requires OpenSSL 3 or python3-cryptography") from exc


def load_story(path):
    try:
        story = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise StoryError(f"cannot read story: {exc}") from exc
    if not isinstance(story, dict):
        raise StoryError("story root must be an object")
    return story


def closed_cycles(story, reachable):
    scenes = story["scenes"]
    terminals = {name for name in reachable if scenes[name].get("ending")}
    escapable = set(terminals)
    changed = True
    while changed:
        changed = False
        for name in reachable - escapable:
            targets = {item["target"] for item in scenes[name].get("transitions", [])}
            if targets & escapable:
                escapable.add(name)
                changed = True
    return sorted(reachable - escapable)


def validate(story, root):
    errors = []
    story_id = story.get("id")
    version = story.get("version")
    entry = story.get("entry")
    scenes = story.get("scenes")
    initial_state = story.get("initial_state", {})
    accessibility = story.get("accessibility", {})
    if not isinstance(story_id, str) or not ID_RE.fullmatch(story_id):
        errors.append("id must be a lowercase content identifier")
    if not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        errors.append("version must use semantic versioning")
    if not isinstance(scenes, dict) or not scenes:
        return errors + ["scenes must be a non-empty object"]
    if (not isinstance(initial_state, dict) or len(initial_state) > 32 or
            any(not isinstance(key, str) or not ID_RE.fullmatch(key) or not isinstance(value, int)
                for key, value in initial_state.items())):
        errors.append("initial_state must contain at most 32 integer variables")
    if entry not in scenes:
        errors.append("entry must name an existing scene")
    if not isinstance(accessibility, dict):
        errors.append("accessibility must be an object")
        accessibility = {}
    repeat_key = accessibility.get("repeat_key", "*")
    if repeat_key not in "0123456789*#" or len(repeat_key) != 1:
        errors.append("accessibility.repeat_key must be one keypad key")
    response_seconds = accessibility.get("minimum_response_seconds", 15)
    if not isinstance(response_seconds, int) or not 5 <= response_seconds <= 300:
        errors.append("accessibility.minimum_response_seconds must be 5..300")
    volume = accessibility.get("volume_percent", 100)
    if not isinstance(volume, int) or not 10 <= volume <= 100:
        errors.append("accessibility.volume_percent must be 10..100")
    for option in ("spoken_instructions", "high_contrast_display"):
        if option in accessibility and not isinstance(accessibility[option], bool):
            errors.append(f"accessibility.{option} must be true or false")

    referenced_media = set()
    for name, scene in scenes.items():
        if not ID_RE.fullmatch(name):
            errors.append(f"scene {name!r} has an invalid identifier")
        display = scene.get("display")
        if (not isinstance(display, list) or len(display) != 2 or
                not all(isinstance(line, str) for line in display)):
            errors.append(f"scene {name}: display must contain exactly two strings")
        elif any(len(line) > 20 for line in display):
            errors.append(f"scene {name}: display text exceeds the 20-character VFD limit")
        elif any("\t" in line or "\n" in line or "\r" in line for line in display):
            errors.append(f"scene {name}: display text cannot contain tabs or newlines")
        audio = scene.get("audio")
        if audio is not None:
            if (not isinstance(audio, str) or Path(audio).name != audio or
                    not re.fullmatch(r"[A-Za-z0-9_-]+\.wav", audio)):
                errors.append(f"scene {name}: audio must be a safe .wav media filename")
            else:
                referenced_media.add(audio)
        if accessibility.get("spoken_instructions", False) and not audio and not scene.get("ending"):
            errors.append(f"scene {name}: spoken instructions require audio for actionable scenes")
        scene_timeout = scene.get("timeout_seconds", response_seconds)
        if not isinstance(scene_timeout, int) or not 1 <= scene_timeout <= 3600:
            errors.append(f"scene {name}: timeout_seconds must be 1..3600")
        call = scene.get("call")
        if call is not None and (not isinstance(call, str) or
                                 (call != "configured" and
                                  not re.fullmatch(r"[0-9+*#]{1,31}", call))):
            errors.append(f"scene {name}: call must be 'configured' or a dialable number")
        transitions = scene.get("transitions", [])
        if not isinstance(transitions, list):
            errors.append(f"scene {name}: transitions must be a list")
            continue
        seen_events = set()
        for transition in transitions:
            if not isinstance(transition, dict):
                errors.append(f"scene {name}: transition must be an object")
                continue
            event = transition.get("event")
            target = transition.get("target")
            if not isinstance(event, str) or not EVENT_RE.fullmatch(event):
                errors.append(f"scene {name}: unsupported event {event!r}")
            condition_key = json.dumps(transition.get("when"), sort_keys=True)
            event_key = (event, condition_key)
            if event_key in seen_events:
                errors.append(f"scene {name}: duplicate event/condition {event!r}")
            seen_events.add(event_key)
            if target not in scenes:
                errors.append(f"scene {name}: missing target {target!r}")
            condition = transition.get("when")
            if condition is not None:
                if (not isinstance(condition, dict) or set(condition) != {"var", "equals"} or
                        not isinstance(condition.get("var"), str) or
                        not isinstance(condition.get("equals"), int)):
                    errors.append(f"scene {name}: invalid transition condition")
            for action in ("set", "increment"):
                values = transition.get(action, {})
                if (not isinstance(values, dict) or len(values) > 1 or
                        any(not isinstance(key, str) or not ID_RE.fullmatch(key) or not isinstance(value, int)
                            for key, value in values.items())):
                    errors.append(f"scene {name}: {action} must contain at most one integer variable")
        if not transitions and not scene.get("ending"):
            errors.append(f"scene {name}: dead end is not marked as an ending")

    media_dir = root / "media"
    present_media = {path.name for path in media_dir.iterdir()} if media_dir.is_dir() else set()
    for filename in sorted(referenced_media - present_media):
        errors.append(f"missing media: media/{filename}")
    for filename in sorted(present_media - referenced_media):
        errors.append(f"unused media: media/{filename}")

    if entry in scenes:
        reachable = set()
        pending = [entry]
        while pending:
            name = pending.pop()
            if name in reachable:
                continue
            reachable.add(name)
            pending.extend(item.get("target") for item in scenes[name].get("transitions", [])
                           if item.get("target") in scenes)
        for name in sorted(set(scenes) - reachable):
            errors.append(f"unreachable scene: {name}")
        trapped = closed_cycles(story, reachable)
        if trapped:
            errors.append("scenes cannot reach an ending: " + ", ".join(trapped))
    return errors


def compile_runtime(story):
    """Compile author-friendly JSON into the daemon's strict tabular format."""
    def field(value):
        return "-" if value == "" else str(value)

    lines = [f"MSTORY\t1\t{story['id']}\t{story['version']}\t{story['entry']}"]
    accessibility = story.get("accessibility", {})
    lines.append("\t".join(str(value) for value in (
        "ACCESS", accessibility.get("repeat_key", "*"),
        accessibility.get("minimum_response_seconds", 15),
        accessibility.get("volume_percent", 100),
        int(bool(accessibility.get("spoken_instructions", False))),
        int(bool(accessibility.get("high_contrast_display", True))))))
    for name, value in sorted(story.get("initial_state", {}).items()):
        lines.append(f"VAR\t{name}\t{value}")
    timeout = int(accessibility.get("minimum_response_seconds", 15))
    for name in sorted(story["scenes"]):
        scene = story["scenes"][name]
        fields = ["SCENE", name, scene["display"][0], scene["display"][1],
                  scene.get("audio", ""), scene.get("ending", ""),
                  str(scene.get("timeout_seconds", timeout)),
                  scene.get("call", "")]
        lines.append("\t".join(field(value) for value in fields))
        for item in scene.get("transitions", []):
            condition = item.get("when", {})
            setting = next(iter(item.get("set", {}).items()), ("", ""))
            increment = next(iter(item.get("increment", {}).items()), ("", ""))
            lines.append("\t".join(field(value) for value in (
                "TRANS", name, item["event"], item["target"],
                condition.get("var", ""), condition.get("equals", ""),
                setting[0], setting[1], increment[0], increment[1])))
    return ("\n".join(lines) + "\n").encode()


def describe(scene_name, scene):
    print(f"\n[{scene_name}]")
    print(f"  DISPLAY: {scene['display'][0]}")
    print(f"           {scene['display'][1]}")
    if scene.get("audio"):
        print(f"  AUDIO:   {scene['audio']}")
    if scene.get("ending"):
        print(f"  ENDING:  {scene['ending']}")


def preview(story):
    current = story["entry"]
    while True:
        scene = story["scenes"][current]
        describe(current, scene)
        transitions = scene.get("transitions", [])
        if not transitions:
            return
        for index, item in enumerate(transitions, 1):
            print(f"  {index}. {item['event']} -> {item['target']}")
        answer = input("Choose event number (q to quit): ").strip()
        if answer.lower() == "q":
            return
        try:
            current = transitions[int(answer) - 1]["target"]
        except (ValueError, IndexError):
            print("Invalid choice; try again.")


def explore_paths(story):
    """Return unique, condition-aware simple paths through a story.

    Persistent condition variables are seeded with every value referenced by
    a transition so return-visit entries are represented without enumerating
    an unbounded number of sessions. State mutations are then applied along
    each path. Keeping paths scene-simple makes authoring output finite even
    when repeat/resume transitions form intentional loops.
    """
    initial = dict(story.get("initial_state", {}))
    condition_values = {}
    for scene in story["scenes"].values():
        for item in scene.get("transitions", []):
            condition = item.get("when", {})
            variable = condition.get("var")
            if variable:
                condition_values.setdefault(variable, {initial.get(variable, 0)}).add(
                    condition.get("equals"))
    variables = sorted(condition_values)
    seeds = [initial]
    if variables:
        seeds = []
        for values in itertools.product(*(sorted(condition_values[name]) for name in variables)):
            state = dict(initial)
            state.update(zip(variables, values))
            seeds.append(state)

    paths = set()

    def walk(name, path, state):
        scene = story["scenes"][name]
        next_path = path + [name]
        if scene.get("ending"):
            paths.add((scene["ending"], tuple(next_path)))
            return
        for item in scene.get("transitions", []):
            if item["target"] in next_path:
                continue
            condition = item.get("when", {})
            if condition and state.get(condition.get("var"), 0) != condition.get("equals"):
                continue
            next_state = dict(state)
            next_state.update(item.get("set", {}))
            for variable, amount in item.get("increment", {}).items():
                next_state[variable] = next_state.get(variable, 0) + amount
            walk(item["target"], next_path, next_state)

    for seed in seeds:
        walk(story["entry"], [], seed)
    return sorted(paths)


def explore(story):
    paths = explore_paths(story)
    by_ending = {}
    for ending, path in paths:
        by_ending.setdefault(ending, []).append(path)
    for ending in sorted(by_ending):
        variants = by_ending[ending]
        representative = min(variants, key=lambda item: (len(item), item))
        print(f"{ending} ({len(variants)} variant(s)): {' -> '.join(representative)}")
    print(f"{len(paths)} unique acyclic path(s) reach {len(by_ending)} ending(s)")


def package(story_path, output, private_key=None, key_id="primary"):
    root = story_path.parent
    story = load_story(story_path)
    errors = validate(story, root)
    if errors:
        raise StoryError("\n".join(errors))
    output.mkdir(parents=True, exist_ok=True)
    identity = f"{story['id']}-{story['version']}"
    archive = output / f"{identity}.tar.gz"
    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary) / identity
        stage.mkdir()
        (stage / "story.json").write_bytes(canonical(story))
        (stage / "story.mst").write_bytes(compile_runtime(story))
        if (root / "media").is_dir():
            (stage / "media").mkdir()
            for source in sorted((root / "media").iterdir()):
                (stage / "media" / source.name).write_bytes(source.read_bytes())
        with archive.open("wb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as bundle:
                    for source in sorted(stage.rglob("*")):
                        info = bundle.gettarinfo(str(source), arcname=str(source.relative_to(stage)))
                        info.uid = info.gid = 0
                        info.uname = info.gname = "root"
                        info.mtime = 0
                        if source.is_file():
                            with source.open("rb") as stream:
                                bundle.addfile(info, stream)
                        else:
                            bundle.addfile(info)
    manifest = {"schema": 1, "id": story["id"], "version": story["version"],
                "key_id": key_id, "bundle": archive.name, "sha256": sha256(archive)}
    manifest_path = output / f"{identity}.manifest.json"
    manifest_path.write_bytes(canonical(manifest))
    if private_key:
        sign_ed25519(private_key, manifest_path, Path(str(manifest_path) + ".sig"))
    print(manifest_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("validate", "preview", "explore", "compile", "package"))
    parser.add_argument("story", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--key-id", default="primary")
    args = parser.parse_args()
    story = load_story(args.story)
    errors = validate(story, args.story.parent)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    if args.command == "validate":
        print(f"OK: {story['id']} {story['version']} ({len(story['scenes'])} scenes)")
    elif args.command == "preview":
        preview(story)
    elif args.command == "explore":
        explore(story)
    elif args.command == "compile":
        if args.output is None:
            parser.error("compile requires --output")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(compile_runtime(story))
        print(args.output)
    elif args.command == "package":
        if args.output is None:
            parser.error("package requires --output")
        package(args.story, args.output, args.private_key, args.key_id)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except StoryError as exc:
        print(f"ERROR: {exc}")
        raise SystemExit(1)
