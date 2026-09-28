//
// Created by ZhuanZ（无密码） on 2026/9/28.
//
#include "update_logical_operator.h"

UpdateLogicalOperator::UpdateLogicalOperator(
  Table *table,
  unique_ptr<Field> field,
  const Value *value, FilterStmt *filter_stmt
  ): table_(table), field_(std::move(field)), value_(value), filter_stmt_(filter_stmt) {}