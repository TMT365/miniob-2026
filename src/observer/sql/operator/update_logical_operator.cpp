//
// Created by ZhuanZ（无密码） on 2026/9/28.
//
#include "update_logical_operator.h"


// 新增构造函数
UpdateLogicalOperator::UpdateLogicalOperator(
  Table *table,
  vector<unique_ptr<Field>> &&fields,
  vector<Value> &&values,
  vector<unique_ptr<Expression>> &&exprs,
  int value_mount,
  FilterStmt *filter_stmt
  ): table_(table), fields_(std::move(fields)),value_mount_(value_mount), filter_stmt_(filter_stmt),
    values_(std::move(values)),exprs_(std::move(exprs))
{}
