
//
// Created by tangmingtao  on 2022/6/27.
//

#include "sql/operator/update_physical_operator.h"
#include "sql/operator/predicate_physical_operator.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"
#include "sql/expr/tuple.h"

UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, unique_ptr<Field> field, const Value *value, FilterStmt *filter_stmt)
    : table_(table), field_(std::move(field)), value_(value), filter_stmt_(filter_stmt)
{}

UpdatePhysicalOperator::~UpdatePhysicalOperator()
{

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
    LOG_INFO("收集符号条件的记录");
    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record   &record    = row_tuple->record();
    records_.emplace_back(record);
  }

  child->close();  // 先关闭子节点，释放所有锁

  Field *field = field_.get();
  // 第二阶段：执行更新操作
  for (Record &record : records_) {
    Record new_record;
    // 修复了原来 std::move(field_) 导致 field_ 被移动后无法再使用的问题
    rc = trx_->update_record(table_, record, new_record, field, value_);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to update record: %s", strrc(rc));
      return rc;
    }

  }
  // 循环结束后要析构对象防止内存泄露

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
