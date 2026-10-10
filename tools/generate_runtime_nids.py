"""Generate names for implemented RuntimeExport contracts, independently of a dump.

Run after adding/removing an export: python tools/generate_runtime_nids.py
--check validates the checked-in table without modifying it.
"""
import argparse
import base64
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SALT = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")


def generated():
    symbols = {}
    known = {}
    for nid, name in re.findall(r'\{"([^"]+)","([^"]+)"\}', (ROOT / "src/import_names.inc").read_text(encoding='utf-8')):
        known.setdefault(name, set()).add(nid[nid.index("#"):])
    defaults = {
        "runtime_kernel.c": {"#p#J", "#I#J"},
        "runtime_file.c": {"#p#J", "#I#J"},
        "runtime_savedata.c": {"#O#P"},
        "runtime_rtc.c": {"#H#I"},
    }
    for source in (ROOT / "src").glob("runtime*.c"):
        for table in re.findall(r"RuntimeExport\s+\w+\[\]\s*=\s*\{(.*?)\};", source.read_text(encoding='utf-8'), re.S):
            for name in re.findall(r'\{\s*"([^"]+)"\s*,', table):
                if "#" not in name:
                    scopes = known.get(name, defaults.get(source.name, set()))
                    symbols.setdefault(name, set()).update(scopes)
    rows = ["/* Generated from implemented RuntimeExport tables; tools/generate_runtime_nids.py. */"]
    for symbol in sorted(symbols):
        digest = hashlib.sha1(symbol.encode("ascii") + SALT).digest()[:8][::-1]
        nid = base64.b64encode(digest).decode("ascii").rstrip("=").replace("/", "-")
        for scope in sorted(symbols[symbol]):
            rows.append(f'{{"{nid}{scope}","{symbol}"}},')
    return "\n".join(rows) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    path = ROOT / "src/runtime_contract_names.inc"
    result = generated()
    if args.check:
        if not path.exists() or path.read_text(encoding='utf-8') != result:
            raise SystemExit("Runtime NID table is stale; run tools/generate_runtime_nids.py")
    else:
        path.write_text(result, encoding='utf-8')
