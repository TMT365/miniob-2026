# MiniOB SQL 文件执行工具

`run_sql_file.py` 可以通过 `obclient` 连接已经启动的 MiniOB observer，并逐条执行 SQL 文件中的语句。

## 适用场景

这个工具适合在 CLion 中调试 observer 时使用：

1. 在 CLion 中启动或调试 observer：

   ```bash
   ./build_debug/bin/observer \
     -f ./etc/observer.ini \
     -s ./data/miniob.sock
   ```

2. 在同一个终端运行 SQL 文件执行器：

   ```bash
   ./externTools/run_sql_file.sh test/case/debug/date.sql
   ```

脚本不会启动 observer，只会启动一个临时的 `obclient` 连接已有的 Unix socket。运行结束后，临时 `obclient` 会自动退出。

## 默认配置

默认使用：

```text
obclient: ./build_debug/bin/obclient
socket:   ./data/miniob.sock
timeout:  20 秒
wait:     10 秒
```

通过 `run_sql_file.sh` 启动时，默认单条 SQL 响应超时为 `3600` 秒（1 小时），适合在 CLion 断点调试时等待 observer 继续运行。等待使用阻塞式 socket，不会因为超时设置变大而持续占用 CPU。

脚本会实际尝试连接 socket，而不是只检查 socket 文件是否存在。因此，即使 socket 文件残留，只要 observer 没有真正监听，脚本也会等待或报告连接失败。

## 命令行参数

```text
./externTools/run_sql_file.sh [options] SQL_FILE
```

默认 SQL 文件配置在 `externTools/run_sql_file.sh` 中：

```bash
SQL_FILE="${SQL_FILE:-test/case/debug/date.sql}"
```

以后可以直接修改 `.sh` 中的 `test/case/debug/date.sql`，切换默认测试文件。也可以把 SQL 文件路径放在外部变量中覆盖默认值：

```bash
SQL_FILE="test/case/debug/date.sql"
./externTools/run_sql_file.sh "$SQL_FILE"
```

也可以使用环境变量形式：

```bash
SQL_FILE="test/case/debug/other.sql" \
  ./externTools/run_sql_file.sh
```

常用选项：

| 选项 | 说明 | 默认值 |
| --- | --- | --- |
| `-c`, `--client` | `obclient` 可执行文件路径 | `./build_debug/bin/obclient` |
| `-s`, `--socket` | MiniOB Unix socket 路径 | `./data/miniob.sock` |
| `-t`, `--timeout` | 等待每条 SQL 响应的秒数 | `3600`（通过 `.sh` 启动时） |
| `--wait` | 等待 socket 接受连接的秒数 | `10` |
| `-h`, `--help` | 显示帮助 | - |

例如：

```bash
./externTools/run_sql_file.sh \
  --client ./build_debug/bin/obclient \
  --socket ./data/miniob.sock \
  --wait 60 \
  --timeout 30 \
  test/case/debug/date.sql
```

也可以临时设置超时时间：

```bash
SQL_TIMEOUT=60 ./externTools/run_sql_file.sh
```

调试时如果希望无限等待，可以设置为 `0`：

```bash
SQL_TIMEOUT=0 ./externTools/run_sql_file.sh
```

也可以直接调用 Python 脚本：

```bash
python3 ./externTools/run_sql_file.py test/case/debug/date.sql
```

## SQL 文件格式

每个非空、非注释行会作为一条 SQL 执行。支持以下注释形式：

```sql
-- 这是 SQL 注释
# 这也是注释

SELECT * FROM date_table;
```

目前建议一条 SQL 完整写在一行中。空行和注释行会被跳过，SQL 行末尾是否有分号不影响执行。

输出示例：

```text
[8] SQL> INSERT INTO date_table VALUES (1,'2020-01-21');

SUCCESS
[29] SQL> SELECT * FROM date_table WHERE u_date='2017-2-28';

id | u_date
```

如果 observer 未启动、socket 不可用，或者 observer 在执行 SQL 时断开连接，脚本会输出错误并返回非零退出码。
