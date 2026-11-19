"""Cross-owner arbitration through actual platform, lease and hook code."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class ModuleConflictTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:raise RuntimeError('C11 compiler required')
        (ROOT/'build').mkdir(exist_ok=True)
        cls.temp=tempfile.TemporaryDirectory(prefix='module-conflicts-',dir=ROOT/'build')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary=Path(cls.temp.name)/'conflicts.exe'
        command=[compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-I','include',
            'src/platform.c','src/binding_store.c','src/resource_conflict.c','src/hooks.c','src/native_call.c',
            'src/module.c','src/services.c','src/memory.c','src/sha256.c','tests/module_conflict_fixture.c','-o',str(cls.binary)]
        run=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=60)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)

    def run_case(self,name):
        run=subprocess.run([str(self.binary),name],capture_output=True,text=True,timeout=15)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        result=json.loads(run.stdout)
        self.assertTrue(result['passed'])
        self.assertFalse(result['native_camera_used'])

    def test_shared_function_calls_and_exclusive_aliases(self):self.run_case('functions')
    def test_shared_reads_overlapping_writes_and_disjoint_tables(self):self.run_case('tables')
    def test_provider_names_and_explicit_semantic_resource_groups(self):self.run_case('groups')
    def test_failed_and_uncertain_attach_ownership(self):self.run_case('failed_attach')
    def test_acknowledged_detach_is_not_repeated_on_registry_contention(self):self.run_case('detach_retry')
    def test_normal_stop_releases_and_panic_retains(self):self.run_case('shutdown')
    def test_permanent_detach_error_is_not_masked_by_later_contention(self):self.run_case('stop_error')
    def test_nonblocking_contention_generations_and_atomic_release(self):self.run_case('registry_edges')
    def test_copied_sdk_conflict_is_diagnostic_and_does_not_panic(self):self.run_case('copied_sdk')
    def test_hundreds_of_owners_and_growing_lease_storage(self):self.run_case('scale')
    def test_hook_target_and_function_claims_survive_source_handle_close(self):self.run_case('hook_lifetime')
    def test_shared_table_cannot_install_mutating_hook(self):self.run_case('hook_shared')


if __name__=='__main__':unittest.main()
