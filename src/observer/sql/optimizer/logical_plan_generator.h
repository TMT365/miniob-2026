/* Copyright (c) 2023 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2023/08/16.
//

#pragma once

#include "common/lang/memory.h"
#include "common/sys/rc.h"
#include "common/type/attr_type.h"
#include "../parser/parse_defs.h"
#include "sql/stmt/select_stmt.h"

class Stmt;
class CalcStmt;
class SelectStmt;
class FilterStmt;
class InsertStmt;
class DeleteStmt;
class UpdateStmt;
class ExplainStmt;
class LogicalOperator;

class LogicalPlanGenerator
{
public:
  LogicalPlanGenerator()          = default;
  virtual ~LogicalPlanGenerator() = default;

  RC create(Stmt *stmt, unique_ptr<LogicalOperator> &logical_operator);

private:
  RC create_plan(CalcStmt *calc_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_plan(FilterStmt *filter_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_plan(InsertStmt *insert_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_plan(DeleteStmt *delete_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_plan(UpdateStmt *update_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_plan(ExplainStmt *explain_stmt, unique_ptr<LogicalOperator> &logical_operator);

  RC create_group_by_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator);
  RC create_join_on_pred_oper(
      SelectStmt *select_stmt, JoinSqlNode *join, unique_ptr<LogicalOperator> &join_on_pred_oper);

  int implicit_cast_cost(AttrType from, AttrType to);
  RC  align_expression_types(unique_ptr<Expression> &left, unique_ptr<Expression> &right);
};
