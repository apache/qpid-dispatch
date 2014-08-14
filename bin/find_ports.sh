#!/bin/bash
# Usage: $0 [ dir ]
# Find all the listening ports mentioned in *.log files under dir.
# With no dir search under current directory.

find "$@" -name '*.log' | xargs gawk 'match($0, /Listening on .* ([0-9]+)/, m) { print m[1] } match($0, /Configured Listener: .*:([0-9]+)/, m) { print m[1] }'

