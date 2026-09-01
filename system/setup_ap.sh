#!/usr/bin/env bash
# Create a persistent WiFi access point on the Pi ("firelink" network) for
# the pre-flight configuration page. Requires Raspberry Pi OS Bookworm or
# newer (NetworkManager). Run once:
#   sudo bash system/setup_ap.sh
set -euo pipefail

SSID="${1:-firelink}"
PSK="${2:-firelink123}"

if ! command -v nmcli >/dev/null 2>&1; then
    echo "nmcli not found. This script needs NetworkManager (Raspberry Pi OS"
    echo "Bookworm+). On older images use hostapd + dnsmasq instead."
    exit 1
fi

# Recreate the connection profile if it already exists.
nmcli con delete firelink-ap 2>/dev/null || true

nmcli con add type wifi ifname wlan0 con-name firelink-ap autoconnect yes ssid "$SSID"
nmcli con modify firelink-ap 802-11-wireless.mode ap 802-11-wireless.band bg
nmcli con modify firelink-ap wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PSK"
nmcli con modify firelink-ap ipv4.method shared ipv6.method disabled

nmcli con up firelink-ap

echo
echo "Access point '$SSID' is up (password: $PSK)."
echo "The Pi is reachable at http://10.42.0.1 once connected."
