"""Pure verifier fixture/output-policy tests; never launch VibeRa or providers.

Actual MCP qualification is verify-mixer-effects-mcp.py against an exact binary.
These small tests do not substitute for its original real application scenarios.
"""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mixer_effects_verifier", ROOT / "scripts/verify-mixer-effects-mcp.py")
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class MixerEffectsVerifierTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="effects-verifier-unit-", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name).resolve()

    def base(self, *, version=19, first=43, existing_plugin=False):
        # Deliberately small unit-only XML, not advertised as a valid DAW project.
        # The actual application regression starts from its own complete saved file.
        root = ET.Element("VIBERA", version=str(version), nextId=str(first))
        channels = ET.SubElement(root, "CHANNELS")
        channel = ET.SubElement(channels, "CHANNEL", id="42")
        modulation = ET.SubElement(ET.SubElement(channel, "SAMPLER"), "MODULATION")
        for target in ("volume", "pan", "cutoff", "resonance", "pitch"):
            ET.SubElement(modulation, "TARGET", id=target)
        mixer = ET.SubElement(root, "MIXER")
        insert = ET.SubElement(mixer, "INSERT", index="7")
        if existing_plugin:
            ET.SubElement(insert, "PLUGIN", id="40")
        path = self.folder / "unit-base.vibra"
        ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)
        return path

    def test_fixture_allocates_from_saved_next_id_and_preserves_base(self):
        base = self.base(first=208)
        before = base.read_bytes()
        missing = self.folder / "does not exist.vst3"
        tree, identities = verifier.missing_fixture(base, missing, 7, 19)
        self.assertEqual(identities, list(range(208, 224)))
        self.assertEqual(base.read_bytes(), before)
        self.assertEqual([node.get("id") for node in tree.findall("./CHANNELS/CHANNEL/SAMPLER/MODULATION/TARGET")],
                         ["volume", "pan", "cutoff", "resonance", "pitch"])
        self.assertFalse(missing.exists())
        self.assertEqual(tree.getroot().get("nextId"), "224")
        plugins = tree.findall("./MIXER/INSERT/PLUGIN")
        self.assertEqual(len(plugins), 16)
        self.assertEqual([int(plugin.get("id")) for plugin in plugins], identities)
        for index, plugin in enumerate(plugins):
            self.assertEqual(plugin.get("state"), "1.A." if index % 2 else "1...")
            self.assertEqual(plugin.get("instrument"), "0")
            self.assertEqual([node.tag for node in plugin], ["AUTOMATION_BINDINGS"])
            descriptor = ET.fromstring(plugin.get("descriptor"))
            self.assertEqual(descriptor.get("file"), str(missing))
            self.assertEqual(descriptor.get("format"), "VST3")

    def test_fixture_refuses_existing_binary_instead_of_running_provider(self):
        missing = self.folder / "unexpected.vst3"
        missing.write_bytes(b"Owned unit sentinel, not an actual plugin")
        with self.assertRaisesRegex(ValueError, "unexpectedly exists"):
            verifier.missing_fixture(self.base(), missing, 7, 19)
        self.assertEqual(missing.read_bytes(), b"Owned unit sentinel, not an actual plugin")

    def test_fixture_refuses_schema_target_existing_plugins_and_id_collision(self):
        missing = self.folder / "absent.vst3"
        for base, target, version in ((self.base(version=18), 7, 19),):
            with self.assertRaisesRegex(ValueError, "schema"):
                verifier.missing_fixture(base, missing, target, version)
        for kwargs, target, message in (({}, 99, "empty target"),
                                        ({"existing_plugin": True}, 7, "pre-existing"),
                                        ({"first": 42}, 7, "nextId")):
            with self.subTest(kwargs=kwargs, target=target):
                with self.assertRaisesRegex(ValueError, message):
                    verifier.missing_fixture(self.base(**kwargs), missing, target, 19)

    def test_xml_oracle_ignores_only_formatting_not_child_order_or_values(self):
        a = ET.fromstring('<A z="2" x="1"><B id="1"/><B id="2"/></A>')
        formatting = ET.fromstring('<A x="1" z="2">\n  <B id="1"/>\n<B id="2"/>\n</A>')
        order = ET.fromstring('<A x="1" z="2"><B id="2"/><B id="1"/></A>')
        value = ET.fromstring('<A x="1.0" z="2"><B id="1"/><B id="2"/></A>')
        self.assertEqual(verifier.xml_value(a), verifier.xml_value(formatting))
        self.assertNotEqual(verifier.xml_value(a), verifier.xml_value(order))
        self.assertNotEqual(verifier.xml_value(a), verifier.xml_value(value))

    def test_output_reuses_only_matching_marker_and_retains_prior_attempt(self):
        root, first = verifier.prepare_output(str(self.folder / "owned output"))
        retained = first / "failure.json"
        retained.write_text('{"passed": false}', encoding="utf-8")
        index = root / "verification.json"
        index.write_text("owned current index", encoding="utf-8")
        root2, second = verifier.prepare_output(str(root))
        self.assertEqual(root, root2)
        self.assertNotEqual(first, second)
        self.assertEqual(retained.read_text(), '{"passed": false}')
        self.assertEqual(index.read_text(), "owned current index")
        self.assertEqual(json.loads((root / verifier.OWNER_FILE).read_text()), verifier.OWNER)

    def test_output_refuses_nonempty_unowned_or_wrong_marker_without_overwrite(self):
        folder = self.folder / "unrelated output"
        folder.mkdir()
        sentinel = folder / "verification.json"
        sentinel.write_bytes(b"user result")
        with self.assertRaisesRegex(ValueError, "unmarked"):
            verifier.prepare_output(str(folder))
        self.assertEqual(sentinel.read_bytes(), b"user result")
        marker = folder / verifier.OWNER_FILE
        marker.write_text('{"owner": "another verifier"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "marker"):
            verifier.prepare_output(str(folder))
        self.assertEqual(sentinel.read_bytes(), b"user result")

    def test_output_refuses_relative_traversal_and_regular_file_ancestor(self):
        with self.assertRaisesRegex(ValueError, "absolute"):
            verifier.prepare_output("relative-owned-output")
        with self.assertRaisesRegex(ValueError, "traversal"):
            verifier.prepare_output(str(self.folder) + "/../unowned")
        regular = self.folder / "parent-file"
        regular.write_bytes(b"keep")
        with self.assertRaises(ValueError):
            verifier.prepare_output(str(regular / "result"))
        self.assertEqual(regular.read_bytes(), b"keep")

    def test_output_refuses_link_ancestor_and_dangling_result_index(self):
        target = self.folder / "real"
        target.mkdir()
        link = self.folder / "linked"
        try:
            link.symlink_to(target, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest("Symlink creation unavailable for this non-elevated unit: " + str(error))
        with self.assertRaisesRegex(ValueError, "reparse"):
            verifier.prepare_output(str(link / "result"))
        root, _ = verifier.prepare_output(str(self.folder / "owned"))
        (root / "verification.json").symlink_to(self.folder / "missing-outside-target.json")
        with self.assertRaisesRegex(ValueError, "index"):
            verifier.prepare_output(str(root))
        self.assertFalse((self.folder / "missing-outside-target.json").exists())


if __name__ == "__main__":
    unittest.main()
