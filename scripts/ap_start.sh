#!/usr/bin/env bash
set -euo pipefail

# Usage: bash scripts/ap_start.sh wlp3s0
if [[ $# -ne 1 ]]; then
  echo "Usage: bash scripts/ap_start.sh <wifi-interface>" >&2
  nmcli device status
  exit 2
fi
IFACE="$1"
CONNECTION="HikoRobo-Hotspot"
SSID="HikoRobo_Lab"

if nmcli -g GENERAL.CONNECTION device show "$IFACE" >/dev/null 2>&1; then
  : # Interface exists.
else
  echo "Wi-Fi interface not found: $IFACE" >&2
  exit 2
fi

if nmcli -t -f NAME connection show | grep -Fxq "$CONNECTION"; then
  nmcli connection up "$CONNECTION" ifname "$IFACE"
else
  read -r -s -p "Set WPA2 Wi-Fi password (8-63 characters): " WIFI_PASSWORD
  printf '\n'
  if [[ ${#WIFI_PASSWORD} -lt 8 || ${#WIFI_PASSWORD} -gt 63 ]]; then
    echo "Password length must be 8-63 characters" >&2
    exit 2
  fi
  nmcli device wifi hotspot \
    ifname "$IFACE" \
    con-name "$CONNECTION" \
    ssid "$SSID" \
    band bg \
    password "$WIFI_PASSWORD"
  # NetworkManager's shared IPv4 mode runs the local DHCP service.
  nmcli connection modify "$CONNECTION" \
    802-11-wireless-security.key-mgmt wpa-psk \
    ipv4.method shared
  nmcli connection up "$CONNECTION" ifname "$IFACE"
  unset WIFI_PASSWORD
fi

echo "--- Connection / IPv4 ---"
nmcli -f GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS device show "$IFACE"
echo "--- IPv4 / broadcast ---"
ip -4 addr show dev "$IFACE"
echo "A separate internet uplink is NOT required for local UDP."
