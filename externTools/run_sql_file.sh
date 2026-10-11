#!/usr/bin/env bash
# ./externTools/run_sql_file.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
# 修改这里即可切换默认测试文件，也可以通过环境变量覆盖。
SQL_FILE="${SQL_FILE:-test/case/debug/function.sql}"
# 单条 SQL 最多等待 1 小时；调试时可通过环境变量覆盖。
SQL_TIMEOUT="${SQL_TIMEOUT:-3600}"

if [[ $# -gt 0 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
    exec python3 "${SCRIPT_DIR}/run_sql_file.py" "$@"
fi

if [[ -z "${SQL_FILE}" && $# -gt 0 && "$1" != -* ]]; then
    SQL_FILE="$1"
    shift
fi

if [[ -z "${SQL_FILE}" ]]; then
    echo "Usage: $0 [options] SQL_FILE" >&2
    echo "Or set SQL_FILE, for example: SQL_FILE=test/case/debug/date.sql $0" >&2
    exit 2
fi

cd -- "${REPO_DIR}"
exec python3 "${SCRIPT_DIR}/run_sql_file.py" \
    --timeout "${SQL_TIMEOUT}" \
    "$@" \
    "${SQL_FILE}"
