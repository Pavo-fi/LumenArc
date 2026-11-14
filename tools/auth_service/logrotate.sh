#!/bin/bash
# Truncate auth.log when it exceeds 5 MB (keeps the running process fd valid).
cd "$HOME/lumenarc-auth" || exit 1
size=$(stat -c%s log/auth.log 2>/dev/null || echo 0)
if [ "$size" -gt 5242880 ]; then
  cp -f log/auth.log log/auth.log.1
  : > log/auth.log
  echo "$(date '+%F %T') rotated ($size bytes)"
fi
