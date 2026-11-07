"""SOURCE-INVENTORY regeneration contracts (T8).

The regenerator must restore the upstream rich-provenance schema byte for
byte: CRLF line endings, indent=2, one key per line, per-entry key order
preserved from the baseline inventory, and the file ending with a single
``}`` line.  Inheritance rules (verified against origin/main archaeology):

* a file identical to its baseline blob keeps its baseline entry verbatim
  (including hand-ordered keys such as the EXCEPTIONS.md quirk);
* a file that differs from its baseline blob inherits
  origin/license/additional_permission but gets
  ``snapshot_sha256=<baseline blob sha256>`` and ``transformed=true``;
* a file absent from the baseline gets the new-file defaults
  (origin=project-source, snapshot_sha256=null, transformed=false,
  license=CC-BY-NC-SA-4.0, additional_permission=EXCEPTIONS.md).

The tool must be idempotent (``--check`` exits 0 after ``--write``), must
report drift through ``--check``, and the regenerated inventory must still
pass verify_source.py.  A mini git repository exercises the rules end to
end; a skip-gated test pins the real-repo invariants against origin/main.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / 'host'
REGEN = HOST / 'regen_source_inventory.py'
VERIFY_SOURCE = ROOT / 'verify_source.py'

sys.path.insert(0, str(HOST))
import regen_source_inventory as regen  # noqa: E402

GIT_ENV = {**os.environ,
           'GIT_CONFIG_GLOBAL': '/dev/null',
           'GIT_CONFIG_SYSTEM': '/dev/null'}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_tool(root, baseline, *flags):
    return subprocess.run(
        [sys.executable, str(REGEN), '--root', str(root),
         '--baseline', baseline, *flags],
        capture_output=True, text=True, env=GIT_ENV)


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args],
                            capture_output=True, text=True,
                            check=True, env=GIT_ENV)
    return result.stdout.strip()


def entry_text(keys):
    """Serializes one inventory entry exactly like the upstream format."""
    lines = ['    {']
    for index, (key, value) in enumerate(keys):
        comma = ',' if index < len(keys) - 1 else ''
        if isinstance(value, bool):
            rendered = 'true' if value else 'false'
        elif value is None:
            rendered = 'null'
        elif isinstance(value, int):
            rendered = str(value)
        else:
            rendered = '"%s"' % value
        lines.append('      "%s": %s%s' % (key, rendered, comma))
    lines.append('    }')
    return '\r\n'.join(lines)


def inventory_text(entries):
    body = ',\r\n'.join(entry_text(entry) for entry in entries)
    return ('{\r\n'
            '  "schema": "fenestra.source-inventory/v1",\r\n'
            '  "files": [\r\n' + body + '\r\n  ]\r\n}\r\n')


class MiniRepoTestCase(unittest.TestCase):
    """Builds a two-commit git repository:

    baseline commit -- keep.txt (unchanged later), edit.txt (modified
    later) and a rich CRLF inventory; work commit -- edit.txt rewritten,
    new.txt added.  ``keep.txt`` carries the upstream hand-ordered key
    quirk (origin/snapshot/transformed before bytes/sha256) so quirk
    preservation is pinned byte-for-byte.
    """

    KEEP_V1 = b'keep v1\n'
    KEEP_V0 = b'keep v0\n'
    EDIT_V1 = b'edit v1\n'
    EDIT_V2 = b'edit v2\n'
    NEW_V1 = b'new v1\n'

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='t8-inventory-'))
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        (self.root / 'keep.txt').write_bytes(self.KEEP_V1)
        (self.root / 'edit.txt').write_bytes(self.EDIT_V1)
        baseline_inventory = inventory_text([
            # Deliberate hand-ordered keys, mirroring the EXCEPTIONS.md
            # entry upstream: provenance fields precede bytes/sha256.
            [('path', 'keep.txt'), ('origin', 'upstream-archival'),
             ('snapshot_sha256', sha(self.KEEP_V0)), ('transformed', False),
             ('bytes', len(self.KEEP_V1)), ('sha256', sha(self.KEEP_V1)),
             ('license', 'CC-BY-NC-SA-4.0'),
             ('additional_permission', 'EXCEPTIONS.md')],
            [('path', 'edit.txt'), ('bytes', len(self.EDIT_V1)),
             ('sha256', sha(self.EDIT_V1)), ('origin', 'project-source'),
             ('snapshot_sha256', sha(self.EDIT_V1)), ('transformed', False),
             ('license', 'CC-BY-NC-SA-4.0'),
             ('additional_permission', 'EXCEPTIONS.md')],
        ])
        (self.root / 'SOURCE-INVENTORY.json').write_bytes(
            baseline_inventory.encode('ascii'))
        git(self.root, 'init', '-q', '-b', 'main')
        git(self.root, 'add', '-A')
        git(self.root, '-c', 'user.name=t8', '-c', 'user.email=t8@example',
            'commit', '-qm', 'baseline')
        self.baseline = git(self.root, 'rev-parse', 'HEAD')
        # Work commit: modify one file, add one file, leave the inventory
        # stale (as the branch's simplified inventory left it).
        (self.root / 'edit.txt').write_bytes(self.EDIT_V2)
        (self.root / 'new.txt').write_bytes(self.NEW_V1)
        git(self.root, 'add', '-A')
        git(self.root, '-c', 'user.name=t8', '-c', 'user.email=t8@example',
            'commit', '-qm', 'work')

    def regenerated(self):
        manifest, _stats = regen.build_manifest(self.root, self.baseline)
        return manifest['files'], regen.serialize(manifest)

    def test_unchanged_entry_is_inherited_byte_for_byte(self):
        _files, data = self.regenerated()
        expected = entry_text([
            ('path', 'keep.txt'), ('origin', 'upstream-archival'),
            ('snapshot_sha256', sha(self.KEEP_V0)), ('transformed', False),
            ('bytes', len(self.KEEP_V1)), ('sha256', sha(self.KEEP_V1)),
            ('license', 'CC-BY-NC-SA-4.0'),
            ('additional_permission', 'EXCEPTIONS.md')])
        self.assertIn(expected.encode('ascii'), data)

    def test_changed_entry_inherits_provenance_and_snapshots_baseline(self):
        files, _data = self.regenerated()
        entry = {e['path']: e for e in files}['edit.txt']
        self.assertEqual(entry['origin'], 'project-source')
        self.assertEqual(entry['license'], 'CC-BY-NC-SA-4.0')
        self.assertEqual(entry['additional_permission'], 'EXCEPTIONS.md')
        self.assertEqual(entry['snapshot_sha256'], sha(self.EDIT_V1))
        self.assertIs(entry['transformed'], True)
        self.assertEqual(entry['bytes'], len(self.EDIT_V2))
        self.assertEqual(entry['sha256'], sha(self.EDIT_V2))

    def test_new_file_gets_authored_defaults_in_canonical_order(self):
        files, _data = self.regenerated()
        entry = {e['path']: e for e in files}['new.txt']
        self.assertEqual(entry, {
            'path': 'new.txt', 'bytes': len(self.NEW_V1),
            'sha256': sha(self.NEW_V1), 'origin': 'project-source',
            'snapshot_sha256': None, 'transformed': False,
            'license': 'CC-BY-NC-SA-4.0',
            'additional_permission': 'EXCEPTIONS.md'})
        self.assertEqual(list(entry.keys()), [
            'path', 'bytes', 'sha256', 'origin', 'snapshot_sha256',
            'transformed', 'license', 'additional_permission'])

    def test_serialization_matches_upstream_byte_format(self):
        _files, data = self.regenerated()
        self.assertTrue(data.endswith(b'}\r\n'))
        self.assertNotIn(b'\n', data.replace(b'\r\n', b''))  # CRLF only
        parsed = json.loads(data.decode('ascii'))
        rebuilt = (json.dumps(parsed, indent=2)
                   .replace('\n', '\r\n').encode('ascii') + b'\r\n')
        self.assertEqual(data, rebuilt)  # one key per line at indent 2
        self.assertEqual([e['path'] for e in parsed['files']],
                         sorted(e['path'] for e in parsed['files']))
        self.assertNotIn('SOURCE-INVENTORY.json',
                         [e['path'] for e in parsed['files']])

    def test_write_then_check_is_idempotent(self):
        result = run_tool(self.root, self.baseline, '--write')
        self.assertEqual(result.returncode, 0, result.stderr)
        written = (self.root / 'SOURCE-INVENTORY.json').read_bytes()
        for _ in range(2):
            check = run_tool(self.root, self.baseline, '--check')
            self.assertEqual(check.returncode, 0,
                             check.stdout + check.stderr)
        manifest, _stats = regen.build_manifest(self.root, self.baseline)
        self.assertEqual(written, regen.serialize(manifest))

    def test_check_reports_drift(self):
        self.assertEqual(
            run_tool(self.root, self.baseline, '--write').returncode, 0)
        (self.root / 'new.txt').write_bytes(b'new v2\n')
        check = run_tool(self.root, self.baseline, '--check')
        self.assertEqual(check.returncode, 1)

    def test_regenerated_inventory_passes_verify_source(self):
        self.assertEqual(
            run_tool(self.root, self.baseline, '--write').returncode, 0)
        shutil.copy(VERIFY_SOURCE, self.root / 'verify_source.py')
        result = subprocess.run(
            [sys.executable, str(self.root / 'verify_source.py')],
            capture_output=True, text=True, env=GIT_ENV)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"passed": true', result.stdout)


def origin_main_available():
    if not shutil.which('git'):
        return False
    probe = subprocess.run(
        ['git', '-C', str(ROOT), 'cat-file', '-e',
         'origin/main:SOURCE-INVENTORY.json'],
        capture_output=True, env=GIT_ENV)
    return probe.returncode == 0


@unittest.skipUnless(origin_main_available(),
                     'origin/main inventory not available')
class RealRepoInvariantsTest(unittest.TestCase):
    """Pins the regeneration semantics against the real repository."""

    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.stats = regen.build_manifest(ROOT, 'origin/main')

    def test_entries_cover_tracked_files_except_the_inventory(self):
        tracked = set(git(ROOT, 'ls-files').splitlines())
        tracked.discard('SOURCE-INVENTORY.json')
        listed = [e['path'] for e in self.manifest['files']]
        self.assertEqual(set(listed), tracked)
        self.assertEqual(listed, sorted(listed))

    def test_every_entry_has_the_rich_field_set(self):
        required = {'path', 'bytes', 'sha256', 'origin', 'snapshot_sha256',
                    'transformed', 'license'}
        for entry in self.manifest['files']:
            self.assertTrue(required <= set(entry), entry['path'])

    def test_changed_file_snapshots_its_main_blob(self):
        entries = {e['path']: e for e in self.manifest['files']}
        entry = entries['README.md']  # modified on this branch
        blob = regen.git_blob(ROOT, 'origin/main', 'README.md')
        self.assertEqual(entry['snapshot_sha256'], sha(blob))
        self.assertIs(entry['transformed'], True)

    def test_new_files_get_project_source_defaults(self):
        entries = {e['path']: e for e in self.manifest['files']}
        for path in ('host/build_z8_core.py',
                     'host/regen_source_inventory.py',
                     'tests/test_source_inventory_regen.py'):
            self.assertIn(path, entries)
            self.assertEqual(entries[path]['origin'], 'project-source')
            self.assertIsNone(entries[path]['snapshot_sha256'])
            self.assertIs(entries[path]['transformed'], False)

    def test_license_entry_keeps_its_source_url(self):
        entries = {e['path']: e for e in self.manifest['files']}
        license_entry = entries['LICENSE']
        self.assertEqual(license_entry['origin'],
                         'Creative Commons standard license text')
        self.assertIn('source_url', license_entry)
        self.assertNotIn('additional_permission', license_entry)

    def test_serialization_is_deterministic(self):
        first = regen.serialize(self.manifest)
        again, _stats = regen.build_manifest(ROOT, 'origin/main')
        self.assertEqual(first, regen.serialize(again))
        self.assertTrue(first.endswith(b'}\r\n'))
