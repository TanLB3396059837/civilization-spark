#!/usr/bin/env bash
# Local RoCE/RDMA capability check. It does not modify network settings.
set -euo pipefail

echo "== RDMA devices =="
command -v ibv_devices >/dev/null 2>&1 || {
    echo "ibv_devices not found; install rdma-core / libibverbs-dev"
    exit 2
}
ibv_devices || true

echo
echo "== Device details =="
ibv_devinfo || true

echo
echo "== RDMA links =="
if command -v rdma >/dev/null 2>&1; then
    rdma link show || true
else
    echo "rdma tool not found"
fi

echo
echo "== GID table =="
if command -v show_gids >/dev/null 2>&1; then
    show_gids || true
else
    echo "show_gids not found (usually from rdma-core/perftest)"
fi

echo
echo "Next: run perftest on two hosts:"
echo "  server: ib_send_bw -d <dev> -x <gid>"
echo "  client: ib_send_bw -d <dev> -x <gid> <server-ip>"
