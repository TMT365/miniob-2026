#!/bin/bash
# clean_db.sh 清理MiniOB数据库文件
DB_DIR="/home/tmt/miniob/miniob/db/sys"

if [ -d "$DB_DIR" ]; then
    rm -rf "$DB_DIR"
    echo "Removed db directory"
fi
echo "✅ Clean complete"
