# JOIN 与 WHERE 优化方案讨论

本文针对当前 MiniOB 的 JOIN、WHERE 和 Tuple/字段传递方式，讨论多表查询性能优化方向。重点是避免无谓的笛卡尔积、尽早过滤数据，并减少连接过程中携带的列。

## 1. 当前实现的问题

### 1.1 多表 JOIN 默认使用 Nested Loop Join

当前物理计划生成位于：

```text
src/observer/sql/optimizer/physical_plan_generator.cpp
```

`create_plan(JoinLogicalOperator &, ...)` 当前总是创建 `NestedLoopJoinPhysicalOperator`。Hash Join 的判断函数 `can_use_hash_join` 仍然返回 `false`，Hash Join 分支也只是 TODO。

当前执行方式大致为：

```text
左子算子取一行
  -> 右子算子重新 open
  -> 右子算子扫描全部记录
  -> 生成左行和右行的组合
  -> PredicatePhysicalOperator 判断 ON 条件
```

如果左表有 `M` 行、右表有 `N` 行，这种方式最坏需要接近 `M * N` 次组合判断；而且右表会被反复扫描。

相关实现：

```text
src/observer/sql/operator/nested_loop_join_physical_operator.cpp
src/observer/sql/operator/join_physical_operator.cpp
```

### 1.2 ON 条件在笛卡尔积之后才判断

当前逻辑计划中，JOIN 的两个子树先生成，`ON` 条件挂到 JOIN 节点上。物理计划中先创建 JOIN，再把 ON 条件包装成 Predicate 算子：

```text
NestedLoopJoin
  -> Predicate(ON)
```

这意味着对于：

```sql
SELECT ...
FROM t1 JOIN t2 ON t1.id = t2.id;
```

系统先产生大量 `t1 × t2` 的组合，再过滤掉绝大多数不匹配行。正确的优化目标应该是把 `t1.id = t2.id` 识别为连接键，在 JOIN 算子内部直接匹配。

### 1.3 WHERE 下推能力有限

当前下推逻辑位于：

```text
src/observer/sql/optimizer/predicate_pushdown_rewriter.cpp
```

它主要处理 `Predicate -> TableGet` 的情况。多表查询的 WHERE 通常位于整个 JOIN 树上，尚未系统地拆分为：

1. 只引用单张表的谓词；
2. 只引用某个已经形成的子连接的谓词；
3. 必须等多个表连接后才能判断的谓词。

例如：

```sql
WHERE t1.status = 1
  AND t2.type = 3
  AND t1.id = t2.id
  AND t1.value + t2.value > 100
```

理想处理方式是：

```text
t1.status = 1       下推到 t1
t2.type = 3         下推到 t2
t1.id = t2.id       作为 JOIN key
t1.value+t2.value>100 连接后判断
```

当前实现还没有完整的谓词引用表集合分析机制。

### 1.4 当前 Tuple 是组合引用，不是紧凑的投影行

`CompositeTuple` 会保存多个子 Tuple：

```text
src/observer/sql/expr/composite_tuple.h
src/observer/sql/expr/composite_tuple.cpp
```

连接结果中的字段访问通过遍历子 Tuple 完成。这样实现简单，但多表连接后：

- 中间结果会携带左右输入的全部字段；
- 字段查找可能反复遍历多个 Tuple；
- 多级 JOIN 会形成嵌套的 CompositeTuple；
- 连接条件只需要两三个字段，但中间行仍然保留整行；
- 直接释放某个输入 Tuple 可能导致 CompositeTuple 或表达式访问悬空引用。

## 2. 对“字段使用次数为 0 就释放”的评价

这个方向的目标是正确的，但不建议直接对单个字段使用简单的运行时引用计数并立即释放内存。

### 2.1 可以保留的核心思想

可以为每个字段建立需求信息，例如：

```text
字段标识：table_id + field_id
需要原因：
  - SELECT 输出
  - WHERE 条件
  - JOIN 条件
  - GROUP BY
  - ORDER BY
  - 聚合表达式
引用阶段：
  - 当前算子
  - 后续算子
```

优化器根据这些信息生成每个算子所需的最小列集合。

例如：

```sql
SELECT t1.name, t2.amount
FROM t1
JOIN t2 ON t1.id = t2.t1_id
WHERE t1.status = 1;
```

连接阶段至少需要：

```text
t1: id, status, name
t2: t1_id, amount
```

其中：

```text
t1.id       JOIN key
t1.status   WHERE
t1.name     SELECT
t2.t1_id    JOIN key
t2.amount   SELECT
```

`t1` 的其他列不应从扫描层读出或传入 JOIN。

### 2.2 不建议把“引用次数归零”直接等同于释放

简单引用计数存在几个风险：

1. **表达式仍可能持有字段引用**
   
   `FieldExpr`、比较表达式、投影表达式和聚合表达式可能在不同计划节点中被复用。字段是否还会被访问，不能只根据当前算子的调用次数判断。

2. **Tuple 可能只是引用底层扫描缓冲区**
   
   当前 `TableScanPhysicalOperator::current_tuple()` 返回内部 `tuple_`，而 `tuple_` 又引用当前记录。下一次 `next()` 后，底层内容可能被覆盖。因此中间算子如果需要跨调用保存字段，必须复制值或拥有稳定的批数据。

3. **Nested Loop 的右侧行生命周期跨越多个调用**
   
   JOIN 输出的 `CompositeTuple` 同时引用左、右当前 Tuple。右子算子下一次读取后，之前的右侧内容可能已经变化。立即释放或复用缓冲区会导致错误结果。

4. **同一个字段可能被多个算子使用**
   
   例如一个字段同时参与 JOIN、WHERE、GROUP BY 和 SELECT。手工维护调用次数很容易出现少减、多减或计划改写后计数失效。

5. **优化器生命周期与执行期生命周期不同**
   
   字段需求是计划级信息，内存释放是执行级行为。两者应通过明确的 `ColumnSet`/`Projection` 接口连接，而不是让表达式直接管理字段内存。

因此更推荐：

```text
优化阶段：计算 required columns
执行阶段：算子只输出 required columns
内存阶段：由 Chunk/Tuple 所有者统一管理生命周期
```

引用计数可以作为统计信息或缓冲区复用依据，但不应作为字段对象的直接析构条件。

## 3. 推荐的总体架构

建议分成四个层次。

### 3.1 字段需求分析

新增一个表达式字段收集工具，例如：

```text
src/observer/sql/optimizer/expression_field_collector.h
src/observer/sql/optimizer/expression_field_collector.cpp
```

提供类似接口：

```cpp
using FieldKey = pair<Table *, int>; // 或稳定的 table_id + field_id
using FieldSet = unordered_set<FieldKey>;

RC collect_fields(const Expression &expr, FieldSet &fields);
RC collect_fields(const LogicalOperator &oper, FieldSet &fields);
```

需要递归处理：

- `FieldExpr`
- `ComparisonExpr`
- `ConjunctionExpr`
- `ArithmeticExpr`
- `AggregateExpr`
- `CastExpr`

对于每个逻辑算子，需求列应取：

```text
本算子表达式引用的列
∪ 所有父算子继续需要的列
```

更准确地说，可以从根节点向下传播需求：

```text
Project/输出需要的列
  -> Group By 需要的列
  -> Where 需要的列
  -> Join 条件需要的列
  -> TableScan 最终读取的列
```

### 3.2 TableGet/TableScan 列裁剪

给 `TableGetLogicalOperator` 和 `TableScanPhysicalOperator` 增加 required columns：

```cpp
void set_required_fields(FieldSet fields);
const FieldSet &required_fields() const;
```

扫描算子只构造、读取和输出需要的列，而不是默认读取整张表。

行式执行可以先实现一个轻量版本：

```text
TableScan 仍然读取 Record
但 Tuple 只暴露 required fields
```

更进一步可以让 RecordScanner/ChunkScanner 也只解码 required columns，特别是 PAX/向量化路径下收益更明显。

### 3.3 中间 Projection 算子

在 JOIN 前增加显式的列裁剪算子：

```text
TableScan
  -> Projection(required columns)
```

或直接让 TableScan 具备投影能力。

连接前的输入只保留：

```text
JOIN key
WHERE 所需字段
上层继续需要的字段
```

不要在 JOIN 算子中使用“删除字段”的副作用操作。JOIN 输出应是一个明确拥有列布局的 Tuple/Chunk。

### 3.4 稳定的列布局

当前 `CompositeTuple` 依赖嵌套 Tuple 查找。建议逐步引入带列映射的中间行：

```cpp
struct TupleLayout {
  vector<FieldKey> fields;
  unordered_map<FieldKey, int> positions;
};
```

中间行可以保存：

```cpp
struct JoinedRow {
  TupleLayout layout;
  vector<Value> values;
};
```

表达式通过 `FieldKey -> position` 访问，而不是每次递归遍历 CompositeTuple。

第一阶段不必马上删除 `CompositeTuple`，可以先为它缓存字段位置；第二阶段再引入扁平化的 `JoinedTuple`。

## 4. JOIN 优化的实现顺序

### 4.1 第一步：把 JOIN 条件直接交给 JOIN 算子

不要生成：

```text
NestedLoopJoin -> Predicate(ON)
```

而应让 JOIN 算子持有连接谓词：

```cpp
join_physical_oper->set_join_predicate(...);
```

Nested Loop 中每生成一对左右 Tuple，立即判断 ON 条件。这样仍然是 `O(M*N)`，但避免了额外 Predicate 层和不必要的中间结果传递，为后续索引连接、Hash Join 留出接口。

### 4.2 第二步：识别等值连接

识别形如：

```text
t1.field = t2.field
```

的谓词，要求：

- 左右均为 `FieldExpr`；
- 两个字段属于不同输入子树；
- 比较操作为 `EQUAL_TO`；
- 两侧类型可以比较或已完成类型转换。

可以抽象为：

```cpp
struct JoinKeyPair {
  unique_ptr<Expression> left_key;
  unique_ptr<Expression> right_key;
};
```

暂时只支持单个等值键即可，后续扩展为复合键。

### 4.3 第三步：实现 Hash Join

Hash Join 的基本流程：

```text
1. 选择较小的一侧作为 build side
2. 读取 build side 所需列
3. 根据 join key 建 hash table
4. 扫描 probe side
5. 查找相同 key
6. 输出匹配的 JoinedRow
```

对于：

```sql
t1 JOIN t2 ON t1.id = t2.id
```

复杂度从接近：

```text
O(M*N)
```

降低为平均：

```text
O(M+N)
```

需要注意：

- Hash key 必须处理 NULL。普通 SQL 等值 JOIN 中 NULL 不与任何值相等；
- 重复 key 要保存多个 build row；
- 输出行不能引用下一次扫描就会被覆盖的临时 Tuple；
- 必须定义 Hash Join 的内存上限和退化策略；
- 暂不支持等值以外的 ON 条件时，Hash Join 输出后仍需执行 residual predicate。

建议物理算子接口最终类似：

```cpp
class HashJoinPhysicalOperator : public JoinPhysicalOperator {
public:
  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

private:
  vector<JoinKeyPair> keys_;
  unique_ptr<Expression> residual_predicate_;
};
```

当前的 `hash_join_physical_operator.h` 只是空类，不能直接在此基础上假设已有功能。

### 4.4 第四步：连接顺序优化

多表查询不能固定按 SQL 书写顺序连接。应根据以下信息选择顺序：

1. 单表 WHERE 过滤后的估计行数；
2. 是否有索引；
3. 等值连接是否可使用 Hash Join；
4. 中间结果的估计行数；
5. 需要保留的字段数量。

例如：

```sql
a JOIN b ON ...
  JOIN c ON ...
  JOIN d ON ...
WHERE a.status = 1
  AND d.type = 2;
```

不应默认按 `(((a JOIN b) JOIN c) JOIN d)` 执行。更合理的策略可能是：

```text
先过滤 a 和 d
选择选择性更高、结果更小的连接
再连接剩余表
```

初期不需要实现完整动态规划，可以先做贪心：

```text
每次选择估计中间结果最小、且存在连接谓词的下一个表
```

表数量较少时，再实现 Selinger 风格的动态规划；要设置表数量上限，避免状态空间爆炸。

## 5. WHERE 优化方案

### 5.1 按引用表集合拆分谓词

为每个谓词计算引用的表集合：

```text
predicate_tables(expr)
```

分类规则：

| 引用表集合 | 处理方式 |
|---|---|
| 空集合 | 常量折叠 |
| 只有一张表 | 下推到该表扫描 |
| 恰好是当前 JOIN 的左右两侧 | 作为 JOIN 条件 |
| 多于当前 JOIN 输入范围 | 保留到更高层 |

对于 AND，可以安全拆分：

```text
p1 AND p2 AND p3
```

对于 OR，不能简单拆成多个独立过滤器。可以先保留整体谓词，或实现基于析取的安全下推。

### 5.2 单表谓词下推到 TableGet

当前 `PredicatePushdownRewriter` 需要增加“引用表集合”判断，不能只看到 `COMPARISON` 就直接下推。

特别要避免把跨表表达式错误下推到单表扫描：

```sql
WHERE t1.id = t2.id
```

这个谓词不能下推到 `t1` 或 `t2` 任一侧，因为另一侧字段还不存在。

### 5.3 索引条件下推

现有 `PhysicalPlanGenerator::create_plan(TableGetLogicalOperator &, ...)` 已经对简单等值条件尝试选择索引。下一步可以支持：

- `=`
- `<`, `<=`, `>`, `>=`
- 单列范围合并
- `AND` 中多个条件选择最有利索引
- JOIN 中的 Index Nested Loop Join

Index Nested Loop Join 的流程是：

```text
外表取一行
  -> 用外表 join key 查内表索引
  -> 只读取匹配的内表记录
```

当外表较小、内表有高选择性索引时，它可能优于 Hash Join。

## 6. 你提出的“使用次数”设计如何落地

如果仍希望保留“使用次数”的概念，建议把它定义成逻辑需求计数，而不是直接内存释放计数。

### 6.1 推荐的数据结构

```cpp
struct FieldRequirement {
  FieldKey key;
  uint32_t output_refs = 0;
  uint32_t predicate_refs = 0;
  uint32_t join_key_refs = 0;
  uint32_t group_refs = 0;
  uint32_t order_refs = 0;
  bool nullable = false;
};
```

总需求可以计算为：

```text
required =
  output_refs > 0 ||
  predicate_refs > 0 ||
  join_key_refs > 0 ||
  group_refs > 0 ||
  order_refs > 0
```

但不建议每次执行后递减这些数字。它们描述的是“计划需要什么”，不是“当前调用栈还有多少次访问”。

### 6.2 真正的释放边界

推荐以算子边界释放：

```text
TableScan 的 Chunk 被下游消费完
  -> Chunk 生命周期结束

HashJoin build side 完成构建
  -> 只保留 build key 和上层所需字段

某个中间 Join 输出被下游消费完
  -> 释放该批 JoinedRow
```

如果需要批内复用，可以使用：

```text
Chunk arena
Column buffer pool
对象池
引用计数的 immutable batch
```

而不是在单个 `Value` 上手动判断“最后一次使用”。

## 7. 推荐的分阶段实施计划

### 阶段一：低风险，立即收益

1. 让 JOIN 算子直接持有并判断 ON 谓词；
2. 修正 Predicate Pushdown，使其只下推真正只引用单表的条件；
3. 为表达式增加字段引用收集函数；
4. 在 TableGet/TableScan 中支持 required fields；
5. 为 CompositeTuple 缓存字段位置，减少重复查找。

### 阶段二：核心性能提升

1. 实现等值 Hash Join；
2. 支持 residual predicate；
3. 支持 Hash Join 的重复 key、NULL 和多列 key；
4. 根据估计行数选择 Nested Loop、Hash Join 或 Index Nested Loop。

### 阶段三：多表计划优化

1. 提取所有单表谓词和连接谓词；
2. 生成连接图；
3. 用贪心算法选择连接顺序；
4. 为小规模多表查询增加动态规划；
5. 加入基于统计信息的代价估算。

### 阶段四：向量化和内存优化

1. Chunk 级列裁剪；
2. Hash Table 只保存 join key 和必要 payload；
3. JoinedRow 扁平化；
4. build/probe 两侧批量处理；
5. 内存达到阈值时采用分区 Hash Join 或退化到 Nested Loop。

## 8. 建议优先修改的文件

第一阶段可重点修改：

```text
src/observer/sql/optimizer/predicate_pushdown_rewriter.cpp
src/observer/sql/optimizer/physical_plan_generator.cpp
src/observer/sql/operator/nested_loop_join_physical_operator.h
src/observer/sql/operator/nested_loop_join_physical_operator.cpp
src/observer/sql/expr/composite_tuple.h
src/observer/sql/expr/composite_tuple.cpp
src/observer/sql/expr/expression.h
src/observer/sql/expr/expression.cpp
src/observer/sql/operator/table_get_logical_operator.h
src/observer/sql/operator/table_get_logical_operator.cpp
src/observer/sql/operator/table_scan_physical_operator.h
src/observer/sql/operator/table_scan_physical_operator.cpp
```

第二阶段重点修改：

```text
src/observer/sql/operator/hash_join_physical_operator.h
src/observer/sql/operator/hash_join_physical_operator.cpp
src/observer/sql/optimizer/physical_plan_generator.cpp
src/observer/sql/operator/join_logical_operator.h
```

## 9. 总结意见

你的“只保留连接需要的字段，并根据后续使用情况减少中间数据”的判断是正确的，也是列裁剪和谓词下推的核心。

但建议把设计从：

```text
字段被使用 N 次，减到 0 就释放
```

调整为：

```text
优化器先计算每个算子需要的 FieldSet；
每个算子只产生 required columns；
由 Tuple/Chunk 所有者管理批数据生命周期；
连接算子只保留 key 和后续需要的 payload。
```

性能收益最大的优先级不是先实现字段引用计数，而是：

```text
单表 WHERE 下推
  -> ON 条件进入 JOIN 内部
  -> Hash Join / Index Nested Loop Join
  -> 列裁剪
  -> 多表连接顺序优化
```

这样可以先避免无谓的笛卡尔积，再减少每条中间记录的宽度，最后减少中间结果的数量和生命周期。
