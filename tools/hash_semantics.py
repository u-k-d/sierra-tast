import copy
import hashlib
import json
import re


ZERO_SHA256 = "sha256:" + ("0" * 64)
_DOC_BLOCK_RE = re.compile(r"```json\n(.*?)\n```", re.DOTALL)


def _canonical_contract(contract: dict) -> str:
    payload = copy.deepcopy(contract)
    payload.pop("hashes", None)
    return json.dumps(payload, ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def contract_sha256(contract: dict) -> str:
    canonical = _canonical_contract(contract)
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def extract_contract_block(doc_text: str) -> dict:
    match = _DOC_BLOCK_RE.search(doc_text)
    if not match:
        raise ValueError("missing JSON contract block in doc")
    return json.loads(match.group(1))


def render_doc_with_contract(doc_text: str, contract: dict) -> str:
    block = "```json\n" + json.dumps(contract, indent=2, ensure_ascii=True) + "\n```"
    replaced, count = _DOC_BLOCK_RE.subn(block, doc_text, count=1)
    if count != 1:
        raise ValueError("expected exactly one JSON contract block in doc")
    return replaced


def normalized_doc_text_for_hash(doc_text: str) -> str:
    contract = extract_contract_block(doc_text)
    contract["hashes"] = {
        "doc_sha256": ZERO_SHA256,
        "contract_sha256": ZERO_SHA256,
    }
    return render_doc_with_contract(doc_text, contract)


def doc_sha256(doc_text: str) -> str:
    normalized = normalized_doc_text_for_hash(doc_text)
    return "sha256:" + hashlib.sha256(normalized.encode("utf-8")).hexdigest()
