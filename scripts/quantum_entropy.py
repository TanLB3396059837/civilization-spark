#!/usr/bin/env python3
"""Generate quantum-derived random bytes and a 64-bit seed.

Examples:
  python scripts/quantum_entropy.py --shots 4096
  IBM_QUANTUM_TOKEN=... python scripts/quantum_entropy.py --backend ibm \
      --backend-name ibm_brisbane --shots 4096
"""

import argparse
import os
import sys


def build_circuit():
    from qiskit import QuantumCircuit

    qc = QuantumCircuit(1, 1)
    qc.h(0)
    qc.measure(0, 0)
    return qc


def sample_aer(qc, shots):
    from qiskit_aer import AerSimulator

    return AerSimulator().run(qc, shots=shots, memory=True).result().get_memory()


def sample_ibm(qc, shots, backend_name):
    token = os.environ.get("IBM_QUANTUM_TOKEN")
    if not token:
        raise RuntimeError("set IBM_QUANTUM_TOKEN for IBM Quantum")

    from qiskit import transpile
    from qiskit_ibm_runtime import QiskitRuntimeService, SamplerV2

    service = QiskitRuntimeService(channel="ibm_quantum", token=token)
    backend = service.backend(backend_name)
    transpiled = transpile(qc, backend=backend)
    sampler = SamplerV2(mode=backend)
    result = sampler.run([transpiled], shots=shots).result()
    return result[0].data.c.get_bitstrings()


def bitstrings_to_bytes(bitstrings):
    bits = "".join(bitstrings)
    usable = len(bits) - (len(bits) % 8)
    if usable == 0:
        return b""
    return int(bits[:usable], 2).to_bytes(usable // 8, "big")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=["aer", "ibm"], default="aer")
    parser.add_argument("--backend-name", default="ibm_brisbane")
    parser.add_argument("--shots", type=int, default=4096)
    parser.add_argument("--output", default="quantum_entropy.bin")
    args = parser.parse_args()

    try:
        qc = build_circuit()
        if args.backend == "ibm":
            bitstrings = sample_ibm(qc, args.shots, args.backend_name)
        else:
            bitstrings = sample_aer(qc, args.shots)
    except ImportError as exc:
        print(f"missing dependency: {exc}", file=sys.stderr)
        print("install: pip install qiskit qiskit-aer qiskit-ibm-runtime", file=sys.stderr)
        return 2

    data = bitstrings_to_bytes(bitstrings)
    if not data:
        print("no usable bits", file=sys.stderr)
        return 1

    with open(args.output, "wb") as fh:
        fh.write(data)

    ones = sum(b.bit_count() for b in data)
    total = len(data) * 8
    seed = int.from_bytes(data[:8].ljust(8, b"\0"), "big")
    print(f"backend={args.backend} shots={len(bitstrings)} bytes={len(data)}")
    print(f"ones_ratio={ones / total:.4f} seed={seed}")
    print(f"written={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
