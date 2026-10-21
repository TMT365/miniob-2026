
//
// Created by tangmingtao  on 2022/6/27.
//

#include "sql/operator/update_physical_operator.h"
#include "sql/operator/predicate_physical_operator.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"
#include "sql/expr/tuple.h"
#include "sql/expr/expression.h"


// 新增构造函数
UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, vector<unique_ptr<Field>> &&fields,
                                               vector<Value> &&values,
                                               vector<unique_ptr<Expression>> &&exprs,
                                               int value_mount, FilterStmt *filter_stmt)
    : table_(table), fields_(std::move(fields)), values_(std::move(values)), exprs_(std::move(exprs)),
      value_mount_(value_mount), filter_stmt_(filter_stmt)
{}

UpdatePhysicalOperator::~UpdatePhysicalOperator()
{
  // values_ 是 vector<Value>，不需要手动释放
  // exprs_ 是 vector<unique_ptr<Expression>>，析构时自动释放
}

RC UpdatePhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  unique_ptr<PhysicalOperator> &child = children_[0];

  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  trx_ = trx;

  // 第一阶段：收集所有匹配的记录
  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get current record: %s", strrc(rc));
      return rc;
    }
    LOG_INFO("收集符合条件的记录");
    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record   &record    = row_tuple->record();
    records_.emplace_back(record);
  }

  child->close();  // 先关闭子节点，释放所有锁

  // 第二阶段：执行更新操作
  for (Record &record : records_) {
    // 1. 收集所有字段和对应的值
    vector<Field *> fields_ptr;
    vector<Value> values;
    fields_ptr.reserve(value_mount_);
    values.reserve(value_mount_);

    for (int i = 0; i < value_mount_; i++) {
      fields_ptr.push_back(fields_[i].get());

      Value value;

      // 检查是使用提前求值的常量，还是需要运行时求值
      if (i < static_cast<int>(exprs_.size()) && exprs_[i] != nullptr) {
        // 需要运行时通过 Expression 求值（可能引用了表中其他字段）
        Expression *expr = exprs_[i].get();

        // 从当前记录构建一个 RowTuple 来求值
        // 注意：需要先设置 schema，让 FieldExpr 能够找到字段
        RowTuple tmp_tuple;
        tmp_tuple.set_schema(table_, table_->table_meta().field_metas());
        tmp_tuple.set_record(&record);

        rc = expr->get_value(tmp_tuple, value);
        if (rc != RC::SUCCESS) {
          LOG_WARN("failed to evaluate expression: %s", strrc(rc));
          return rc;
        }
      } else if (i < static_cast<int>(values_.size())) {
        // 提前求值的常量，直接使用
        value = values_[i];
      } else {
        // 错误：两者都没有
        LOG_WARN("both values_[%d] and exprs_[%d] are not available", i, i);
        return RC::INTERNAL;
      }

      values.push_back(std::move(value));
    }

    // 2. 调用 make_record 一次性创建新记录（复制旧记录并设置多个字段的值）
    Record new_record;
    rc = table_->make_record(record, new_record, fields_ptr, values, value_mount_);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to make new record: %s", strrc(rc));
      return rc;
    }

    // 3. 调用 trx 更新（只调用一次，用完整的新记录替换旧记录）
    rc = trx_->update_record(table_, record, new_record);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to update record: %s", strrc(rc));
      return rc;
    }
  }

  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::next()
{
  return RC::RECORD_EOF;
}

RC UpdatePhysicalOperator::close()
{
  return RC::SUCCESS;
}
