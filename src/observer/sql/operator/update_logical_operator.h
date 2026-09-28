//
// Created by ZhuanZ（无密码） on 2026/9/28.
//

#pragma once
#include "./operator_node.h"
#include "./logical_operator.h"
#include "common/lang/string.h"
#include "../stmt/filter_stmt.h"
#include "../../common/value.h"

class Table;

/**
 * @brief 更新逻辑算子
 * @ingroup LogicalOperator
 */
class UpdateLogicalOperator : public LogicalOperator
{
public:
  UpdateLogicalOperator(Table *table, unique_ptr<Field> field, const Value *value, FilterStmt *filter_stmt);
  virtual ~UpdateLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::UPDATE; }
  OpType get_op_type() const override { return OpType::UPDATE; }

  // getter
  Table             *table() const          { return table_; }
  unique_ptr<Field>  field()           { return std::move(field_); }
  const Value       *value() const          { return value_; }
  FilterStmt  *filter_stmt() const    { return filter_stmt_; }

private:
  Table             *table_         = nullptr;
  unique_ptr<Field> field_;         // 拥有对象
  const Value       *value_         = nullptr;
  FilterStmt        *filter_stmt_   = nullptr;
};