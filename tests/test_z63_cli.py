"""The normal Z6III entry must verify its build and reject updater routing."""
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fenestra_cli', ROOT/'fenestra.py')
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class Z63Cli(unittest.TestCase):
    def test_normal_build_requires_independent_verification(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            argv = ['fenestra.py', 'build', '--target', 'z63-c201', '--firmware',
                    '/local/original.bi', '--output', str(output), '--llvm-bin',
                    '/local/llvm', '--linker', '/local/ld.lld']
            with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()), \
                 patch('build_z63_probe.build', return_value={'target':'z63-c201'}) as build, \
                 patch('verify_z63_build.verify', return_value={'do_not_flash':True}) as verify:
                cli.main()
            self.assertEqual(build.call_args.kwargs, dict(menu=True, sdk=True, ptp=False, runtime=True, ui=True))
            verify.assert_called_once_with(Path('/local/original.bi'), output, Path('/local/llvm'))
            self.assertIn('"do_not_flash": true', (output/'verified.json').read_text())

    def test_verification_failure_is_not_published(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            argv = ['fenestra.py', 'build', '--target', 'z63-c201', '--firmware',
                    '/local/original.bi', '--output', str(output), '--llvm-bin',
                    '/local/llvm', '--linker', '/local/ld.lld']
            with patch.object(sys, 'argv', argv), \
                 patch('build_z63_probe.build', return_value={}), \
                 patch('verify_z63_build.verify', side_effect=ValueError('changed source')):
                with self.assertRaisesRegex(ValueError, 'changed source'):
                    cli.main()
            self.assertFalse((output/'verified.json').exists())

    def test_z63_does_not_enter_legacy_updater(self):
        argv = ['fenestra.py', 'make-patch', '--target', 'z63-c201']
        with patch.object(sys, 'argv', argv), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                cli.main()
        self.assertEqual(error.exception.code, 2)
