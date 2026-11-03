"""Pure native menu/case preflight; never import/run Win32 desktop automation."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeGroupColourContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = ROOT / "scripts/verify-mixer-controls-desktop.py"
        tree = ast.parse(script.read_text(encoding="utf-8-sig"))
        selected = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                    and node.name in ("mixer_group_colour_menu_contract", "select_case")]
        if len(selected) != 2:
            raise AssertionError("Exact two reviewed pure functions required")
        namespace = {"re": re}
        exec(compile(ast.Module(body=selected, type_ignores=[]), str(script), "exec"), namespace)
        cls.contract = staticmethod(namespace["mixer_group_colour_menu_contract"])
        cls.select = staticmethod(namespace["select_case"])
        cls.controls = (ROOT / "src/StudioMixerControls.cpp").read_text(encoding="utf-8-sig")
        cls.organisation = (ROOT / "src/StudioMixerOrganisation.inc").read_text(encoding="utf-8-sig")
        cls.groups = (ROOT / "src/MixerGroups.h").read_text(encoding="utf-8-sig")

    def test_explicit_case_and_full_default(self):
        self.assertEqual(self.select(["app", "out"]), "full")
        for name in ("full", "overload", "reorder", "group-colour"):
            self.assertEqual(self.select(["app", "out", "--case", name]), name)
        for args in ([], ["app"], ["app", "out", "group-colour"],
                     ["app", "out", "--case", "unknown"],
                     ["app", "out", "--case", "group-colour", "--unsafe"]):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.select(args)

    def test_current_two_menu_path_with_no_solo(self):
        result = self.contract(self.controls, self.organisation, self.groups)
        self.assertEqual(result["mainDownKeys"], 2)
        self.assertEqual(result["groupDownKeys"], 11)
        self.assertEqual(len(result["enabledPrefix"]), 11)
        self.assertEqual(result["enabledPrefix"][-1], "group.colourMembers")
        self.assertEqual(result["labels"]["theme"], "Reset group insert colours to theme")
        self.assertEqual(result["labels"]["explicit"], "Apply this insert colour to group")

    def test_prefix_availability_and_target_drift_refuse_before_input(self):
        changes = [
            ("controls", 'menu.addItem(30, "Selection / display groups...");',
             'menu.addItem(30, "Other...");'),
            ("controls", 'menu.addItem(1, "Rename... (F2 / middle-click)");',
             'menu.addItem(1, "Rename... (F2 / middle-click)", false);'),
            ("controls", 'menu.addItem(30, "Selection / display groups...");',
             'menu.addItem(99, "Extra", false);\nmenu.addItem(30, "Selection / display groups...");'),
            ("organisation", 'menu.addItem(5, "Ungroup selected", canGroup && membershipAvailable);',
             'menu.addItem(5, "Ungroup selected", false);'),
            ("organisation", 'if (!membershipAvailable) menu.addItem(98,',
             'if (membershipAvailable) menu.addItem(98,'),
            ("organisation", 'menu.addSubMenu("Select group", select, !groupIds.empty());',
             'menu.addItem(99, "Extra row");\nmenu.addSubMenu("Select group", select, !groupIds.empty());'),
            ("organisation", 'menu.addItem(13, mixerGroupColourActionLabel(',
             'menu.addItem(14, mixerGroupColourActionLabel('),
            ("organisation", 'menu.addItem(2, "Select this group", !group->members.empty());',
             'if (false) menu.addItem(2, "Select this group", !group->members.empty());'),
            ("organisation", 'args.mixer = index; // Original clicked member, never later selection/row.',
             'args.mixer = selectedInsert; // Original clicked member, never later selection/row.'),
            ("groups", 'storedColour == 0 ?', 'storedColour != 0 ?'),
            ("groups", 'Reset group insert colours to theme', 'Use a rendered RGB colour'),
        ]
        for field, before, after in changes:
            with self.subTest(field=field, before=before):
                values = dict(controls=self.controls, organisation=self.organisation, groups=self.groups)
                self.assertIn(before, values[field])
                values[field] = values[field].replace(before, after, 1)
                with self.assertRaises(RuntimeError):
                    self.contract(values["controls"], values["organisation"], values["groups"])

    def test_ambiguous_source_refuses(self):
        with self.assertRaises(RuntimeError):
            self.contract(self.controls + "\nvoid Studio::mixerControlMenu(int index)",
                          self.organisation, self.groups)
        with self.assertRaises(RuntimeError):
            self.contract(self.controls,
                          self.organisation + "\nvoid Studio::mixerOrganisationMenu(int index)", self.groups)


if __name__ == "__main__":
    unittest.main()
