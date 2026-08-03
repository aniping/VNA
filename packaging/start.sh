#!/bin/sh
# Keep the offline Linux launcher POSIX-sh compatible and LF-terminated.
set -u

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
printf '%s\n' 'Starting Vector Network Analyzer'
printf '%s\n' 'Web URL: http://127.0.0.1:8080/'
printf 'Log file: %s\n' "$script_dir/logs/vna.log"
"$script_dir/bin/vna-server"
exit_code=$?
if [ "$exit_code" -ne 0 ]; then
    printf 'ERROR: Vector Network Analyzer exited with code %s.\n' \
        "$exit_code" >&2
fi
exit "$exit_code"
