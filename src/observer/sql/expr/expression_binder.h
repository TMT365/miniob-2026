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
// Created by Wangyunlai on 2024/05/29.
//

#pragma once

#include "sql/expr/expression.h"

class BinderContext
{
public:
  BinderContext()          = default;
  virtual ~BinderContext() = default;

  void add_table(Table *table) { query_tables_.push_back(table); }

  Table *find_table(const char *table_name) const;

  /**
   * @brief 获取查询涉及的表对象
   * @return 查询涉及的表对象
   */
  const vector<Table *> &query_tables() const { return query_tables_; }

private:
  vector<Table *> query_tables_; /// 查询涉及的表对象，里面封装了表名、字段信息等
};

/**
 * @brief 绑定表达式
 * @details 绑定表达式，就是在SQL解析后，得到文本描述的表达式，将表达式解析为具体的数据库对象
 */
class ExpressionBinder
{
public:
  ExpressionBinder(BinderContext &context) : context_(context) {}
  virtual ~ExpressionBinder() = default;
  /**
   * 绑定表达式，总入口函数，负责将表达式绑定到具体的数据库对象上
   * @param expr unique_ptr<Expression>&，也就是Expression对象的智能指针的引用，表示要绑定的表达式
   * @param bound_expressions vector<unique_ptr<Expression>>&，表示绑定后的表达式集合
   * @return
   */
  RC bind_expression(unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions);

private:
  // SELECT * FROM table_name; 这种查询语句中，*表示查询所有字段，这里需要将*绑定为具体的字段表达式
  RC bind_star_expression(unique_ptr<Expression> &star_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT field1, field2 FROM table_name; 这种查询语句中，field1, field2表示查询具体的字段，这里需要将字段绑定为具体的字段表达式
  RC bind_unbound_field_expression(
      unique_ptr<Expression> &unbound_field_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT 1, 'abc' FROM table_name; 这种查询语句中，1, 'abc'表示查询具体的值，这里需要将值绑定为具体的值表达式
  RC bind_field_expression(unique_ptr<Expression> &field_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT CAST(field1 AS INT) FROM table_name; 这种查询语句中，CAST(field1 AS INT)表示将字段field1转换为INT类型，这里需要将转换表达式绑定为具体的转换表达式
  RC bind_value_expression(unique_ptr<Expression> &value_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT field1 + field2 FROM table_name; 这种查询语句中，field1 + field2表示将字段field1和field2进行加法运算，这里需要将加法表达式绑定为具体的加法表达式
  RC bind_cast_expression(unique_ptr<Expression> &cast_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT field1 > field2 FROM table_name; 这种查询语句中，field1 > field2表示将字段field1和field2进行比较运算，这里需要将比较表达式绑定为具体的比较表达式
  RC bind_comparison_expression(
      unique_ptr<Expression> &comparison_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT field1 AND field2 FROM table_name; 这种查询语句中，field1 AND field2表示将字段field1和field2进行逻辑运算，这里需要将逻辑表达式绑定为具体的逻辑表达式
  RC bind_conjunction_expression(
      unique_ptr<Expression> &conjunction_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT field1 +-*/ field2 FROM table_name; 这种查询语句中，field1 +-*/ field2表示将字段field1和field2进行算术运算，这里需要将算术表达式绑定为具体的算术表达式
  RC bind_arithmetic_expression(
      unique_ptr<Expression> &arithmetic_expr, vector<unique_ptr<Expression>> &bound_expressions);

  // SELECT COUNT(field1) FROM table_name; 这种查询语句中，COUNT(field1)表示将字段field1进行聚合运算，这里需要将聚合表达式绑定为具体的聚合表达式
  RC bind_aggregate_expression(
      unique_ptr<Expression> &aggregate_expr, vector<unique_ptr<Expression>> &bound_expressions);

  BinderContext &context_;
};
