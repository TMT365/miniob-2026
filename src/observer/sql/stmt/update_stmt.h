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
#include "sql/stmt/stmt.h"
#include "../observer/storage/field/field.h"

class Table;
class FilterStmt;

/**
 * @brief 更新语句
 * @ingroup Statement
 */
class UpdateStmt : public Stmt
{
public:
  UpdateStmt() = default;
  UpdateStmt(Table *table,unique_ptr<Field> field,const Value *values, int value_amount, FilterStmt *filter_stmt);
  ~UpdateStmt() override;

  static RC create(Db *db, const UpdateSqlNode &update_sql, Stmt *&stmt);
  StmtType type() const override { return StmtType::UPDATE; }

  Table *table() const { return table_; }
  const Value *values() const { return values_; }
  int    value_amount() const { return value_amount_; }
  unique_ptr<Field>  field()  { return std::move(field_) ; }
  FilterStmt *filter_stmt() const { return filter_stmt_; }

private:
  Table   *table_             = nullptr;
  //TODO 现在这里先只实现一个字段的更新，后续可以扩展为多个字段的更新
  unique_ptr<Field>   field_  = nullptr;
  const Value   *values_            = nullptr;
  int     value_amount_ = 0;
  //TODO 过滤字段现在只支持 AND ，后续可以扩展为 OR，NOT 等
  FilterStmt *filter_stmt_    = nullptr;
};
