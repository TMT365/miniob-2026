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
// Created by Wangyunlai on 2022/6/5.
//

#pragma once

#include "common/sys/rc.h"
#include "sql/stmt/stmt.h"
#include "sql/expr/expression_binder.h"
#include "storage/field/field.h"

class FieldMeta;
class FilterStmt;
class Db;
class Table;

/**
 * @brief 表示select语句
 * @ingroup Statement
 */
class SelectStmt : public Stmt
{
public:
  SelectStmt() = default;
  ~SelectStmt() override;
  StmtType type() const override { return StmtType::SELECT; }

  static RC create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt);

  // getter
  // const vector<Table *> &tables() const { return tables_; }
  FilterStmt                     *where_filter_stmt() const { return where_filter_stmt_; }
  FilterStmt                     *having_filter_stmt() const { return having_filter_stmt_; }
  JoinSqlNode                    *join_sql_node() const { return join_sql_node_; }
  vector<unique_ptr<Expression>> &query_expressions() { return query_expressions_; }
  vector<unique_ptr<Expression>> &group_by() { return group_by_; }
  unordered_map<string, Table *> &table_map() { return table_map_; }

private:
  vector<unique_ptr<Expression>> query_expressions_; /// 查询字段表达式，是扁平化的，可能包含聚合函数
  unordered_map<string, Table *> table_map_;
  FilterStmt                    *where_filter_stmt_  = nullptr;  /// 查询条件
  FilterStmt                    *having_filter_stmt_ = nullptr;
  vector<unique_ptr<Expression>> group_by_;  /// 分组字段表达式，是数组，支持多字段分组
  JoinSqlNode                   *join_sql_node_;
  ;
  static RC semantic_analyze_join_node(
      Db *db, JoinSqlNode *join, unordered_map<string, Table *> &table_map, vector<BinderContext::TableRef> &tables);
};
