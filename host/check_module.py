"""Check a .wlm against an actual linked-core compatibility.json, without execution."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from module_header import validate_package
from module_compatibility import CompatibilityError,validate_environment


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module',type=Path)
    parser.add_argument('--core-profile',type=Path,required=True)
    args=parser.parse_args()
    try:
        environment=validate_environment(json.loads(args.core_profile.read_text(encoding='utf-8')))
        if args.module.stat().st_size>1024*1024:raise CompatibilityError(1)
        data=args.module.read_bytes()
        try:
            info=validate_package(data,bytes.fromhex(environment['module_identity']),capabilities=environment['capabilities'],environment=environment)
        except CompatibilityError:
            raise
        except ValueError as error:
            print(json.dumps(dict(compatible=False,issue=dict(reason='invalid_package',code=1),error=str(error))));return 1
    except CompatibilityError as error:
        print(json.dumps(dict(compatible=False,issue=error.issue)));return 1
    except (OSError,ValueError) as error:
        print(json.dumps(dict(compatible=False,error=str(error))));return 2
    print(json.dumps(dict(compatible=True,module_sha256=hashlib.sha256(data).hexdigest(),
        core_sha256=environment.get('core_sha256'),declared_contract_checked=bool(info.get('compatibility_checked')),preflight=info)))
    return 0


if __name__=='__main__':sys.exit(main())
