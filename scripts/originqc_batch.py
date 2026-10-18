#!/usr/bin/env python3
"""Submit a batch of single-qubit H+measure programs to Origin Quantum Cloud.

OriginQC batch jobs require shots in [10, 49600]. The API returns per-program
probability vectors; raw counts are not exposed by this endpoint.

Example:
  ORIGIN_API_KEY=... python scripts/originqc_batch.py \
      --backend WK_C180 --programs 16 --shots 10 --output originqc_batch.json
"""

import argparse
import hashlib
import json
import os
import time


def normalize_probs(probs):
    if isinstance(probs, dict):
        return float(probs.get("0", probs.get(0, 0.0))), float(
            probs.get("1", probs.get(1, 0.0))
        )
    return float(probs[0]), float(probs[1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", default="WK_C180")
    parser.add_argument("--programs", type=int, default=16)
    parser.add_argument("--shots", type=int, default=10)
    parser.add_argument("--output", default="originqc_batch.json")
    parser.add_argument("--amend", action="store_true",
                        help="enable readout-error mitigation (calibrated probabilities)")
    parser.add_argument("--no-amend", action="store_true",
                        help=argparse.SUPPRESS)
    args = parser.parse_args()

    shots = max(10, args.shots)
    if shots != args.shots:
        print(f"shots raised to {shots} (OriginQC minimum is 10)")
    if args.programs <= 0:
        raise SystemExit("--programs must be positive")

    key = os.environ.get("ORIGIN_API_KEY")
    if not key:
        raise SystemExit("set ORIGIN_API_KEY")

    from pyqpanda3 import qcloud
    from pyqpanda3.core import H, QProg, measure

    service = qcloud.QCloudService(api_key=key)
    backends = service.backends()
    print("backends:", backends)
    if args.backend not in backends or not backends[args.backend]:
        raise SystemExit(f"backend unavailable: {args.backend}")

    backend = service.backend(args.backend)
    progs = []
    for _ in range(args.programs):
        prog = QProg()
        prog << H(0) << measure(0, 0)
        progs.append(prog)

    options = qcloud.QCloudOptions()
    options.set_is_prob_counts(False)
    options.set_mapping(True)
    options.set_amend(args.amend and not args.no_amend)

    job = backend.run(progs, shots, options)
    print("job_id:", job.job_id())

    status = job.status()
    for _ in range(180):
        status = job.status()
        if "FINISHED" in str(status) or "FAILED" in str(status):
            break
        time.sleep(1)
    print("status:", status)

    result = job.result()
    probs_list = result.get_probs_list()
    if not probs_list:
        single = result.get_probs()
        probs_list = [single] if single else []

    bits = []
    counts_list = []
    for probs in probs_list:
        p0, p1 = normalize_probs(probs)
        bits.append(0 if p0 >= p1 else 1)
        n0 = max(0, min(shots, round(p0 * shots)))
        counts_list.append({"0": n0, "1": shots - n0})

    origin = result.origin_data
    if callable(origin):
        origin = origin()

    record = {
        "backend": args.backend,
        "job_id": job.job_id(),
        "status": str(status),
        "programs": args.programs,
        "shots_per_program": shots,
        "probs_list": probs_list,
        "counts_list": counts_list,
        "majority_bits": bits,
        "majority_ones_ratio": (sum(bits) / len(bits)) if bits else None,
        "total_counts": {
            "0": sum(c["0"] for c in counts_list),
            "1": sum(c["1"] for c in counts_list),
        },
        "shot_ones_ratio": (
            sum(c["1"] for c in counts_list) / (len(counts_list) * shots)
            if counts_list else None
        ),
        "origin_data": origin,
    }
    seed_material = job.job_id() + json.dumps(probs_list, sort_keys=True)
    record["derived_seed"] = (
        int.from_bytes(hashlib.sha256(seed_material.encode()).digest()[:8], "big")
        & 0x7FFFFFFFFFFFFFFF
    )

    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(record, fh, ensure_ascii=False, indent=2)

    print("programs:", len(probs_list))
    print("majority_bits:", "".join(str(b) for b in bits))
    print("total_counts:", record["total_counts"])
    print("majority_ones_ratio:", record["majority_ones_ratio"])
    print("shot_ones_ratio:", record["shot_ones_ratio"])
    print("derived_seed:", record["derived_seed"])
    print("written:", args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
