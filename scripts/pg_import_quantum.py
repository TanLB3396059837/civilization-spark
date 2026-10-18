#!/usr/bin/env python3
"""Import an OriginQC batch JSON result into the civ PostgreSQL database.

Usage:
  CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass" \
    python scripts/pg_import_quantum.py \
      --json originqc_batch_raw.json --run-id 31
"""

import argparse
import json
import os


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", default="originqc_batch_raw.json")
    parser.add_argument("--run-id", type=int, default=-1)
    args = parser.parse_args()

    dsn = os.environ.get("CIV_PG_DSN")
    if not dsn:
        raise SystemExit("set CIV_PG_DSN")
    if not os.path.exists(args.json):
        raise SystemExit(f"missing JSON: {args.json}")

    try:
        import psycopg
    except ImportError:
        raise SystemExit("install psycopg: pip install 'psycopg[binary]'")

    with open(args.json, "r", encoding="utf-8") as fh:
        record = json.load(fh)

    origin = record.get("origin_data")
    if isinstance(origin, str):
        try:
            origin = json.loads(origin)
        except json.JSONDecodeError:
            origin = {}
    obj = (origin or {}).get("obj", {})
    qpu_runtime_ms = float(obj.get("qpuRunTime") or 0.0)

    with psycopg.connect(dsn) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO quantum_jobs
                    (platform, platform_job_id, backend, programs,
                     shots_per_program, status, counts, probs,
                     qpu_runtime_ms, derived_seed, run_id)
                VALUES
                    ('originqc', %s, %s, %s, %s, %s,
                     %s::jsonb, %s::jsonb, %s, %s, NULLIF(%s, -1))
                ON CONFLICT (platform, platform_job_id) DO UPDATE SET
                    backend=EXCLUDED.backend,
                    programs=EXCLUDED.programs,
                    shots_per_program=EXCLUDED.shots_per_program,
                    status=EXCLUDED.status,
                    counts=EXCLUDED.counts,
                    probs=EXCLUDED.probs,
                    qpu_runtime_ms=EXCLUDED.qpu_runtime_ms,
                    derived_seed=EXCLUDED.derived_seed,
                    run_id=COALESCE(EXCLUDED.run_id, quantum_jobs.run_id),
                    updated_at=now()
                RETURNING quantum_job_id
                """,
                (
                    record.get("job_id", ""),
                    record.get("backend", ""),
                    int(record.get("programs", 0)),
                    int(record.get("shots_per_program", 0)),
                    record.get("status", ""),
                    json.dumps(record.get("counts_list", [])),
                    json.dumps(record.get("probs_list", [])),
                    qpu_runtime_ms,
                    int(record.get("derived_seed", 0)),
                    args.run_id,
                ),
            )
            quantum_job_id = cur.fetchone()[0]

            cur.execute(
                "DELETE FROM quantum_samples WHERE quantum_job_id=%s",
                (quantum_job_id,),
            )
            for i, counts in enumerate(record.get("counts_list", [])):
                for bitstring, count in counts.items():
                    cur.execute(
                        """
                        INSERT INTO quantum_samples
                            (quantum_job_id, program_index, bitstring, count)
                        VALUES (%s, %s, %s, %s)
                        ON CONFLICT (quantum_job_id, program_index, bitstring)
                        DO UPDATE SET count=EXCLUDED.count
                        """,
                        (quantum_job_id, i, str(bitstring), int(count)),
                    )

            if args.run_id > 0:
                cur.execute(
                    "UPDATE runs SET seed_source='originqc' WHERE run_id=%s",
                    (args.run_id,),
                )

    print(f"imported quantum_job_id={quantum_job_id} "
          f"job_id={record.get('job_id')} run_id={args.run_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
