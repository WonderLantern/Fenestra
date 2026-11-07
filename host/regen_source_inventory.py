"""Regenerate SOURCE-INVENTORY.json with the rich provenance schema.

Restores the upstream (origin/main) inventory semantics that commit
6c05b39 collapsed to the bytes/sha256 triple, and keeps them restorable:
the inventory is byte-formatted exactly like the upstream file (CRLF line
endings, indent=2, one key per line, per-entry key order preserved from
the baseline, trailing "}\r\n") so a merge against main shows only
genuine entry changes.

Rules (verified against origin/main archaeology; see
tests/test_source_inventory_regen.py):
  * scope: every git-tracked file except SOURCE-INVENTORY.json itself,
    sorted by path;
  * a file identical to its baseline blob keeps its baseline entry
    verbatim, including hand-ordered keys (the EXCEPTIONS.md quirk) and
    the LICENSE source_url form;
  * a file that differs from its baseline blob inherits
    origin/license/additional_permission but gets
    snapshot_sha256=<baseline blob sha256> and transformed=true;
  * a file absent from the baseline gets the new-file defaults:
    origin=project-source, snapshot_sha256=null, transformed=false,
    license=CC-BY-NC-SA-4.0, additional_permission=EXCEPTIONS.md.

bytes/sha256 are always recomputed from the working tree so the output
stays consistent with verify_source.py (the upstream README.md entry was
stale precisely because a regen was skipped after a file change).
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

SCHEMA = 'fenestra.source-inventory/v1'
INVENTORY_NAME = 'SOURCE-INVENTORY.json'
DEFAULT_BASELINE = 'origin/main'
NEW_ORIGIN = 'project-source'
NEW_LICENSE = 'CC-BY-NC-SA-4.0'
NEW_ADDITIONAL_PERMISSION = 'EXCEPTIONS.md'


def git(root: Path, *args: str) -> bytes:
    """Runs git in *root*; any failure raises with the captured stderr."""
    result = subprocess.run(['git', '-C', str(root), *args],
                            capture_output=True)
    if result.returncode != 0:
        raise RuntimeError("git %s failed: %s"
                           % (' '.join(args),
                              result.stderr.decode('utf-8', 'replace').strip()))
    return result.stdout


def git_blob(root: Path, revision: str, path: str):
    """Returns the blob bytes of *path* at *revision*, or None if absent."""
    result = subprocess.run(['git', '-C', str(root), 'cat-file', 'blob',
                             '%s:%s' % (revision, path)], capture_output=True)
    return result.stdout if result.returncode == 0 else None


def load_baseline_entries(root: Path, baseline: str) -> dict:
    """Parses the baseline inventory into an ordered path -> entry map."""
    data = git_blob(root, baseline, INVENTORY_NAME)
    if data is None:
        git(root, 'rev-parse', '--verify', baseline)  # distinguish bad rev
        raise RuntimeError('baseline %s has no %s' % (baseline, INVENTORY_NAME))
    manifest = json.loads(data.decode('utf-8'))
    if manifest.get('schema') != SCHEMA:
        raise ValueError('baseline inventory has unknown schema')
    entries = {}
    for entry in manifest['files']:
        if entry['path'] in entries:
            raise ValueError('duplicate baseline entry: ' + entry['path'])
        entries[entry['path']] = entry
    return entries


def build_manifest(root: Path, baseline: str):
    """Builds the regenerated manifest and rule statistics.

    Returns (manifest, stats); pure computation, touches no files.
    """
    root = Path(root)
    baseline_entries = load_baseline_entries(root, baseline)
    tracked = git(root, 'ls-files').decode('utf-8').splitlines()
    files, stats = [], {'inherited_unchanged': 0, 'inherited_transformed': 0,
                        'new': 0}
    for path in sorted(p for p in tracked if p != INVENTORY_NAME):
        if '\n' in path or '\r' in path:
            raise ValueError('unserializable inventory path: %r' % path)
        current = (root / path)
        if not current.is_file():
            raise RuntimeError('tracked file missing from worktree: ' + path)
        data = current.read_bytes()
        digest, size = hashlib.sha256(data).hexdigest(), len(data)
        entry = None
        if path in baseline_entries:
            entry = dict(baseline_entries[path])  # keeps the key order
            blob = git_blob(root, baseline, path)
            if blob is None:
                raise RuntimeError(
                    'baseline inventory lists %s but the revision lacks '
                    'its blob' % path)
            if hashlib.sha256(blob).hexdigest() == digest:
                stats['inherited_unchanged'] += 1
            else:
                entry['snapshot_sha256'] = hashlib.sha256(blob).hexdigest()
                entry['transformed'] = True
                stats['inherited_transformed'] += 1
            entry['bytes'] = size
            entry['sha256'] = digest
        else:
            entry = {'path': path, 'bytes': size, 'sha256': digest,
                     'origin': NEW_ORIGIN, 'snapshot_sha256': None,
                     'transformed': False, 'license': NEW_LICENSE,
                     'additional_permission': NEW_ADDITIONAL_PERMISSION}
            stats['new'] += 1
        files.append(entry)
    stats['files'] = len(files)
    return {'schema': SCHEMA, 'files': files}, stats


def serialize(manifest: dict) -> bytes:
    """Serializes in the upstream byte format (CRLF, indent=2, "}\r\n")."""
    for entry in manifest['files']:
        for value in entry.values():
            if isinstance(value, str) and ('\n' in value or '\r' in value):
                raise ValueError('inventory value with embedded newline: %r'
                                 % value)
    text = json.dumps(manifest, indent=2)
    return text.replace('\n', '\r\n').encode('utf-8') + b'\r\n'


def describe_drift(current: bytes, regenerated: bytes, limit: int = 10):
    """Lists paths whose entries differ (or are missing/extra)."""
    try:
        old = {e['path']: e for e in json.loads(current.decode('utf-8'))['files']}
        new = {e['path']: e for e in json.loads(regenerated.decode('utf-8'))['files']}
    except (ValueError, KeyError, UnicodeDecodeError):
        return ['current inventory is not parseable']
    drift = sorted(set(old) ^ set(new)) + sorted(
        p for p in set(old) & set(new) if old[p] != new[p])
    return drift[:limit] + (['... %d more' % (len(drift) - limit)]
                            if len(drift) > limit else [])


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Regenerate SOURCE-INVENTORY.json with rich provenance.')
    parser.add_argument('--root', type=Path,
                        default=Path(__file__).resolve().parent.parent,
                        help='repository root (default: this repo)')
    parser.add_argument('--baseline', default=DEFAULT_BASELINE,
                        help='git revision to inherit entries from '
                             '(default: %s)' % DEFAULT_BASELINE)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true',
                      help='verify the on-disk inventory matches a '
                           'regeneration; exit 0 if identical')
    mode.add_argument('--write', action='store_true',
                      help='regenerate the inventory in place')
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        manifest, stats = build_manifest(root, args.baseline)
        data = serialize(manifest)
    except (RuntimeError, ValueError) as error:
        print('error: %s' % error, file=sys.stderr)
        return 2
    target = root / INVENTORY_NAME
    if args.check:
        if not target.is_file():
            print('error: missing %s' % INVENTORY_NAME, file=sys.stderr)
            return 1
        current = target.read_bytes()
        if current == data:
            print(json.dumps({'check': 'passed', **stats}))
            return 0
        print('inventory drift (%d entries differ):'
              % (len(json.loads(data)['files'])))
        for line in describe_drift(current, data):
            print('  ' + line)
        return 1
    target.write_bytes(data)
    print(json.dumps({'check': 'written', **stats}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
