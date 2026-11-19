#!/usr/bin/env bash
# Sends a stream of small SOL transfers to a local test validator, so the
# scheduler has real traffic to read.
#
# usage: scripts/traffic.sh [count] [per_second]
#
# The validator must be started with this keypair as the genesis mint, so the
# sender is funded:
#   solana-test-validator --ledger ~/.tvl --reset --enable-scheduler-bindings \
#     --mint "$(solana address -k ~/.config/solana/id.json)"
set -euo pipefail

count=${1:-100}
per_second=${2:-10}
keypair=${KEYPAIR:-$HOME/.config/solana/id.json}
url=http://127.0.0.1:8899

me=$(solana address -k "$keypair")
delay=$(echo "scale=3; 1 / $per_second" | bc)

for i in $(seq 1 "$count"); do
    # i lamports: a different amount each time keeps every signature unique.
    # --no-wait: the scheduler doesn't execute txs yet, so they never land.
    solana transfer "$me" "$(printf '0.%09d' "$i")" \
        --keypair "$keypair" --url "$url" \
        --allow-unfunded-recipient --no-wait >/dev/null 2>&1 &
    sleep "$delay"
done
wait
echo "sent $count transfers"
