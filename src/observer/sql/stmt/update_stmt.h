/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/5/22.
//

#pragma once

#include "common/sys/rc.h"
#include "common/value.h"
#include "sql/stmt/stmt.h"
#include "../observer/storage/field/field.h"

class Table;
class FilterStmt;
class Expression;

/**
 * @brief 更新语句
 * @ingroup Statement
 */
class UpdateStmt : public Stmt
{
public:

  // 新增：支持提前求值的常量值和运行时求值的表达式
  UpdateStmt(Table *table, vector<unique_ptr<Field>> &&fields,
              vector<unique_ptr<UpdatePairNode>> &&update_pair_nodes,
              vector<Value> &&values,
              vector<unique_ptr<Expression>> &&exprs,
              int value_amount, FilterStmt *filter_stmt);

  ~UpdateStmt() override;

  static RC create(Db *db, UpdateSqlNode &update_sql, Stmt *&stmt);
  StmtType type() const override { return StmtType::UPDATE; }

  Table *table() const { return table_; }
  const vector<unique_ptr<UpdatePairNode>> &update_pair_nodes() const { return update_pair_nodes_; }
  int    value_amount() const { return value_amount_; }
  const vector<unique_ptr<Field>> &field() const { return fields_; }
  FilterStmt *filter_stmt() const { return filter_stmt_; }

  // 新增：获取提前求值的常量值（提前求值成功时使用）
  const vector<Value> &values() const { return values_; }
  // 新增：获取需要运行时求值的表达式（为 nullptr 表示已提前求值，直接使用 values_）
  const vector<unique_ptr<Expression>> &exprs() const { return exprs_; }

  // 新增：转移所有权，用于将 values_ 和 exprs_ 转移给 LogicalOperator
  vector<Value> &&take_values() { return std::move(values_); }
  vector<unique_ptr<Expression>> &&take_exprs() { return std::move(exprs_); }
  vector<unique_ptr<Field>> &&take_fields() { return std::move(fields_); }

private:
  Table   *table_             = nullptr;
  // 支持多字段更新
  vector<unique_ptr<Field>>   fields_;
  vector<unique_ptr<UpdatePairNode>>   update_pair_nodes_;
  int     value_amount_ = 0;
  //TODO 过滤字段现在只支持 AND ，后续可以扩展为 OR，NOT 等
  FilterStmt *filter_stmt_    = nullptr;

  // 新增：存储提前求值的常量值（与 fields_ 一一对应，exprs_[i] 为 nullptr 时使用）
  vector<Value> values_;
  // 新增：存储需要运行时求值的表达式（与 fields_ 一一对应，为 nullptr 表示已提前求值）
  vector<unique_ptr<Expression>> exprs_;
};
