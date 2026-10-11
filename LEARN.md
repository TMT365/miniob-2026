# MiniOB 存储结构详解

> 相关学习内容：[逻辑计划重写规则解读](#11-逻辑计划重写规则)

## 目录

1. [概述](#1-概述)
2. [BufferPool（缓冲池）](#2-bufferpool缓冲池)
3. [Page（页面）](#3-page页面)
4. [Frame（页帧）](#4-frame页帧)
5. [RecordManager（记录管理器）](#5-recordmanager记录管理器)
6. [RID（记录标识符）](#6-rid记录标识符)
7. [Record（记录）](#7-record记录)
8. [TableMeta（表元数据）](#8-tablemeta表元数据)
9. [B+Tree 索引](#9-btree-索引)
10. [各结构之间的关联](#10-各结构之间的关联)
11. [逻辑计划重写规则](#11-逻辑计划重写规则)
12. [表达式功能扩展：聚合函数与普通函数](#12-表达式功能扩展聚合函数与普通函数)

---

## 1. 概述

MiniOB 的存储结构分为多层：

```
┌─────────────────────────────────────────────────────────┐
│                     SQL Layer                          │
├─────────────────────────────────────────────────────────┤
│                   TableMeta                            │
│              (表的字段、索引、主键信息)                   │
├─────────────────────────────────────────────────────────┤
│              RecordManager / B+Tree Index               │
│         (记录管理)         (索引管理)                    │
├─────────────────────────────────────────────────────────┤
│                     BufferPool                          │
│                 (内存页管理、磁盘读写)                   │
├─────────────────────────────────────────────────────────┤
│                      Disk                               │
│               (.table 文件  /  .index 文件)              │
└─────────────────────────────────────────────────────────┘
```

- **.table 文件**：存储表的数据记录
- **.index 文件**：存储 B+Tree 索引

---

## 2. BufferPool（缓冲池）

### 2.1 作用

BufferPool 是**内存与磁盘之间的缓存层**，负责：
- 将磁盘上的页面加载到内存
- 将内存中的修改写回磁盘
- 管理页面在内存中的淘汰（LRU）

### 2.2 核心结构

```cpp
// disk_buffer_pool.h

/**
 * @brief BufferPool 文件头（存储在文件 Page 0）
 * @details 文件的第一个页面，存储文件的元数据
 */
struct BPFileHeader
{
  int32_t buffer_pool_id;   // BufferPool 唯一标识
  int32_t page_count;       // 文件总页面数
  int32_t allocated_pages;  // 已分配的页面数
  char    bitmap[0];        // 页面分配位图，第0位总是1（Page 0 保留）
};
```

| 字段 | 说明 |
|------|------|
| `buffer_pool_id` | 区分不同的 BufferPool（每个表/索引对应一个） |
| `page_count` | 文件中总页面数 |
| `allocated_pages` | 已分配的页面数 |
| `bitmap` | 标识哪些页面已分配，0=空闲，1=已分配 |

### 2.3 BPFrameManager

管理内存中的所有 Frame（页帧），使用 LRU 缓存策略：

```cpp
class BPFrameManager
{
  using FrameLruCache = common::LruCache<FrameId, Frame *, BPFrameIdHasher>;
  FrameLruCache  frames_;    // LRU 缓存，key=FrameId，value=Frame*
  FrameAllocator allocator_;  // 内存分配器
};
```

### 2.4 BufferPoolManager

管理所有的 DiskBufferPool 实例：

```cpp
class BufferPoolManager
{
  unordered_map<string, DiskBufferPool *>  buffer_pools_;  // 文件名 → BufferPool
  unordered_map<int32_t, DiskBufferPool *> id_to_buffer_pools_;  // ID → BufferPool
  BPFrameManager frame_manager_;  // 全局 Frame 管理器
};
```

---

## 3. Page（页面）

### 3.1 定义

Page 是**磁盘和内存中数据的基本单位**，固定大小 8KB：

```cpp
// page.h
static constexpr const int BP_PAGE_SIZE      = (1 << 13);  // 8KB
static constexpr const int BP_PAGE_DATA_SIZE = (BP_PAGE_SIZE - sizeof(LSN) - sizeof(CheckSum));
// = 8192 - 8 - 4 = 8180 bytes（数据区）

struct Page
{
  LSN      lsn;           // 日志序列号（8 bytes），用于持久化
  CheckSum check_sum;     // 校验和（4 bytes），检测数据损坏
  char     data[BP_PAGE_DATA_SIZE];  // 实际数据区（8180 bytes）
};
```

### 3.2 Page 与 Node 的关系

```
┌─────────────────────────────────────┐
│              Page (8KB)             │
├─────────────────────────────────────┤
│  LSN (8B) │ CheckSum (4B) │ data  │
│            │               │        │
│     12B    │   8180B 数据区         │
└─────────────────────────────────────┘
           │
           ▼
    根据文件类型不同，data 区存放不同结构：
    ┌─────────────────┬─────────────────┐
    │  .table 文件    │  .index 文件    │
    ├─────────────────┼─────────────────┤
    │ PageHeader      │ IndexFileHeader │
    │ + Records       │ (Page 0)        │
    │ (ROW_FORMAT)    │                 │
    │                 │ B+Tree Node      │
    │                 │ (其他 Page)      │
    └─────────────────┴─────────────────┘
```

---

## 4. Frame（页帧）

### 4.1 定义

Frame 是 Page 在**内存中的表示**，除了包含 Page 数据，还管理元信息：

```cpp
// frame.h

class Frame
{
private:
  bool          dirty_ = false;              // 是否脏页（被修改过）
  atomic<int>   pin_count_{0};              // 引用计数，防止被淘汰
  unsigned long acc_time_ = 0;               // 访问时间（LRU 用）
  FrameId       frame_id_;                   // 标识 (buffer_pool_id, page_num)
  Page          page_;                        // 实际的页面数据
  common::RecursiveSharedMutex lock_;         // 读写锁
};
```

### 4.2 Frame 的生命周期

```
1. allocate/load:
   Frame 被创建/加载，pin_count++

2. 使用中:
   pin_count > 0，不能被淘汰

3. unpin:
   使用完毕，pin_count--

4. 可淘汰时:
   pin_count == 0 且 dirty:
     → 写出到磁盘，标记 clean
     → 从内存移除（LRU）

   pin_count == 0 且 clean:
     → 直接从内存移除
```

### 4.3 FrameId（页帧标识符）

```cpp
class FrameId
{
  int     buffer_pool_id_;  // 属于哪个 BufferPool
  PageNum page_num_;        // 页号
};
```

---

## 5. RecordManager（记录管理器）

### 5.1 概述

RecordManager 负责**表中记录的组织和管理**，包括：
- 如何在文件中存放记录
- 如何读取、检索记录
- 记录的增删改查

### 5.2 PageHeader（页面头）

每个数据页面都有一个 PageHeader，记录该页面的元信息：

```cpp
// record_manager.h

struct PageHeader
{
  int32_t record_num;        // 当前页面记录个数
  int32_t column_num;        // 当前页面记录所包含的列数
  int32_t record_real_size;  // 每条记录的实际大小
  int32_t record_size;       // 每条记录占用空间大小（对齐后）
  int32_t record_capacity;   // 最大记录个数
  int32_t col_idx_offset;    // 列索引偏移量（PAX 格式用）
  int32_t data_offset;       // 第一条记录的偏移量
};
```

### 5.3 ROW_FORMAT 页面布局

```
┌─────────────────────────────────────────────┐
│              Page (8KB)                      │
├─────────────────────────────────────────────┤
│  LSN (8B) │ CheckSum (4B) │                │
├─────────────────────────────────────────────┤
│           PageHeader (28B)                   │
├─────────────────────────────────────────────┤
│         Record Bitmap                        │
│  (每条记录1位，0=已删除，1=有效)              │
├─────────────────────────────────────────────┤
│  record0 │ record1 │ record2 │ ... │ recordN │
│  (定长)    (定长)   (定长)                  │
└─────────────────────────────────────────────┘
           ↑
           data_offset 指向这里
```

### 5.4 核心类

| 类 | 作用 |
|---|------|
| `RecordFileHandler` | 管理整个文件的记录增删改查 |
| `RecordPageHandler` | 管理单个页面的记录增删改查 |
| `RowRecordPageHandler` | ROW 格式的页面处理 |
| `PaxRecordPageHandler` | PAX 格式的页面处理 |
| `RecordScanner` | 遍历文件中所有记录 |
| `RecordPageIterator` | 遍历页面中所有记录 |

### 5.5 记录定位

通过 **RID**（Record Identifier）定位：
- `page_num`：记录所在的页面号
- `slot_num`：记录在页面中的槽位号

由于记录是定长的，可以直接计算：`地址 = 页面起始 + data_offset + slot_num * record_size`

---

## 6. RID（记录标识符）

### 6.1 定义

```cpp
// record.h

struct RID
{
  PageNum page_num;  // 记录所在的页号
  SlotNum slot_num;  // 记录在页面中的槽位号
};
```

### 6.2 RID 的作用

```
RID (page_num=2, slot_num=5)

含义：第 2 个页面的第 5 个槽位

定位记录：
  char* record_addr = page_data + data_offset + 5 * record_size
```

---

## 7. Record（记录）

### 7.1 定义

```cpp
// record.h

class Record
{
private:
  RID    rid_;          // 记录标识符 (page_num, slot_num)
  string key_;          // 记录的主键（LSM 引擎用）
  char  *data_;         // 记录数据的内存地址（通常指向页面）
  int    len_;          // 记录数据长度
  bool   owner_;        // 是否自己管理内存
};
```

### 7.2 Record 的内存管理

| owner_ | 说明 |
|--------|------|
| `false` | data_ 指向页面内存，页面释放时 Record 失效 |
| `true` | data_ 是独立分配的，Record 析构时释放内存 |

---

## 8. TableMeta（表元数据）

### 8.1 定义

```cpp
// table_meta.h

class TableMeta
{
  int32_t           table_id_;       // 表 ID
  string            name_;           // 表名
  vector<FieldMeta> trx_fields_;     // 事务相关字段
  vector<FieldMeta> fields_;         // 所有字段（含系统字段）
  vector<IndexMeta> indexes_;        // 所有索引
  vector<string>    primary_keys_;   // 主键字段名列表
  StorageFormat     storage_format_; // 存储格式（ROW/PAX）
  StorageEngine     storage_engine_;// 存储引擎（HEAP/LSM）
  int               record_size_;    // 记录大小
};
```

### 8.2 存储格式

```cpp
enum class StorageFormat
{
  ROW_FORMAT,  // 行存
  PAX_FORMAT,  // PAX 混存
};
```

### 8.3 存储引擎

```cpp
enum class StorageEngine
{
  HEAP,  // 堆表
  LSM,   // LSM-Tree
};
```

---

## 9. B+Tree 索引

### 9.1 IndexFileHeader（索引文件 Page 0）

```cpp
// bplus_tree.h

struct IndexFileHeader
{
  PageNum  root_page;          // 根节点页号
  int32_t  internal_max_size;  // 内部节点最大键值对数
  int32_t  leaf_max_size;      // 叶子节点最大键值对数
  int32_t  attr_length;        // 索引字段长度
  int32_t  key_length;        // attr_length + sizeof(RID)
  AttrType attr_type;         // 索引字段类型
};
```

### 9.2 B+Tree 节点

```cpp
// 通用节点头
struct IndexNode
{
  static constexpr int HEADER_SIZE = 12;
  bool    is_leaf;   // 是否叶子节点
  int     key_num;   // 键值对数量
  PageNum parent;    // 父节点页号
};

// 内部节点
struct InternalIndexNode : public IndexNode
{
  // array: |[ignored]|key0|pageNum0|key1|pageNum1|...
  char array[0];
};

// 叶子节点
struct LeafIndexNode : public IndexNode
{
  static constexpr int HEADER_SIZE = IndexNode::HEADER_SIZE + 4;
  PageNum next_brother;  // 下一个叶子页
  // array: |key0|rid0|key1|rid1|...
  char array[0];
};
```

### 9.3 .index 文件结构

```
┌─────────────────────────────────────────────────────┐
│               .index 文件                           │
├─────────────┬─────────────┬─────────────┬─────────────┤
│   Page 0    │   Page 1    │   Page 2    │   Page 3    │
│ (Header)    │  (Internal) │  (Leaf)     │  (Leaf)     │
│             │             │             │             │
│ root_page=1 │ key|pageNum │ key|rid     │ key|rid     │
│ max_size   │ key|pageNum │ key|rid     │ key|rid     │
│ attr_length │ key|pageNum │ key|rid     │             │
└─────────────┴──────┬──────┴──────▲──────┴─────────────┘
                     │            │
              parent_page_num    next_brother
```

### 9.4 Key 的构成

```
┌─────────────────────────────────┐
│      字段值 (attr_length)       │
│         +                       │
│      RID (8 bytes)              │
│         =                       │
│      key (key_length)           │
└─────────────────────────────────┘
```

**为什么拼接 RID？** 保证唯一性。即使字段值相同，RID 也能区分两条记录。

---

## 10. 各结构之间的关联

### 10.1 数据读取流程

```
SQL: SELECT * FROM t WHERE id = 10;

1. TableMeta
   → 找到 id 字段是主键，有 B+Tree 索引

2. B+Tree 索引查找
   → BufferPoolManager.get_buffer_pool(id)
   → DiskBufferPool 加载 .index 文件
   → Frame 从磁盘读取 Page
   → B+Tree.find_leaf(key=10)
   → 返回 RID = (page=3, slot=5)

3. 数据记录读取
   → BufferPoolManager.get_buffer_pool(table_id)
   → DiskBufferPool 加载 .table 文件
   → Frame 从磁盘读取 Page 3
   → RecordPageHandler.get_record(rid)
   → 返回 Record { data=记录内容, rid=(3,5) }
```

### 10.2 数据插入流程

```
SQL: INSERT INTO t VALUES (10, 'Alice');

1. TableMeta
   → 计算 record_size = 字段1大小 + 字段2大小 + ...

2. 分配 RID
   → RecordFileHandler.allocate_page()
   → BufferPool 分配一个新 Page 或使用已有 Page
   → 返回 RID = (page=5, slot=12)

3. 插入记录到 .table 文件
   → RecordFileHandler.insert_record(data, rid)
   → BufferPool 获取 Page 5 的 Frame
   → RecordPageHandler.insert_record(data, rid)
   → Frame.mark_dirty()

4. 插入索引到 .index 文件
   → BplusTreeIndex.insert_entry(key=(10, rid), rid)
   → BufferPool 获取/分配 Page
   → BplusTreeHandler.insert_entry()
   → Frame.mark_dirty()
```

### 10.3 完整架构图

```
┌────────────────────────────────────────────────────────────────┐
│                         SQL Layer                              │
│                      TableMeta                                 │
│         fields_, indexes_, primary_keys_, record_size_         │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────┐      ┌─────────────────────┐         │
│  │  RecordManager      │      │   BplusTreeIndex    │         │
│  │                     │      │                     │         │
│  │  RecordFileHandler  │      │   BplusTreeHandler  │         │
│  │  RecordPageHandler  │      │   IndexFileHeader   │         │
│  │  PageHeader         │      │   InternalNode      │         │
│  │  RID                │      │   LeafNode          │         │
│  │  Record             │      │   BplusTreeOperation│         │
│  └──────────┬──────────┘      └──────────┬──────────┘         │
│             │                              │                   │
├─────────────┼──────────────────────────────┼───────────────────┤
│             │         BufferPool           │                    │
│  ┌──────────┴──────────┐      ┌───────────┴───────────┐       │
│  │  DiskBufferPool     │      │   DiskBufferPool       │       │
│  │  (for .table)       │      │   (for .index)        │       │
│  └──────────┬──────────┘      └───────────┬───────────┘       │
│             │                              │                   │
├─────────────┼──────────────────────────────┼───────────────────┤
│             │         BufferPoolManager    │                    │
│             │         BPFrameManager       │                    │
│             │         (LRU Cache)         │                    │
├─────────────┼──────────────────────────────┼───────────────────┤
│             │            Frame             │                    │
│             │  dirty_ | pin_count_ | page_  │                    │
├─────────────┼──────────────────────────────┼───────────────────┤
│             │            Page              │                    │
│             │  LSN | CheckSum | data[8180] │                    │
├─────────────┼──────────────────────────────┼───────────────────┤
│             │           Disk                │                    │
│             │  .table 文件  /  .index 文件  │                    │
└─────────────┴──────────────────────────────┴───────────────────┘
```

### 10.4 关键对应关系

| 概念 | 大小 | 说明 |
|------|------|------|
| Page | 8KB | 磁盘与内存传输的基本单位 |
| Frame | ~8KB + 管理结构 | Page 在内存中的表示 |
| RID | 8 bytes | (page_num, slot_num) 定位记录 |
| Record | 变长 | 表中一条记录 |
| key (B+Tree) | attr_length + 8 bytes | 索引键 = 字段值 + RID |
| IndexFileHeader | ~32 bytes | 索引文件元数据 |
| PageHeader | 28 bytes | 数据页元数据 |

---

## 附录：相关源文件

| 源文件 | 内容 |
|--------|------|
| `storage/buffer/disk_buffer_pool.h/cpp` | BufferPool 实现 |
| `storage/buffer/page.h` | Page 结构定义 |
| `storage/buffer/frame.h/cpp` | Frame 实现 |
| `storage/record/record_manager.h/cpp` | 记录管理器 |
| `storage/record/record.h` | Record/RID 定义 |
| `storage/table/table_meta.h/cpp` | 表元数据 |
| `storage/index/bplus_tree.h/cpp` | B+Tree 实现 |

---

## 11. 逻辑计划重写规则

### 11.1 为什么需要重写逻辑计划

SQL 语句描述的是查询结果和语义，不一定直接对应最高效的执行方式。例如：

```sql
SELECT *
FROM t1, t2
WHERE t1.id = t2.id
  AND t1.status = 1;
```

初始逻辑计划可以理解为：

```text
Project
  └── Predicate
        └── Join
              ├── TableGet(t1)
              └── TableGet(t2)
```

如果直接执行，可能先产生 `t1 × t2` 的笛卡尔积，再判断 `t1.id = t2.id` 和 `t1.status = 1`。逻辑计划重写会在不改变查询结果的前提下，把计划变成更适合执行的形式：

```text
Project
  └── Join(t1.id = t2.id)
        ├── Filter(t1.status = 1)
        │     └── TableGet(t1)
        └── TableGet(t2)
```

这样可以：

- 在扫描 `t1` 时提前过滤 `t1.status = 1`；
- 将 `t1.id = t2.id` 识别为连接条件；
- 为 Hash Join、索引连接和列裁剪提供条件；
- 减少中间结果的行数和宽度。

因此，逻辑计划重写的本质是：

> 将等价但低效的逻辑计划，转换为等价且更适合物理执行的逻辑计划。

### 11.2 `Rewriter` 的职责

相关文件：

```text
src/observer/sql/optimizer/rewriter.h
src/observer/sql/optimizer/rewriter.cpp
```

`Rewriter` 本身不是某一条具体优化规则，而是规则调度器：

```cpp
class Rewriter
{
private:
  vector<unique_ptr<RewriteRule>> rewrite_rules_;
};
```

当前注册的规则包括：

```text
ExpressionRewriter
PredicateRewriteRule
PredicatePushdownRewriter
```

可以这样理解：

```text
RewriteRule = 一条等价变换规则
Rewriter    = 多条规则的调度器
OptimizeStage = 重复应用规则直到计划稳定
```

### 11.3 当前的三类重写规则

#### 1. 表达式重写

文件：

```text
src/observer/sql/optimizer/expression_rewriter.cpp
```

当前包括：

```text
ComparisonSimplificationRule
ConjunctionSimplificationRule
```

常量比较可以提前计算：

```sql
WHERE 1 = 1
```

会被重写为：

```text
ValueExpr(true)
```

而：

```sql
WHERE 1 = 0
```

会被重写为：

```text
ValueExpr(false)
```

对于：

```sql
WHERE id > 0 AND 1 = 1
```

可以删除恒真的部分，简化为：

```sql
WHERE id > 0
```

#### 2. 谓词重写

文件：

```text
src/observer/sql/optimizer/predicate_rewrite.cpp
```

如果计划中出现：

```text
Predicate(true)
  └── TableGet
```

就可以删除 `Predicate` 节点，因为它不会过滤任何记录。

如果出现：

```text
Predicate(false)
  └── TableGet
```

则可以直接得到空结果，不需要扫描表。

例如：

```sql
SELECT *
FROM big_table
WHERE 1 = 0;
```

理想情况下不需要读取 `big_table` 的数据记录。

#### 3. 谓词下推

文件：

```text
src/observer/sql/optimizer/predicate_pushdown_rewriter.cpp
```

谓词下推是把过滤条件尽可能放到靠近数据源的位置：

```text
Predicate(id > 10)
  └── TableGet
```

重写后，`id > 10` 会成为 `TableGet` 或扫描算子的过滤条件。物理计划生成器就可以进一步选择：

- Table Scan；
- Index Scan；
- 更少的中间结果。

### 11.4 为什么需要反复重写

`OptimizeStage::rewrite()` 不是只调用一次，而是循环执行：

```cpp
do {
  change_made = false;
  rc = rewriter_.rewrite(logical_operator, change_made);
} while (change_made);
```

这是因为一条规则的结果可能为下一条规则创造新的优化机会。例如：

```text
(1 = 1) AND (id > 0)
  ↓
true AND (id > 0)
  ↓
id > 0
  ↓
Predicate(id > 0) 下推到 TableGet
```

当某一轮没有任何规则再改变计划时，说明计划达到稳定状态，也称为固定点。

### 11.5 重写规则和物理计划生成的区别

逻辑重写决定：

```text
逻辑上如何组织算子和表达式
```

物理计划生成决定：

```text
具体采用什么执行算法
```

例如：

```text
重写后：
Join(t1.id = t2.id)

物理计划生成：
HashJoin
NestedLoopJoin
IndexNestedLoopJoin
```

因此，`Rewriter` 不直接决定使用 Hash Join，但它应该把连接条件整理成便于物理计划生成器识别的形式。

### 11.6 当前实现的限制

当前 `PredicatePushdownRewriter` 主要处理：

```text
Predicate -> TableGet
```

对于：

```text
Predicate
  └── Join
```

还不能完整地把 WHERE 条件拆分到左右表，也不能自动把跨表条件识别为 JOIN 条件。

例如：

```sql
WHERE t1.status = 1
  AND t2.type = 3
  AND t1.id = t2.id
  AND t1.value + t2.value > 100
```

理想的重写结果应是：

```text
t1.status = 1              下推到 t1
t2.type = 3                下推到 t2
t1.id = t2.id              作为 JOIN 条件
t1.value + t2.value > 100  保留到 JOIN 之后
```

要实现这一步，需要先收集每个表达式引用的表集合：

```text
predicate_tables(expr)
```

然后按照引用范围分类：

| 引用的表 | 处理方式 |
|---|---|
| 没有表 | 常量折叠 |
| 只有一张表 | 下推到该表 |
| 当前 JOIN 的左右两侧 | 作为 JOIN 条件候选 |
| 多于当前 JOIN 输入范围 | 保留在上层 |

对于 `AND`，通常可以拆分各个子谓词；对于 `OR`，不能简单地拆成多个独立过滤器。

### 11.7 与列裁剪的关系

逻辑计划重写也是分析字段需求的合适阶段。例如：

```sql
SELECT t1.name
FROM t1 JOIN t2
  ON t1.id = t2.t1_id
WHERE t1.status = 1;
```

可以分析出：

```text
t1.id      JOIN 需要
t1.status  WHERE 需要
t1.name    SELECT 需要
t2.t1_id   JOIN 需要
```

因此扫描和连接阶段不必携带所有列，只需要携带：

```text
JOIN key
WHERE 所需字段
上层输出或计算所需字段
```

更推荐将这个需求表示为 `FieldSet`，而不是执行时简单地对字段使用次数递减：

```text
优化阶段：计算每个算子需要的字段集合
执行阶段：算子只产生 required columns
内存阶段：由 Tuple/Chunk 所有者管理生命周期
```

这样可以避免表达式仍然持有字段引用、Tuple 复用底层缓冲区，以及多级 JOIN 中出现悬空引用。

### 11.8 查询执行的完整位置

逻辑重写位于 SQL 执行流程的中间：

```text
SQL
  ↓
解析
  ↓
字段绑定和类型检查
  ↓
生成初始逻辑计划
  ↓
逻辑计划重写
  ├── 常量折叠
  ├── AND/OR 简化
  ├── 恒真/恒假过滤消除
  ├── WHERE 下推
  ├── JOIN 条件提取
  ├── 列需求分析
  └── 连接顺序调整
  ↓
物理计划生成
  ├── TableScan
  ├── IndexScan
  ├── NestedLoopJoin
  ├── HashJoin
  └── IndexNestedLoopJoin
  ↓
执行
```

一句话总结：

> `rewriter.h` 定义的是逻辑计划优化的入口；它通过组织多条 `RewriteRule`，在不改变结果的情况下，让过滤更早、连接更准确、数据更少，并为后续物理算子选择提供更好的输入。

---

## 12. 表达式功能扩展：聚合函数与普通函数

本次改动的目标是同时支持聚合函数和普通标量函数。重点实现了：

```sql
LENGTH(chars)
ROUND(float)
DATE_FORMAT(date, format)
```

同时保留原有的：

```sql
COUNT(expr)
SUM(expr)
AVG(expr)
MIN(expr)
MAX(expr)
```

### 12.1 为什么需要重构

聚合函数和普通函数在 SQL 语法上完全相同，都是：

```sql
function_name(argument1, argument2, ...)
```

如果在 yacc 阶段直接把所有 `ID(...)` 都构造成 `AggregateExpr`，普通函数就无法进入后续执行流程。因此不能仅靠语法区分，而应该分成两个阶段：

```text
解析阶段：统一生成 UnboundFunctionExpr
绑定阶段：根据函数名区分聚合函数和普通函数
```

最终流程为：

```text
SQL
  ↓
expression / function_call
  ↓
UnboundFunctionExpr
  ↓
ExpressionBinder
  ├── COUNT/SUM/AVG/MIN/MAX
  │     └── UnboundAggregateExpr
  │           └── bind_aggregate_expression()
  └── LENGTH/ROUND/DATE_FORMAT
        └── FunctionExpr
              ├── get_value()       // tuple 行式执行
              ├── get_column()      // Chunk 向量化执行
              └── try_get_value()   // 常量折叠
```

### 12.2 解析阶段：统一函数调用语法

`yacc_sql.y` 将函数调用统一为：

```text
ID LBRACE expression_list RBRACE
```

`expression_list` 支持一个或多个参数，使以下语句都能使用同一套语法：

```sql
COUNT(*)
LENGTH(name)
ROUND(score)
DATE_FORMAT(create_time, '%Y-%M-%D')
```

解析器只记录函数名和未绑定参数，不在此阶段判断它是聚合函数还是普通函数：

```cpp
UnboundFunctionExpr(function_name, args)
```

这样可以避免为聚合函数和普通函数维护两套重复语法。

### 12.3 表达式类型和参数所有权

新增或使用的表达式类型包括：

| 类型 | 作用 |
|------|------|
| `UNBOUND_FUNCTION` | 解析后、绑定前的通用函数调用 |
| `FUNCTION` | 已绑定的普通标量函数 |
| `UNBOUND_AGGREGATION` | 绑定聚合函数时的中间表达式 |
| `AGGREGATION` | 已绑定的聚合表达式 |

表达式参数使用：

```cpp
vector<unique_ptr<Expression>>
```

原因是表达式树拥有子表达式的所有权。`unique_ptr` 保证每个子表达式只有一个所有者，绑定和重写时通过 `std::move()` 转移所有权。

`bind_expression()` 返回 `vector<unique_ptr<Expression>>`，是因为 `StarExpr` 等表达式可能展开成多个字段。普通函数通常只接受一个绑定结果，所以每个参数都要检查：

```cpp
if (one_arg.size() != 1) {
  return RC::INVALID_ARGUMENT;
}
```

### 12.4 绑定阶段：识别聚合函数和普通函数

`ExpressionBinder::bind_function_expression()` 的顺序是：

1. 通过 `AggregateExpr::type_from_string()` 判断是否为聚合函数；
2. 如果是聚合函数，构造 `UnboundAggregateExpr`；
3. 复用已有的 `bind_aggregate_expression()`；
4. 否则递归绑定普通函数的所有参数；
5. 检查函数名、参数数量和参数类型；
6. 构造 `FunctionExpr`。

聚合函数复用已有绑定逻辑很重要，因为原有逻辑已经包含：

- `COUNT(*)` 的特殊处理；
- 聚合子表达式绑定；
- 聚合类型检查；
- 嵌套聚合检查。

普通函数的参数约束为：

| 函数 | 参数 | 返回值 |
|------|------|--------|
| `LENGTH` | 一个 `CHARS` 参数 | `INTS` |
| `ROUND` | 一个 `FLOATS` 参数 | `FLOATS` |
| `DATE_FORMAT` | `DATES`、`CHARS` | `CHARS` |

例如：

```sql
DATE_FORMAT(create_time, '%Y')
```

第二个参数会被绑定为一个 `ValueExpr`，其值是格式字符串；如果传入字段，则会绑定为普通字段表达式，并在每一行执行时读取。

### 12.5 行式执行：`FunctionExpr::get_value()`

tuple 执行路径通过 `FunctionExpr::get_value()` 计算函数结果。三个函数共用一个内部计算入口：

```cpp
evaluate_function(type, args, result)
```

这样可以保证 tuple 执行和向量化执行使用相同的函数语义。

函数行为如下：

```text
LENGTH("abc")                    -> 3
ROUND(3.6)                       -> 4.0
DATE_FORMAT(date, "%Y")          -> "2024"
DATE_FORMAT(date, "%M")          -> "January"
DATE_FORMAT(date, "%D")          -> "1st"
```

`DATE_FORMAT` 当前支持：

```text
%D  带英文序数后缀的日期，例如 1st、2nd、3rd、11th
%M  完整月份名称，例如 January
%Y  四位年份
```

### 12.6 常量折叠：`try_get_value()` 和表达式重写

普通函数需要参与表达式重写，否则函数参数内部的常量表达式无法被递归处理。例如：

```sql
WHERE LENGTH('abc') = 3
```

`FunctionExpr::try_get_value()` 只有在所有参数都能脱离 tuple 求值时才返回成功：

```text
LENGTH('abc')  -> 可以提前计算
LENGTH(name)   -> 依赖当前行，不能提前计算
```

`ComparisonExpr::try_get_value()` 也从只处理两个直接 `ValueExpr`，改为递归调用左右表达式的 `try_get_value()`。因此函数结果可以参与常量比较。

`expression_rewriter.cpp` 增加了 `FUNCTION` 分支，用于递归重写函数参数。重写器负责优化和常量折叠，不负责替代 `FunctionExpr` 的正常执行。

### 12.7 表达式树遍历

`expression_iterator.cpp` 需要识别：

```text
FUNCTION
UNBOUND_FUNCTION
```

并递归遍历它们的参数。否则下列逻辑无法访问函数内部的字段：

```sql
LENGTH(name)
DATE_FORMAT(create_time, '%Y')
```

表达式遍历的作用包括：

- 收集字段引用；
- 计算字段需求；
- 进行表达式重写；
- 检查表达式是否包含聚合；
- 支持投影和过滤阶段的统一处理。

### 12.8 向量化执行：`FunctionExpr::get_column()`

Chunk 执行路径通过 `FunctionExpr::get_column()` 计算整列结果：

```text
1. 对每个参数调用 get_column()
2. 获取参数列
3. 按行读取参数 Value
4. 调用 evaluate_function()
5. 将结果追加到输出 Column
```

常量列每一行都读取下标 `0`，普通列读取当前行号：

```cpp
int arg_row = arg_column.column_type() == Column::Type::CONSTANT_COLUMN ? 0 : row;
```

结果列类型为：

```text
LENGTH       -> 定长 INTS 列
ROUND        -> 定长 FLOATS 列
DATE_FORMAT  -> 变长 CHARS 列
```

### 12.9 `Column` 对变长字符串的支持

原来的 `Column` 主要使用连续的 `data_` 保存定长值。`DATE_FORMAT` 的结果长度不固定，因此增加了 `TextData`：

```cpp
struct TextData
{
  VectorBuffer       buffer;
  vector<string_t>   values;
  vector<uint8_t>    nulls;
};
```

三部分职责如下：

| 成员 | 作用 |
|------|------|
| `VectorBuffer` | 使用 Arena 管理长字符串的实际字符内存 |
| `vector<string_t>` | 保存每一行字符串的描述和值 |
| `vector<uint8_t>` | 保存每一行是否为 NULL |

`string_t` 的短字符串可以内嵌在对象中，长字符串则保存指向 `VectorBuffer` 的指针。因此 `vector<string_t>` 不一定拥有完整字符数据。

文本列的存储关系为：

```text
Column
  └── shared_ptr<TextData>
        ├── VectorBuffer
        ├── vector<string_t>
        └── vector<uint8_t>
```

需要区分两种所有权语义：

```text
Column 拷贝/clone()
  -> 深拷贝 TextData，并将长字符串复制到新的 VectorBuffer

Column::reference()
  -> 共享 shared_ptr<TextData>
```

这样既能保证 `Chunk` 拷贝后数据互不影响，又能保证引用 Chunk 在源 Chunk 生命周期结束后仍能读取字符串。

对于文本列：

- `data_` 为空；
- 应使用 `get_value(index)` 读取；
- 不能使用定长列的 `copy_to()`；
- `append()` 返回 `RC::UNIMPLEMENTED`；
- `data_len()` 不表示所有字符串字符总长度。

### 12.10 日期值的生命周期修复

`Value::get_date()` 不能返回局部变量成员的地址：

```cpp
Value tmp_value;
return &tmp_value.value_.date_value_;  // 错误：返回悬空指针
```

正确做法是：

- 当前值已经是 `DATES` 时，返回 `value_.date_value_`；
- 需要隐式转换时，将转换结果保存到 `Value::date_cache_`；
- 返回当前 `Value` 自己的缓存地址；
- NULL 或无法转换时返回 `nullptr`。

这类问题在开启以下编译选项时会被发现：

```text
-Werror=return-local-addr
```

原则是：函数不能返回局部变量、局部容器或局部对象成员的地址/引用。

### 12.11 构建系统中容易遗漏的地方

如果把多个表达式类合并到已有文件，例如将 `FunctionExpr` 实现写入：

```text
src/observer/sql/expr/expression.cpp
```

就不能在 CMake 中继续添加不存在的：

```text
sql/expr/FunctionExpr.cpp
sql/expr/FunctionExpr.h
```

否则构建时会出现：

```text
No rule to make target '.../FunctionExpr.cpp'
```

规避方法：

1. 添加源文件前确认实际路径和大小写；
2. 删除或合并源文件时同步修改 `CMakeLists.txt`；
3. 头文件通常不需要加入编译源文件列表；
4. 修改 CMake 后重新运行 CMake 配置；
5. 看到 `No rule to make target` 时，先检查路径是否真实存在，而不是先怀疑 C++ 代码。

### 12.12 本次涉及的文件

#### 核心实现文件

| 文件 | 修改内容 |
|------|----------|
| `src/observer/sql/parser/yacc_sql.y` | 统一函数调用语法，生成 `UnboundFunctionExpr` |
| `src/observer/sql/expr/expression.h` | 增加函数表达式类型、`UnboundFunctionExpr` 和 `FunctionExpr` 声明 |
| `src/observer/sql/expr/expression.cpp` | 实现函数计算、常量折叠和向量化执行 |
| `src/observer/sql/expr/expression_binder.h` | 增加普通函数绑定接口 |
| `src/observer/sql/expr/expression_binder.cpp` | 区分聚合函数和普通函数，绑定参数和类型 |
| `src/observer/sql/expr/expression_iterator.cpp` | 遍历普通函数和未绑定函数参数 |
| `src/observer/sql/optimizer/expression_rewriter.cpp` | 递归重写函数参数 |
| `src/observer/storage/common/column.h` | 增加变长字符串列、深拷贝和引用语义 |
| `src/observer/storage/common/column.cpp` | 实现文本列追加、读取、NULL 处理和生命周期管理 |
| `src/observer/common/value.h` | 增加日期转换缓存 |
| `src/observer/common/value.cpp` | 修复 `get_date()` 悬空指针问题 |

#### 关联检查文件

以下文件根据表达式类型或执行链路进行了同步确认或调整：

| 文件 | 作用 |
|------|------|
| `src/observer/sql/optimizer/logical_plan_generator.cpp` | 表达式进入逻辑计划 |
| `src/observer/sql/optimizer/conjunction_simplification_rule.cpp` | 条件表达式简化 |
| `src/observer/sql/optimizer/expression_rewriter.h` | 重写器接口 |
| `src/observer/sql/stmt/select_stmt.cpp` | 查询表达式生成 |
| `src/observer/sql/expr/aggregator.h` | 聚合表达式相关类型 |
| `src/observer/CMakeLists.txt` | 源文件发现和构建配置 |

#### 测试和辅助文件

| 文件 | 作用 |
|------|------|
| `test/case/sql/function.sql` | 函数 SQL 测试用例 |
| `test/case/debug/function.sql` | 函数调试用例 |
| `externTools/run_sql_file.sh` | 执行 SQL 文件的辅助脚本 |

### 12.13 验证清单

本次功能已经通过测试。后续修改类似表达式功能时，可以按下面的顺序验证：

```text
1. 解析：函数调用能否生成正确的未绑定表达式
2. 绑定：函数名、参数数量和参数类型是否正确检查
3. 遍历：表达式迭代器能否访问所有子表达式
4. 重写：常量参数能否参与常量折叠
5. tuple：get_value() 是否正确
6. vector：get_column() 是否正确
7. NULL：NULL 是否正确传播
8. 字符串：短字符串和长字符串是否都能读取
9. 生命周期：clone() 深拷贝、reference() 共享是否正确
10. 构建：CMake 中的源文件路径是否真实存在
```

函数功能扩展的关键原则是：

> 语法统一解析，绑定阶段分类；行式和向量化执行复用同一计算逻辑；定长和变长列使用不同的存储路径；拷贝与引用明确区分所有权。
