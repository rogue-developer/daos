#!/bin/bash

set -eux

for i in 0 1; do
    if [ -e /sys/class/net/ib$i ]; then
        if ! ifconfig ib$i | grep "inet "; then
          {
            echo "Found interface ib$i down after reboot on $HOSTNAME"
            systemctl status || true
            systemctl --failed || true
            journalctl -n 500 || true
            ifconfig ib$i || true
            cat /sys/class/net/ib$i/mode || true
            ifup ib$i || true
            ifconfig -a || true
            cat /etc/sysconfig/network-scripts/ifcfg-ib$1
            if ! ifconfig ib$i | grep "inet "; then
                echo "Failed to bring up interface"
                exit 1
            fi
          } # | mail -s "Interface found down after reboot" "$OPERATIONS_EMAIL"
        fi
    fi
done

if ! grep /mnt/share /proc/mounts; then
    mkdir -p /mnt/share
    mount "$FIRST_NODE":/export/share /mnt/share
fi
