#!/bin/sh
set -u

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
log_file="$script_dir/logs/vna.log.jsonl"
printf '%s\n' 'Starting Vector Network Analyzer'
printf '%s\n' 'Web URL: http://127.0.0.1:8080/'
printf 'Log file: %s\n' "$log_file"
"$script_dir/bin/vna-server"
exit_code=$?
if [ "$exit_code" -ne 0 ]; then
    printf 'ERROR: Vector Network Analyzer exited with code %s. Log file: %s\n' \
        "$exit_code" "$log_file" >&2
fi
exit "$exit_code"
