"""Apply a locally generated Fenestra patch to a user-provided original update."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent / 'host'))
from cipher_patch import apply_patch

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--patch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = apply_patch(args.firmware, args.patch, args.output)
    print(json.dumps({k: result[k] for k in ('target', 'firmware_bytes', 'original_sha256', 'result_sha256')}, indent=2))

if __name__ == '__main__':
    main()
