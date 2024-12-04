#!/bin/sh

set -e

# Error if we're not running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root" 1>&2
    exit 1
fi

# Enable verbose DRM logging.
echo 0x1FF >/sys/module/drm/parameters/debug

# Clear kernel logs
dmesg -C

# Continuously echo kernel logs in background
dmesg -w &
PID=$!

# Run the program given as arguments
set +e
$@

# kill dmesg
kill $PID

# Disable DRM logging
echo 0 > /sys/module/drm/parameters/debug
