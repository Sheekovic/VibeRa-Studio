"""Pure native-fixture preflight tests. Never import/run desktop automation."""
import ast
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NativeMenuContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = ROOT / "scripts/verify-mixer-controls-desktop.py"
        tree = ast.parse(script.read_text(encoding="utf-8-sig"))
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "mixer_colour_menu_contract")
        namespace = {"re": re}
        # Executing this single pure function cannot acquire input ownership,
        # launch applications or run the script's module-level native code.
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        cls.contract = staticmethod(namespace["mixer_colour_menu_contract"])
        cls.source = (ROOT / "src/StudioMixerControls.cpp").read_text(encoding="utf-8-sig")

    def test_current_named_menu_with_disabled_restore(self):
        result = self.contract(self.source)
        self.assertEqual(result["target"], "Colour")
        self.assertEqual(result["downKeys"], 7)
        self.assertIn("no current solo", result["prerequisites"])

    def test_old_extra_disabled_and_renamed_paths_refuse_before_input(self):
        replacements = [
            ('    menu.addItem(15, "Unmute all tracks (Ctrl-right-click enable light)");\n', ''),
            ('    menu.addItem(30, "Selection / display groups...");\n', ''),
            ('    menu.addItem(2, "Mute", true, insert->muted);',
             '    menu.addItem(99, "Unexpected row");\n    menu.addItem(2, "Mute", true, insert->muted);'),
            ('controller.project.mixerSolo.has_value());', 'true);'),
            ('menu.addSubMenu("Colour", colours);', 'menu.addSubMenu("Other", colours);'),
            ('    menu.addItem(2, "Mute", true, insert->muted);',
             '    menu.addSubMenu("Unexpected", colours);\n    menu.addItem(2, "Mute", true, insert->muted);'),
            ('menu.addItem(1, "Rename... (F2 / middle-click)");',
             'menu.addItem(1, "Rename... (F2 / middle-click)", false);'),
            ('menu.addItem(2, "Mute", true, insert->muted);',
             'menu.addItem(2, "Mute", false, insert->muted);'),
        ]
        for before, after in replacements:
            with self.subTest(before=before):
                self.assertIn(before, self.source)
                mutation = self.source.replace(before, after, 1)
                with self.assertRaises(RuntimeError):
                    self.contract(mutation)



class NativeCommandCompletionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = ROOT / "scripts/verify-mixer-controls-desktop.py"
        tree = ast.parse(script.read_text(encoding="utf-8-sig"))
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "await_command_result")
        import time
        namespace = {"time": time}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        cls.await_result = staticmethod(namespace["await_command_result"])

    def test_exact_chain_and_numeric_routing_completion(self):
        for token, tool in (("owned-chain", "vibera_mixerchain"), (31, "vibera_mixer")):
            observed = []
            def call(name, args):
                observed.append((name, args))
                payload = dict(operation=token, completionState="completed", documentChanged=True)
                return payload if isinstance(token, str) else dict(routing=payload)
            actual = self.await_result(dict(operation=token, completionState="pending"), call,
                                       clock=lambda: 0, pause=lambda unused: None)
            self.assertEqual(actual["completionState"], "completed")
            self.assertEqual(observed, [(tool, dict(action="job" if isinstance(token,str) else "routingJob", operation=token))])

    def test_failed_cancelled_unknown_and_timeout_never_become_success(self):
        for state in ("failed", "cancelled"):
            with self.subTest(state=state), self.assertRaisesRegex(RuntimeError, "did not complete"):
                self.await_result(dict(operation="owned", completionState="pending"),
                                  lambda name,args: dict(completionState=state),
                                  clock=lambda: 0, pause=lambda unused: None)
        with self.assertRaisesRegex(RuntimeError, "Unknown asynchronous"):
            self.await_result(dict(completionState="pending"), lambda name,args: self.fail("No polling target"),
                              clock=lambda: 0, pause=lambda unused: None)
        ticks = iter((0,21))
        with self.assertRaisesRegex(RuntimeError, "still pending"):
            self.await_result(dict(operation="owned", completionState="pending"),
                              lambda name,args: self.fail("Deadline must refuse before polling"),
                              clock=lambda: next(ticks), pause=lambda unused: None)

class NativeSampleProjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = ROOT / "scripts/verify-mixer-controls-desktop.py"
        tree = ast.parse(script.read_text(encoding="utf-8-sig"))
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "await_sample_projection")
        import time
        namespace = {"time": time}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        cls.await_projection = staticmethod(namespace["await_sample_projection"])

    def drive(self, states, sources=None, guard=None):
        ticks = [0.]
        index = [-1]
        def read():
            index[0] += 1
            return states[min(index[0], len(states)-1)]
        def page():
            return (sources[min(index[0], len(sources)-1)] if sources else
                    dict(projectIdentity="owned", revision=3, items=[dict(id=11, source="owned.wav")]))
        return self.await_projection(read, page, 11, "owned.wav", guard or (lambda: None),
                                     clock=lambda: ticks[0], pause=lambda unused: ticks.__setitem__(0, ticks[0]+1))

    @staticmethod
    def observation(calls, pending=False):
        return dict(projectIdentity="owned", revision=3, pendingAudioUpdate=pending,
                    audio=dict(channels=[dict(channel=11, graphInitialisations=1, renderCalls=calls)]))

    def test_pending_and_actual_later_callback(self):
        guards = []
        result = self.drive([self.observation(99, True), self.observation(0), self.observation(1)],
                            guard=lambda: guards.append(True))
        self.assertEqual((result["beforeRenderCalls"], result["afterRenderCalls"]), (0, 1))
        self.assertEqual(len(guards), 3)
        self.assertFalse(result["pendingAudioUpdate"])

    def test_pending_or_no_callback_times_out_without_success(self):
        for value in (self.observation(9, True), self.observation(9)):
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, "did not execute"):
                self.drive([value])

    def test_source_project_revision_and_callback_owner_drift_refuse(self):
        for key, changed in (("projectIdentity", "foreign"), ("revision", 4)):
            next_state = self.observation(2)
            next_state[key] = changed
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "project/revision changed"):
                self.drive([self.observation(1), next_state])
        with self.assertRaisesRegex(RuntimeError, "channel/source identity changed"):
            self.drive([self.observation(1)], [dict(projectIdentity="owned", revision=3,
                items=[dict(id=11, source="different.wav")])])
        with self.assertRaisesRegex(RuntimeError, "sampler changed"):
            self.drive([self.observation(4), self.observation(0)])
        with self.assertRaisesRegex(RuntimeError, "guard refused"):
            self.drive([self.observation(1)], guard=lambda: (_ for _ in ()).throw(RuntimeError("guard refused")))


class NativeCaptionDiagnosticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = ROOT / "scripts/verify-mixer-controls-desktop.py"
        cls.tree = ast.parse(script.read_text(encoding="utf-8-sig"))
        function = next(node for node in cls.tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "caption_activation_diagnostic")
        namespace = {"c": ctypes, "w": wintypes}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        cls.observe = staticmethod(namespace[function.name])

    class ReadOnlyWindows:
        """Synthetic geometry; only PID pair comes from retained final refusal.

        The old receipt did not contain coordinates/style. These values cannot
        establish what actually obscured that historical run.
        """
        owners = {11: 26544, 41: 14128, 42: 14128, 43: 14128}
        roots = {11: 11, 41: 42, 42: 42, 43: 42}
        available = True
        dpi = 144

        def GetWindowThreadProcessId(self, hwnd, output):
            output._obj.value = self.owners.get(hwnd, 0)
            return 100 + hwnd if hwnd in self.owners else 0

        def IsWindow(self, hwnd):
            return self.available and hwnd in self.owners

        def GetWindowLongPtrW(self, hwnd, index):
            return (8 if hwnd == 42 else 0) if index == -20 else 0x86000000

        def GetAncestor(self, hwnd, flag):
            if flag != 2:
                raise AssertionError("Only the observational root query is allowed")
            return self.roots.get(hwnd, 0)

        def GetWindowRect(self, hwnd, output):
            output._obj.left, output._obj.top = 80, 70
            output._obj.right, output._obj.bottom = 2000, 1220
            return self.available

        def GetClientRect(self, hwnd, output):
            output._obj.left, output._obj.top = 0, 0
            output._obj.right, output._obj.bottom = 1920, 1150
            return self.available

        def GetDpiForWindow(self, hwnd):
            return self.dpi

        def __getattr__(self, name):
            raise AssertionError("Diagnostic attempted an unexpected/native input API: " + name)

    def snapshot(self, **changes):
        arguments = dict(positioned=True, activated=False, converted=True,
                         origin=wintypes.POINT(80, 70), caption=wintypes.POINT(380, 88),
                         hit=41, foreground=43,
                         held=dict(mouseButtons=False, modifiers=False))
        arguments.update(changes)
        return self.observe(self.ReadOnlyWindows(), 11, 26544, **arguments)

    def test_retained_foreign_pid_pair_preserves_actual_refusal_and_root_identity(self):
        result = self.snapshot()
        self.assertEqual(result["ownerPid"], 26544)
        self.assertEqual(result["owned"]["pid"], 26544)
        self.assertEqual(result["hit"]["pid"], 14128)
        self.assertEqual(result["foreground"]["pid"], 14128)
        self.assertEqual(result["hit"]["hwnd"], 41)
        self.assertEqual(result["hit"]["root"]["hwnd"], 42)
        self.assertFalse(result["hit"]["topmost"])
        self.assertTrue(result["hit"]["root"]["topmost"])
        self.assertFalse(result["setForegroundWindowSucceeded"])
        self.assertEqual(result["clientOriginScreen"], [80, 70])
        self.assertEqual(result["captionScreen"], [380, 88])
        self.assertEqual(result["windowDpi"], 144)
        self.assertTrue(result["captionInsideOwnedClient"])
        self.assertEqual(json.loads(json.dumps(result)), result)
        self.assertIn("not atomic", result["scope"])

    def test_failed_conversion_never_reports_unconverted_point_as_screen_coordinates(self):
        result = self.snapshot(converted=False, hit=None, held=None)
        self.assertFalse(result["clientToScreenSucceeded"])
        for key in ("clientOriginScreen", "captionScreen", "captionInsideOwnedClient", "hit"):
            self.assertIsNone(result[key])
        self.assertEqual(result["ownedWindowRect"], [80, 70, 2000, 1220])

    def test_positioning_failure_does_not_invent_later_api_results(self):
        result = self.snapshot(positioned=False, activated=None, converted=None,
                               origin=None, caption=None, hit=None, held=None)
        self.assertFalse(result["positioningSucceeded"])
        self.assertIsNone(result["setForegroundWindowSucceeded"])
        self.assertIsNone(result["clientToScreenSucceeded"])
        self.assertIsNone(result["captionScreen"])

    def test_api_activation_success_does_not_overwrite_foreign_hit_or_held_refusal(self):
        result = self.snapshot(activated=True, held=dict(mouseButtons=False, modifiers=True))
        self.assertTrue(result["setForegroundWindowSucceeded"])
        self.assertEqual(result["hit"]["pid"], 14128)
        self.assertTrue(result["heldCategories"]["modifiers"])

    def test_client_containment_uses_actual_origin_and_half_open_rect(self):
        for point, inside in ((wintypes.POINT(80, 70), True),
                              (wintypes.POINT(1999, 1219), True),
                              (wintypes.POINT(2000, 88), False),
                              (wintypes.POINT(79, 88), False)):
            with self.subTest(point=(point.x, point.y)):
                self.assertEqual(self.snapshot(caption=point)["captionInsideOwnedClient"], inside)

    def test_unavailable_geometry_is_unknown_not_fabricated(self):
        api = self.ReadOnlyWindows()
        api.available = False
        api.dpi = 0
        result = self.observe(api, 11, 26544, positioned=True, activated=False, converted=True,
                              origin=wintypes.POINT(80, 70), caption=wintypes.POINT(380, 88),
                              hit=None, foreground=0, held=None)
        for key in ("ownedWindowRect", "ownedClientRect", "captionInsideOwnedClient", "windowDpi", "hit"):
            self.assertIsNone(result[key])
        self.assertIsNone(result["owned"]["style"])
        self.assertIsNone(result["owned"]["topmost"])
        self.assertEqual(result["foreground"]["hwnd"], 0)

    def test_failed_conversion_has_explicit_refusal_before_any_input(self):
        guards = [node for node in ast.walk(self.tree) if isinstance(node, ast.If)
                  and isinstance(node.test, ast.UnaryOp) and isinstance(node.test.op, ast.Not)
                  and isinstance(node.test.operand, ast.Name) and node.test.operand.id == "converted"]
        self.assertEqual(len(guards), 1)
        body = ast.Module(body=guards[0].body, type_ignores=[])
        self.assertTrue(any(isinstance(node, ast.Raise) for node in ast.walk(body)))
        calls = [node.func.attr for node in ast.walk(body) if isinstance(node, ast.Call)
                 and isinstance(node.func, ast.Attribute)]
        self.assertNotIn("SetCursorPos", calls)
        self.assertNotIn("mouse_event", calls)
        self.assertNotIn("WindowFromPoint", calls)


class NativeUnicodeTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import time
        script = ROOT / "scripts/verify-mixer-controls-desktop.py"
        tree = ast.parse(script.read_text(encoding="utf-8-sig"))
        names = {"unicode_keyboard_input_type", "send_unicode_text"}
        functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
        namespace = {"c": ctypes, "w": wintypes, "time": time}
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(script), "exec"), namespace)
        cls.input_type = namespace["unicode_keyboard_input_type"]()
        cls.send_text = staticmethod(namespace["send_unicode_text"])

    def test_complete_windows_input_union_abi(self):
        pointer64 = ctypes.sizeof(ctypes.c_void_p) == 8
        self.assertEqual(ctypes.sizeof(wintypes.WORD), 2)
        self.assertEqual(ctypes.sizeof(wintypes.DWORD), 4)
        self.assertEqual(ctypes.sizeof(wintypes.LONG), 4)
        self.assertEqual(ctypes.sizeof(self.input_type), 40 if pointer64 else 28)
        self.assertEqual(self.input_type.data.offset, 8 if pointer64 else 4)
        union = self.input_type._fields_[1][1]
        members = dict(union._fields_)
        self.assertEqual(set(members), {"mi", "ki", "hi"})
        self.assertEqual(ctypes.sizeof(members["mi"]), 32 if pointer64 else 24)
        self.assertEqual(ctypes.sizeof(members["ki"]), 24 if pointer64 else 16)
        self.assertEqual(members["ki"].wVk.offset, 0)
        self.assertEqual(members["ki"].wScan.offset, 2)
        self.assertEqual(members["ki"].dwFlags.offset, 4)
        self.assertEqual(members["ki"].time.offset, 8)
        self.assertEqual(members["ki"].dwExtraInfo.offset, 16 if pointer64 else 12)

    def test_literal_utf16_units_pair_order_and_guard_before_every_send(self):
        events = []
        packets = []
        def send(count, batch, size):
            events.append(("send", count))
            self.assertEqual(events[-2], "guard")
            self.assertEqual(size, ctypes.sizeof(self.input_type))
            self.assertEqual(count, 2)
            for item in batch:
                keyboard = item.data.ki
                packets.append((item.type, keyboard.wVk, keyboard.wScan, keyboard.dwFlags,
                                keyboard.time, keyboard.dwExtraInfo))
            return count
        self.send_text("drums", send, lambda: events.append("guard"), self.input_type,
                       pause=lambda delay: events.append(("pause", delay)))
        expected = [(1, 0, ord(char), flags, 0, 0) for char in "drums" for flags in (4, 6)]
        self.assertEqual(packets, expected)
        self.assertEqual(events, [item for unused in "drums" for item in
                                  ("guard", ("send", 2), ("pause", .15))])

    def test_supplementary_unicode_uses_ordered_utf16_surrogates_without_layout_api(self):
        units = []
        def send(count, batch, size):
            units.append((batch[0].data.ki.wScan, batch[1].data.ki.wScan))
            return count
        self.send_text("é😀", send, lambda: None, self.input_type, pause=lambda unused: None)
        self.assertEqual(units, [(0xe9, 0xe9), (0xd83d, 0xd83d), (0xde00, 0xde00)])

    def test_guard_refusal_and_invalid_text_send_nothing(self):
        def no_send(*unused):
            self.fail("No input may be sent after refusal")
        def refuse():
            raise RuntimeError("Foreground or physical-input guard refused")
        with self.assertRaisesRegex(RuntimeError, "guard refused"):
            self.send_text("drums", no_send, refuse, self.input_type)
        for text in ("", "x"*65, "\n", "x\x00", "\x7f", "😀"*33, "\ud800"):
            with self.subTest(text=repr(text)), self.assertRaises((ValueError, UnicodeEncodeError)):
                self.send_text(text, no_send, lambda: self.fail("Invalid text must refuse before guard"),
                               self.input_type)

    def test_zero_acceptance_fails_without_retry_or_keyup(self):
        calls = []
        with self.assertRaisesRegex(RuntimeError, "only 0 of2"):
            self.send_text("drums", lambda count, batch, size: calls.append(count) or 0,
                           lambda: None, self.input_type, pause=lambda unused: self.fail("No pause after failure"))
        self.assertEqual(calls, [2])

    def test_partial_acceptance_releases_only_same_unit_and_never_retries_text(self):
        events = []
        def send(count, batch, size):
            events.append(("send", count, [(item.data.ki.wScan, item.data.ki.dwFlags) for item in batch]))
            self.assertEqual(events[-2], "guard")
            return 1
        with self.assertRaisesRegex(RuntimeError, "only 1 of2"):
            self.send_text("drums", send, lambda: events.append("guard"), self.input_type,
                           pause=lambda unused: self.fail("No later text after partial send"))
        self.assertEqual(events, ["guard", ("send", 2, [(100, 4), (100, 6)]),
                                  "guard", ("send", 1, [(100, 6)])])

    def test_partial_release_failure_is_explicit_without_retry(self):
        calls = []
        def send(count, batch, size):
            calls.append(count)
            return 1 if count == 2 else 0
        with self.assertRaisesRegex(RuntimeError, "key-up cleanup failed"):
            self.send_text("drums", send, lambda: None, self.input_type, pause=lambda unused: None)
        self.assertEqual(calls, [2, 1])

    def test_partial_then_guard_loss_never_sends_cleanup_to_unowned_window(self):
        calls = []
        guards = []
        def guard():
            guards.append(True)
            if len(guards) == 2:
                raise RuntimeError("Owner changed")
        with self.assertRaisesRegex(RuntimeError, "key-up cleanup guard refused"):
            self.send_text("drums", lambda count, batch, size: calls.append(count) or 1,
                           guard, self.input_type, pause=lambda unused: None)
        self.assertEqual(calls, [2])
        self.assertEqual(len(guards), 2)



if __name__ == "__main__":
    unittest.main()
