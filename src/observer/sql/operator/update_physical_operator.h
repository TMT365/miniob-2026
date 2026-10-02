//
// Created by tangmingtao on 2026/9/28.
//

#pragma once

#include "../operator/operator_node.h"
#include "../operator/physical_operator.h"
#include "storage/field/field.h"
#include "common/lang/vector.h"
#include "common/value.h"

class Tuple;
class Trx;
class UpdateStmt;
class Table;
class Field;
class Value;
class FilterStmt;
class Expression;

/**
 * @brief 物理算子，更新
 * @ingroup PhysicalOperator
 */
class UpdatePhysicalOperator : public PhysicalOperator
{
public:
  // 新构造函数，支持提前求值的常量和运行时求值的表达式
  UpdatePhysicalOperator(Table *table, vector<unique_ptr<Field>> &&fields,
                         vector<Value> &&values,
                         vector<unique_ptr<Expression>> &&exprs,
                         int value_mount, FilterStmt *filter_stmt);

  virtual ~UpdatePhysicalOperator();

  PhysicalOperatorType type() const override { return PhysicalOperatorType::UPDATE; }

  OpType get_op_type() const override { return OpType::UPDATE; }


  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override { return nullptr; }

private:
  Table *table_ = nullptr;
  const vector<unique_ptr<Field>> fields_;  ///< 一个记录同时要更新的字段集合

  // 新增：存储提前求值的常量值（与 fields_ 一一对应，exprs_[i] 为 nullptr 时使用）
  const vector<Value> values_;

  // 新增：存储需要运行时求值的表达式（与 fields_ 一一对应，为 nullptr 表示已提前求值）
  const vector<unique_ptr<Expression>> exprs_;

  int value_mount_ = 0;
  FilterStmt *filter_stmt_ = nullptr;
  Trx *trx_ = nullptr;
  vector<Record> records_;

  RC create_new_record(const Record &old_record, Record &new_record);
  RC set_value_to_record(char *record_data, const Value &vale, const FieldMeta *field);
};
