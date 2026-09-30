//
// Created by tangmingtao on 2026/9/28.
//

#pragma once

#include "../operator/operator_node.h"
#include "../operator/physical_operator.h"
#include "storage/field/field.h"

class Tuple;
class Trx;
class UpdateStmt;
class Table;
class Field;
class Value;
class FilterStmt;

/**
 * @brief 物理算子，更新
 * @ingroup PhysicalOperator
 */
class UpdatePhysicalOperator : public PhysicalOperator
{
public:
  UpdatePhysicalOperator(Table *table, unique_ptr<Field> field, const Value *value, FilterStmt *filter_stmt);
  virtual ~UpdatePhysicalOperator();

  PhysicalOperatorType type() const override { return PhysicalOperatorType::UPDATE; }

  OpType get_op_type() const override { return OpType::UPDATE; }


  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override { return nullptr; }

private:
  Table *table_ = nullptr;
  unique_ptr<Field> field_ = nullptr;
  const Value *value_ = nullptr;
  FilterStmt *filter_stmt_ = nullptr;
  Trx *trx_ = nullptr;
  vector<Record> records_;

  RC create_new_record(const Record &old_record, Record &new_record);
  RC set_value_to_record(char *record_data, const Value &vale, const FieldMeta *field);
};
