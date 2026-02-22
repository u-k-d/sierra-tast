import copy
import json
from pathlib import Path

from hash_semantics import contract_sha256, doc_sha256, extract_contract_block


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    contracts = sorted((ROOT / "contracts").glob("L*_*.manifest.json"))
    docs = sorted((ROOT / "docs" / "normative").glob("L*_*.md"))
    if len(contracts) != 9 or len(docs) != 9:
        raise SystemExit("expected 9 contracts and 9 normative docs")

    failures: list[str] = []

    # Semantics proof: doc-only changes impact doc hash, not contract hash.
    sample_doc = docs[0]
    sample_text = sample_doc.read_text(encoding="utf-8")
    sample_contract = extract_contract_block(sample_text)
    base_contract_hash = contract_sha256(sample_contract)
    base_doc_hash = doc_sha256(sample_text)

    changed_doc_hash = doc_sha256(sample_text + "\nHash semantics probe line.\n")
    if changed_doc_hash == base_doc_hash:
        failures.append("doc-only mutation did not change doc_sha256")
    if contract_sha256(sample_contract) != base_contract_hash:
        failures.append("doc-only mutation unexpectedly changed contract_sha256")

    # Semantics proof: contract-only changes impact contract hash, doc hash unchanged for unchanged doc.
    mutated_contract = copy.deepcopy(sample_contract)
    mutated_contract["layer_version"] = int(mutated_contract["layer_version"]) + 1
    if contract_sha256(mutated_contract) == base_contract_hash:
        failures.append("contract-only mutation did not change contract_sha256")
    if doc_sha256(sample_text) != base_doc_hash:
        failures.append("contract-only mutation unexpectedly changed doc_sha256")

    # Repository-level verification.
    for manifest_path in contracts:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        doc_path = ROOT / "docs" / "normative" / f"{manifest_path.stem.replace('.manifest', '')}.md"
        doc_text = doc_path.read_text(encoding="utf-8")
        hashes = manifest.get("hashes", {})
        expected_contract = contract_sha256(manifest)
        expected_doc = doc_sha256(doc_text)
        if hashes.get("contract_sha256") != expected_contract:
            failures.append(f"{manifest['layer_id']}: contract_sha256 mismatch")
        if hashes.get("doc_sha256") != expected_doc:
            failures.append(f"{manifest['layer_id']}: doc_sha256 mismatch")

    if failures:
        for f in failures:
            print(f)
        return 2

    print("hash semantics checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
