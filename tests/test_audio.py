"""Compile and execute shared audio/SDK fixtures without camera firmware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            raise RuntimeError("Clang or GCC is required for the audio fixtures")
        base = ROOT / "build"
        base.mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="audio-", dir=base)
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "audio-test.exe"
        command = [
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-Wconversion", "-Wshadow",
            "-I", "include", "src/audio.c", "src/native_audio.c", "src/services.c",
            "src/platform.c", "src/binding_store.c", "src/native_call.c",
            "src/module.c", "src/sha256.c", "src/memory.c", "examples/audio-tone/module.c",
            "tests/test_audio.c", "-o", str(cls.binary),
        ]
        built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        cls.driver_binary = Path(cls.temp.name) / "native-audio-driver-test.exe"
        built = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow",
            "-DFX_PLATFORM_PROFILE=1", "-I", "include", "-I", "tests", "src/audio.c", "src/native_audio.c",
            "src/native_sound.c", "src/native_audio_driver.c", "src/services.c", "src/platform.c",
            "src/binding_store.c", "src/native_call.c", "src/module.c", "src/sha256.c", "src/memory.c",
            "examples/audio-tone/module.c", "tests/test_native_audio_driver.c", "-o", str(cls.driver_binary),
        ], cwd=ROOT, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        cls.native_binary = Path(cls.temp.name) / "native-sound-test.exe"
        built = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow",
            "-I", "include", "src/audio.c", "src/native_sound.c", "src/memory.c", "tests/test_native_sound.c",
            "-o", str(cls.native_binary),
        ], cwd=ROOT, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, case):
        ran = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=10)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_mono_stereo_gain_mix_clipping_and_underruns(self):
        self.run_case("mix")

    def test_queue_wrap_pause_drain_stop_and_backpressure(self):
        self.run_case("queue")

    def test_failed_wrapped_copy_does_not_publish_partial_audio(self):
        self.run_case("failed-copy")

    def test_limits_formats_owners_and_allocation_failure(self):
        self.run_case("limits")

    def test_render_contention_and_panic_revocation(self):
        self.run_case("contention")

    def test_128_streams_use_dynamic_budget_not_fixed_slots(self):
        self.run_case("many-streams")

    def test_worker_sdk_copy_replay_and_owner_cleanup(self):
        self.run_case("worker")

    def test_ordinary_tone_module_produces_pcm_and_finishes(self):
        self.run_case("tone")

    def test_ordinary_tone_module_handles_unavailable_output(self):
        self.run_case("unavailable")

    def test_native_double_buffer_tail_stop_and_cancel_both_profiles(self):
        self.native_case("normal")

    def test_ordinary_module_uses_lazy_native_worker_and_original_buffer_protocol(self):
        ran = subprocess.run([str(self.driver_binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_native_busy_and_foreign_consumer_are_not_overwritten(self):
        self.native_case("busy")

    def test_native_missed_ring_stops_without_writing_live_buffer(self):
        self.native_case("callbacks")

    def test_native_timeout_retains_outstanding_message(self):
        self.native_case("timeout")

    def test_native_reject_and_stop_failure_do_not_close_early(self):
        self.native_case("reject")

    def native_case(self, case):
        for target in ("zr", "z9"):
            with self.subTest(target=target):
                ran = subprocess.run([str(self.native_binary), target, case], capture_output=True, text=True, timeout=10)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
