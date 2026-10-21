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

#include "common/lang/unordered_map.h"
#include "common/lang/vector.h"
#include "sql/expr/expression.h"
#include "sql/parser/parse_defs.h"
#include "sql/stmt/stmt.h"

class Db;
class Table;
class FieldMeta;

struct FilterObj
{
  bool  is_attr;
  bool                   is_expr;  // true: 使用 expression（无法归并为value的复杂表达式）
  Field                  field;    // 当 is_attr == true 时使用
  Value                  value;    // 当 is_attr == false && is_expr == false 时使用（常量值）
  unique_ptr<Expression> expr;     // 当 is_expr == true 时使用

  void init_attr(const Field &f)
  {
    is_attr     = true;
    is_expr     = false;
    this->field = f;
  }

  void init_value(const Value &v)
  {
    is_attr     = false;
    is_expr     = false;
    this->value = v;
  }

  void init_expr(Expression *e)
  {
    is_expr = true;
    is_attr = false;
    this->expr.reset(e);
  }

  void init_expr(unique_ptr<Expression> &e)
  {
    is_expr    = true;
    is_attr    = false;
    this->expr = std::move(e);
  }
};

class FilterUnit
{
public:
  FilterUnit() = default;
  ~FilterUnit() {}

  void set_comp(CompOp comp) { comp_ = comp; }

  CompOp comp() const { return comp_; }

  void set_left(unique_ptr<FilterObj> obj) { left_ = std::move(obj); }
  void set_right(unique_ptr<FilterObj> obj) { right_ = std::move(obj); }

  const FilterObj &left() const { return *left_; }
  const FilterObj &right() const { return *right_; }

private:
  CompOp    comp_ = NO_OP;
  unique_ptr<FilterObj> left_;
  unique_ptr<FilterObj> right_;
};

/**
 * @brief Filter/谓词/过滤语句
 * @ingroup Statement
 */
class FilterStmt
{
public:
  FilterStmt() = default;
  virtual ~FilterStmt();

  const vector<FilterUnit *> &filter_units() const { return filter_units_; }

  static RC create(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
      const ConditionSqlNode *conditions, int condition_num, FilterStmt *&stmt);

  static RC create_filter_unit(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
      const ConditionSqlNode &condition, FilterUnit *&filter_unit);

  static RC create_filter_unit_recursion(
      Db *db, Table *default_table, unordered_map<string, Table *> *tables, Expression *expr, FilterObj &filter_obj);

private:
  vector<FilterUnit *> filter_units_;  // 默认当前都是AND关系,所以是扁平的

  static RC get_table_and_field(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
      const string &table_name, const string &field_name, Table *&table, const FieldMeta *&field);

  static RC bind_expression(Db *db, Table *default_table, unordered_map<string, Table *> *tables, Expression *expr,
      unique_ptr<Expression> &bound_expr);
};
