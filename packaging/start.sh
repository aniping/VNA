#!/bin/sh
set -u

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
text_log="$script_dir/logs/vna.log"
structured_log="$script_dir/logs/vna.jsonl"
printf '%s\n' 'Starting Vector Network Analyzer'
printf '%s\n' 'Web URL: http://127.0.0.1:8080/'
printf 'Text log: %s\n' "$text_log"
printf 'Structured log: %s\n' "$structured_log"
"$script_dir/bin/vna-server"
exit_code=$?
if [ "$exit_code" -ne 0 ]; then
    printf 'ERROR: Vector Network Analyzer exited with code %s. Text log: %s\n' \
        "$exit_code" "$text_log" >&2
fi
exit "$exit_code"
