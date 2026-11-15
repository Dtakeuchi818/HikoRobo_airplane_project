#!/usr/bin/env bash
set -euo pipefail
IFACE="${1:-}"
nmcli device status
if [[ -n "$IFACE" ]]; then
    nmcli -f GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS device show "$IFACE"
    ip -4 addr show dev "$IFACE"
fi
