#!/usr/bin/env python3
"""Probe Origin Quantum Cloud with a minimal H + measure circuit.

The API key is read from ORIGIN_API_KEY and is never written to disk.

Examples:
  ORIGIN_API_KEY=... python scripts/originqc_probe.py --list
  ORIGIN_API_KEY=... python scripts/originqc_probe.py --backend full_amplitude --shots 1024
  ORIGIN_API_KEY=... python scripts/originqc_probe.py --backend WK_C180 --shots 100
"""

import argparse
import os
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", default="full_amplitude")
    parser.add_argument("--shots", type=int, default=1024)
    parser.add_argument("--url", default="http://pyqanda-admin.qpanda.cn")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    key = os.environ.get("ORIGIN_API_KEY")
    if not key:
        raise SystemExit("set ORIGIN_API_KEY")

    from pyqpanda3 import qcloud
    from pyqpanda3.core import H, QProg, measure

    service = qcloud.QCloudService(api_key=key, url=args.url)
    backends = service.backends()
    print("backends:", backends)
    if args.list:
        return 0

    if args.backend not in backends:
        raise SystemExit(f"unknown backend: {args.backend}")
    if not backends[args.backend]:
        raise SystemExit(f"backend offline: {args.backend}")

    backend = service.backend(args.backend)
    print("selected:", backend.name())

    prog = QProg()
    prog << H(0) << measure(0, 0)

    if args.backend == "full_amplitude":
        job = backend.run(prog, args.shots)
    else:
        options = qcloud.QCloudOptions()
        options.set_amend(True)
        options.set_mapping(True)
        job = backend.run(prog, args.shots, options)

    print("job_id:", job.job_id())
    print("status:", job.status())

    # Give the remote scheduler a short window, then let result() wait.
    for _ in range(10):
        time.sleep(1)
        print("status:", job.status())

    result = job.result()
    print("counts:", result.get_counts())
    try:
        print("probs:", result.get_probs())
    except Exception as exc:
        print("probs unavailable:", exc)
    for name in ("get_counts_list", "prob_count_raw", "origin_data",
                 "timing_info", "job_status"):
        try:
            value = getattr(result, name)
            if callable(value):
                value = value()
            print(f"{name}:", value)
        except Exception as exc:
            print(f"{name} unavailable:", exc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
