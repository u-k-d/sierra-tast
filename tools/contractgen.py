import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

for md in sorted((ROOT / "docs" / "normative").glob("L*_*.md")):
    text = md.read_text(encoding="utf-8")
    m = re.search(r"```json\n(.*?)\n```", text, re.DOTALL)
    if not m:
        raise SystemExit(f"contract block not found: {md}")
    contract = json.loads(m.group(1))
    out = ROOT / "contracts" / f"{md.stem}.manifest.json"
    out.write_text(json.dumps(contract, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

print("contract extraction complete")
