"""Synthetic parser fixtures only: these are NOT GPU or GD runtime evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "scripts/verify-gd-vulkan.py"
spec = importlib.util.spec_from_file_location("evidence", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.route = {"pid": 42, "hwnd": "0x123", "zink": True, "preview": False,
                      "renderer": "zink test fixture", "opengl32": "fixture/opengl32.dll"}
        self.events = [
            {"schema": 2, "source": "mesa-26.2.6-zink", "pid": 42, "hwnd": "0x123",
             "event": "vkCreateWin32SurfaceKHR", "result": 0},
            {"schema": 2, "source": "mesa-26.2.6-zink", "pid": 42, "hwnd": "0x123",
             "event": "vkQueuePresentKHR", "result": 0, "draw_calls": 1000,
             "present_calls": 120, "present_errors": 0, "window_present_calls": 120,
             "window_present_total_ms": 1.2, "window_present_max_ms": 0.2,
             "window_submit_calls": 30, "window_submit_total_ms": 0.6,
             "window_submit_max_ms": 0.1, "window_flush_calls": 30,
             "window_flush_total_ms": 0.9, "window_flush_max_ms": 0.2,
             "window_acquire_calls": 120, "window_acquire_total_ms": 0.4,
             "window_acquire_max_ms": 0.1},
        ]
        self.wgl_events = [{"schema": 1, "source": "mesa-26.2.6-wgl", "event": "wglSwapBuffers",
                            "pid": 42, "hwnd": "0x123", "swap_calls": 120, "failures": 0,
                            "total_ms": 120.0, "total_max_ms": 2.0, "flush_ms": 10.0,
                            "flush_max_ms": 0.3, "interval_wait_ms": 0.0,
                            "interval_wait_max_ms": 0.0, "present_stage_ms": 105.0,
                            "present_stage_max_ms": 1.5}]

    def check(self, route=None, events=None):
        log = "[Main] [Vulkanizer]: GD_RENDER_ROUTE " + json.dumps(route or self.route)
        return module.verify(log, self.events if events is None else events, expected_pid=42)

    def test_correlated_fixture(self):
        self.assertEqual(self.check()["status"], "GD_GL_TO_VULKAN_CONFIRMED")
        timing = self.check()["mesa_cpu_timing_window"]
        self.assertEqual(timing["zink_batch_vkQueueSubmit_calls"], 30)
        self.assertEqual(timing["vkQueuePresentKHR_total_ms"], 1.2)
        self.assertEqual(timing["zink_flush_total_ms"], 0.9)
        self.assertEqual(timing["vkAcquireNextImageKHR_calls"], 120)

    def test_reject_preview_or_non_zink(self):
        for key in ("preview", "zink"):
            route = copy.deepcopy(self.route)
            route[key] = not route[key]
            with self.assertRaises(ValueError): self.check(route)

    def test_reject_other_window_or_process(self):
        for key, value in (("hwnd", "0x999"), ("pid", 99)):
            events = copy.deepcopy(self.events)
            for event in events: event[key] = value
            with self.assertRaises(ValueError): self.check(events=events)

    def test_require_draws_and_sustained_presents(self):
        for key, value in (("draw_calls", 0), ("present_calls", 119)):
            events = copy.deepcopy(self.events)
            events[-1][key] = value
            with self.assertRaises(ValueError): self.check(events=events)

    def test_reject_vulkan_errors(self):
        events = copy.deepcopy(self.events)
        events[-1]["present_errors"] = 1
        with self.assertRaises(ValueError): self.check(events=events)

    def test_renderer_string_alone_is_insufficient(self):
        with self.assertRaises(ValueError): self.check(events=[])

    def test_require_real_source_tag(self):
        events = copy.deepcopy(self.events)
        for event in events: event["source"] = "standalone-preview"
        with self.assertRaises(ValueError): self.check(events=events)

    def test_correlate_wgl_swap_timing_to_game_window(self):
        log = "[Main] [Vulkanizer]: GD_RENDER_ROUTE " + json.dumps(self.route)
        result = module.verify(log, self.events, expected_pid=42, wgl_events=self.wgl_events)
        self.assertEqual(result["mesa_wgl_timing_window"]["present_stage_ms"], 105.0)
        bad = copy.deepcopy(self.wgl_events)
        bad[0]["hwnd"] = "0x999"
        with self.assertRaises(ValueError):
            module.verify(log, self.events, expected_pid=42, wgl_events=bad)

    def test_duplicate_context_attestation_is_ambiguous(self):
        line = "[Main] [Vulkanizer]: GD_RENDER_ROUTE " + json.dumps(self.route)
        with self.assertRaises(ValueError): module.verify(line + "\n" + line, self.events, 42)

    def test_wgl_probe_result_cannot_pass_as_gd(self):
        log = "WGL_API_PROBE pid=42 hwnd=0x123 frames=120 renderer=zink fixture readback=230,40,80 GL_error=0"
        result = module.verify_wgl_probe(log, self.events)
        self.assertEqual(result["status"], "WGL_ZINK_API_CHECK_PASSED")
        with self.assertRaises(ValueError): module.verify(log, self.events, 42)

    def test_wgl_probe_rejects_wrong_readback(self):
        log = "WGL_API_PROBE pid=42 hwnd=0x123 frames=120 renderer=zink fixture readback=0,0,0 GL_error=0"
        with self.assertRaises(ValueError): module.verify_wgl_probe(log, self.events)


if __name__ == "__main__":
    unittest.main(verbosity=1)
