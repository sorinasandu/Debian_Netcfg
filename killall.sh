#!/bin/sh
# Killall for dhcp clients.

pids=$(ps ax | grep 'udhcpc\|dhclient\|pump' | grep -v grep | sed 's/^[ ]*\([0-9]*\).*/\1/')

for pid in $pids; do
  if kill -0 $pid 2>/dev/null; then
    kill -TERM $pid
    sleep 1
    # Still alive? Die!
    if kill -0 $pid 2>/dev/null; then
      kill -KILL $pid
    fi
  fi
done
