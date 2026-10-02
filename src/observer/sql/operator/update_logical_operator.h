//
// Created by ZhuanZ（无密码） on 2026/9/28.
//

#pragma once
#include "./operator_node.h"
#include "./logical_operator.h"
#include "common/lang/string.h"
#include "common/lang/vector.h"
#include "common/value.h"
#include "sql/stmt/filter_stmt.h"

class Table;
class Field;
class Expression;

/**
 * @brief 更新逻辑算子
 * @ingroup LogicalOperator
 */
class UpdateLogicalOperator : public LogicalOperator
{
public:

  // 新构造函数，支持提前求值的常量和运行时求值的表达式
  UpdateLogicalOperator(Table *table, vector<unique_ptr<Field>> &&fields, vector<Value> &&values,
      vector<unique_ptr<Expression>> &&exprs, int value_mount, FilterStmt *filter_stmt);

  virtual ~UpdateLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::UPDATE; }
  OpType get_op_type() const override { return OpType::UPDATE; }

  // getter
  Table             *table() const          { return table_; }
  const vector<unique_ptr<Field>> &field() const  { return fields_; }
  FilterStmt        *filter_stmt() const    { return filter_stmt_; }
  int                value_mount() const    { return value_mount_; }

  // 新增 getter
  const vector<Value> &values() const { return values_; }
  const vector<unique_ptr<Expression>> &exprs() const { return exprs_; }

  // 新增：转移所有权
  vector<Value> &&take_values()  { return std::move(values_); }
  vector<unique_ptr<Expression>> &&take_exprs() { return std::move(exprs_); }
  vector<unique_ptr<Field>> &&take_fields()   { return std::move(fields_); }

private:
  Table             *table_         = nullptr;
  vector<unique_ptr<Field>> fields_;         // 拥有对象
  int value_mount_ = 0;
  FilterStmt        *filter_stmt_   = nullptr;

  // 新增：存储提前求值的常量值
  vector<Value> values_;
  // 新增：存储需要运行时求值的表达式（为 nullptr 表示已提前求值）
  vector<unique_ptr<Expression>> exprs_;
};
