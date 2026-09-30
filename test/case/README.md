# MiniOB 自动化测试框架

## 概述

`miniob_test.py` 是 MiniOB 的自动化功能测试脚本，用于验证 SQL 语句执行的正确性。

## 工作流程

```
┌─────────────────────────────────────────────────────────────────┐
│                    miniob_test.py 执行流程                        │
├─────────────────────────────────────────────────────────────────┤
│  1. [编译阶段]  cmake -B build_debug/ && make -C build_debug/  │
│  2. [启动服务器]  启动 observer 进程监听 unix socket/TCP 端口    │
│  3. [加载测试用例]  扫描 test/case/test/ 目录下的 .test 文件    │
│  4. [执行测试用例]  逐行解析 .test 文件并执行                   │
│  5. [结果比较]  compare(实际结果.tmp, 期望结果.result)          │
│  6. [输出报告]  统计 PASS/FAIL/Timeout                          │
└─────────────────────────────────────────────────────────────────┘
```

## 默认目录配置

| 目录/文件 | 默认路径 | 说明 |
|-----------|---------|------|
| 项目根目录 | 自动检测 `test/case/../..` | `/home/tmt/miniob` |
| 编译输出目录 | `{project_dir}/build_debug/` | observer 在 `bin/observer` |
| 数据目录 | `{project_dir}/data/` | 测试产生的表数据、索引文件 |
| 配置文件 | `{project_dir}/etc/observer.ini` | observer 配置 |
| 日志输出 | `stdout` | 可用 `--log` 指定 |
| 结果 tmp | `{work_dir}/result_output/` | 临时结果文件 |

## 运行命令

```bash
# 运行所有测试（会编译）
python3 test/case/miniob_test.py

# 运行指定测试（会编译）
python3 test/case/miniob_test.py --test-case=basic

# 推荐：已编译后跳过编译
python3 test/case/miniob_test.py --test-case=basic --skip-compile

# 多测试（逗号分隔）
python3 test/case/miniob_test.py --test-case=basic,primary-update

# 仅生成结果，不比对
python3 test/case/miniob_test.py --test-case=my_test --report-only

# 调试模式（保留数据）
python3 test/case/miniob_test.py --test-case=basic -d

# 查看 SQL 执行日志
python3 test/case/miniob_test.py --test-case=basic --log stdout

# 指定工作目录
python3 test/case/miniob_test.py --work-dir=/custom/path
```

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--test-case` | None | 指定运行的测试用例 |
| `--skip-compile` | False | 跳过编译，使用已编译的二进制 |
| `--compile-rebuild` | False | 强制重建（先清理再编译） |
| `--report-only` | False | 仅生成结果，不比对 |
| `--not-use-unix-socket` | False | 使用 TCP 端口而非 Unix Socket |
| `--log` | stdout | 日志输出位置 |
| `--work-dir` | `{project_dir}/build_debug/` | 工作目录 |
| `-d/--debug` | False | 调试模式，保留数据 |
| `--server-port` | 16888 | TCP 监听端口 |

## 测试用例格式 (.test 文件)

### 命令类型

| 类型 | 示例 | 说明 |
|------|------|------|
| SQL 语句 | `CREATE TABLE t1(id int);` | 直接发送给 observer 执行 |
| echo 命令 | `-- echo 创建表` | 仅输出描述文字，不执行 SQL |
| sort 命令 | `-- sort SELECT * FROM t1;` | 执行 SQL，结果排序后输出 |
| ensure 命令 | `-- ensure:hashjoin SELECT ...` | 检查执行计划是否符合预期 |
| connection | `-- connection conn1` | 切换到指定连接 |
| connect | `-- connect conn1` | 创建新连接 |
| 注释 | `# 这是注释` | 忽略，不执行 |

### 示例

```sql
-- echo 1. 创建表
CREATE TABLE t1(id int, name char(20));

-- echo 2. 插入数据
INSERT INTO t1 VALUES (1, 'Alice');

-- echo 3. 查询数据（排序输出）
-- sort SELECT * FROM t1;
```

## 结果文件

| 文件 | 位置 | 说明 |
|------|------|------|
| `.test` | `test/case/test/` | 测试用例（输入） |
| `.result` | `test/case/result/` | 期望结果（标准答案） |
| `.result.tmp` | `{work_dir}/result_output/` | 实际运行结果 |

测试执行后逐行比较 `.result.tmp` 和 `.result`，完全一致 → PASS，否则 → FAIL。

## 目录结构

```
miniob/
├── build_debug/              # 编译输出（默认）
│   └── bin/observer        # 可执行文件
├── data/                   # 数据目录（默认）
│   └── miniob/db/
│       ├── sys/            # 系统数据库
│       └── *.data/*.table/*.index  # 表数据和索引
├── etc/observer.ini        # 配置文件
├── test/case/
│   ├── README.md          # 本文档
│   ├── miniob_test.py     # 测试脚本
│   ├── test/              # 测试用例 (*.test)
│   └── result/            # 期望结果 (*.result)
└── logs/observer.log     # 日志
```

## 数据文件说明

测试产生的实际数据文件位于 `data/miniob/db/` 目录下：

| 文件类型 | 扩展名 | 说明 |
|---------|--------|------|
| 表数据 | `.data` | 堆表记录数据 |
| 表元数据 | `.table` | 表结构定义 |
| 索引数据 | `.index` | B+Tree 索引 |
| 双重写缓冲 | `dblwr.db` | 崩溃恢复用 |
| LSM 数据 | `lsm/` | LSM-Tree 存储 |

例如：`data/miniob/db/sys/Update_table_1.data`

## 常用命令参考

```bash
# 基本运行
python3 test/case/miniob_test.py --test-case=basic

# 推荐：已编译后跳过编译
python3 test/case/miniob_test.py --test-case=basic --skip-compile

# 多测试
python3 test/case/miniob_test.py --test-case=basic,primary-update

# 仅报告（不比对）
python3 test/case/miniob_test.py --test-case=basic --report-only

# 调试模式
python3 test/case/miniob_test.py --test-case=basic -d

# 查看 SQL 执行日志
python3 test/case/miniob_test.py --test-case=basic --log stdout

# 指定工作目录
python3 test/case/miniob_test.py --work-dir=/custom/path

# 指定端口
python3 test/case/miniob_test.py --server-port=6789
```

## 注意事项

1. **每个测试用例独立**：每个 .test 文件执行前会重启 observer 进程，保证用例之间互不影响。

2. **结果比较是严格逐行比较**：`.result.tmp` 和 `.result` 必须完全一致（包括空行）。

3. **使用 --report-only 生成新结果**：新写一个 .test 后，用此参数运行会自动生成 .result 文件。

4. **调试模式 -d 保留数据**：不会清理 `data/` 目录，方便排查问题。

5. **推荐使用 --skip-compile**：已编译后运行时加上此参数，跳过编译加速测试。

6. **Unix Socket vs TCP**：默认使用 Unix Socket，Windows 环境可能需要 `--not-use-unix-socket`。

7. **结果比较输出行号**：比对失败时会显示具体行号，格式为 `line{N}=内容`，便于定位问题。

## SQL 执行输出颜色

执行 SQL 时会在屏幕显示带颜色的输出：

| 类型 | 颜色 | 示例 |
|------|------|------|
| SQL 语句 | 青色 | `正在执行 SQL: CREATE TABLE t1(id int);` |
| SUCCESS | 绿色 | `执行输出: SUCCESS` |
| 其他结果 | 红色 | `执行输出: FAILURE` |

**注意**：颜色输出默认显示在屏幕（stdout），不受 `--log` 参数影响。日志文件仍通过 logger 写入。

