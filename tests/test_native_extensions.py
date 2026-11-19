"""Executable native/ordinary SDK contracts; no firmware or camera input."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
COMMON=['src/platform.c','src/binding_store.c','src/native_call.c','src/accounting.c',
        'src/services.c','src/module.c','src/sha256.c','src/memory.c','src/extensions.c','src/stream.c']

class NativeExtensions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler=shutil.which('gcc') or shutil.which('clang') or shutil.which('cc')
        if not cls.compiler:raise unittest.SkipTest('a C11 compiler is required')
        (ROOT/'build').mkdir(exist_ok=True)

    def compile_run(self,name,sources,threads=False):
        with tempfile.TemporaryDirectory(prefix='native-sdk-',dir=ROOT/'build') as temp:
            output=Path(temp)/('test.exe' if os.name=='nt' else 'test')
            command=[self.compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-I',str(ROOT/'include')]
            if threads:command+=['-DFX_TEST_PTHREAD=1','-pthread']
            command += [str(ROOT/source) for source in sources]+['-o',str(output)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(output)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('passed',result.stdout,name)

    def test_stream_ownership_and_concurrent_payload_order(self):
        self.compile_run('stream',['tests/test_stream_runtime.c','src/stream.c','src/memory.c'],threads=True)

    def test_sdk_continuations_revision_fences_and_hook_conflicts(self):
        self.compile_run('helpers',['tests/test_sdk_helpers.c'])

    def test_named_services_audio_logs_and_absolute_timers(self):
        self.compile_run('extensions',['tests/test_extension_runtime.c','src/audio.c',*COMMON])

    def test_native_file_extensions(self):
        self.compile_run('files',['tests/test_native_file_extensions.c','src/native_resources.c','src/capacity.c','src/worker_wait.c','src/slot_pool.c','src/hooks.c','src/module_bus.c','src/machine.c',*COMMON])

    def test_native_loader_entry_lifetime_and_both_identities(self):
        self.compile_run('resident',['tests/test_resident_runtime.c','src/resident.c',*COMMON])

if __name__=='__main__':unittest.main()
