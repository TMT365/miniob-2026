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
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/parser/expression_binder.h"

using namespace std;
using namespace common;

/**
 * @brief 析构函数，释放资源
 * @details 析构函数会释放SelectStmt对象中持有的资源，包括查询涉及的表、查询字段表达式、过滤条件等。
 */
SelectStmt::~SelectStmt()
{
  if (nullptr != where_filter_stmt_) {
    delete where_filter_stmt_;
    where_filter_stmt_ = nullptr;
  }
  if (!table_map_.empty()) {
    table_map_.clear();
  }
}

RC SelectStmt::semantic_analyze_join_node(
    Db *db, JoinSqlNode *join, unordered_map<string, Table *> &table_map, vector<Table *> &tables)
{
  // 递归寻找
  RC rc = RC::SUCCESS;
  if (nullptr == join) {
    return rc;
  }

  if (join->type == TableRefType::BASE_TABLE) {
    const char *table_name = join->table_name.c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. relation name is null. relation named %s", table_name);
      return RC::INVALID_ARGUMENT;
    }
    Table *table = db->find_table(table_name);  /// 在数据库中查找表对象
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
    // TODO 使用insert()是不会插入重复对象的，table_map 是无序表，插入顺序不是输出顺序
    table_map.insert({table_name, table});
    // 所以要使用tables保证输出有序
    tables.push_back(table);
  } else if (join->type == TableRefType::JOIN_NODE) {
    rc = semantic_analyze_join_node(db, join->left.get(), table_map, tables);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot construct join on filter stmt");
      return rc;
    }
    rc = semantic_analyze_join_node(db, join->right.get(), table_map, tables);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot construct join on filter stmt");
      return rc;
    }
    // ON 只要不为空
    if (!join->on_conditions.empty()) {
      // 创建 JOIN ON 的statement
      FilterStmt *on_filter_stmt = nullptr;
      rc                         = FilterStmt::create(db,
          nullptr,
          &table_map,
          join->on_conditions.data(),
          static_cast<int>(join->on_conditions.size()),
          on_filter_stmt);
      if (rc != RC::SUCCESS) {
        LOG_WARN("cannot construct join on filter stmt");
        return rc;
      }
      join->on_filter_stmt = on_filter_stmt;
    }
  }
  return rc;
}

RC SelectStmt::create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  BinderContext binder_context; /// 创建BinderContext对象，用于绑定查询涉及的表


  vector<Table *>                tables; /// 存储查询涉及的表对象
  unordered_map<string, Table *> table_map; /// 使用unordered_map存储表名和表对象的映射关系，方便后续查询和绑定字段表达式

  // 在递归找表的同时创建JOIN stmt
  RC rc = semantic_analyze_join_node(db, select_sql.join.get(), table_map, tables);
  if (OB_FAIL(rc)) {
    LOG_WARN("semantic analyze join node failed. rc=%s", strrc(rc));
    return rc;
  }

  for (auto &table : tables) {
    binder_context.add_table(table); /// 将表对象添加到BinderContext中，方便后续绑定字段表达式
  }


  vector<unique_ptr<Expression>> bound_expressions;
  ExpressionBinder expression_binder(binder_context);
  // question? 这里的expression不只是限制于字段啊？可以有字段、算数表达式、常量表达式、聚合表达式
  // 现在连最基本的多表指定字段都无法支持：
  for (unique_ptr<Expression> &expression : select_sql.expressions) {
    // question 这里的bind是在干嘛，UnboundFieldExpr --> FieldExpr?
    rc = expression_binder.bind_expression(expression, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> group_by_expressions;
  for (unique_ptr<Expression> &expression : select_sql.group_by) {
     rc = expression_binder.bind_expression(expression, group_by_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  Table *default_table = nullptr;
  if (tables.size() == 1) {
    default_table = tables[0];
  }

  /*
   * NOTE 现在的 condition 不一样了，内部是 expression，ArithmeticExpr 和 UnBoundFieldExpr
   * NOTE 在SELECT * ...的UnBoundFieldExpt要绑定变成FieldExpr表达式，在where后面的也需要
   */
  FilterStmt *where_filter_stmt = new FilterStmt();
  rc = FilterStmt::create(db,
      default_table,
      &table_map,
      select_sql.conditions.data(),
      static_cast<int>(select_sql.conditions.size()),
      where_filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("cannot construct where filter stmt");
    delete where_filter_stmt;
    return rc;
  }


  // everything alright
  SelectStmt *select_stmt = new SelectStmt();

  select_stmt->query_expressions_.swap(bound_expressions);
  select_stmt->where_filter_stmt_ = where_filter_stmt;
  select_stmt->join_sql_node_     = select_sql.join.get();
  select_stmt->group_by_.swap(group_by_expressions);
  select_stmt->table_map_ = table_map;
  stmt                      = select_stmt;
  return RC::SUCCESS;
}
