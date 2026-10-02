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

#include "sql/stmt/update_stmt.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "sql/expr/expression.h"
#include "sql/parser/expression_binder.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

using namespace std;
using namespace common;

UpdateStmt::UpdateStmt(Table *table, vector<unique_ptr<Field>> &&fields,
                       vector<unique_ptr<UpdatePairNode>> &&update_pair_nodes,
                       vector<Value> &&values,
                       vector<unique_ptr<Expression>> &&exprs,
                       int value_amount, FilterStmt *filter_stmt)
    : table_(table),
      fields_(std::move(fields)),
      update_pair_nodes_(std::move(update_pair_nodes)),
      value_amount_(value_amount),
      filter_stmt_(filter_stmt),
      values_(std::move(values)),
      exprs_(std::move(exprs))
{}

UpdateStmt::~UpdateStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }

  if (nullptr != table_) {
    table_ = nullptr;
  }
}

RC UpdateStmt::create(Db *db, UpdateSqlNode &update_sql, Stmt *&stmt)
{
  const char *table_name = update_sql.relation_name.c_str();
  if (nullptr == table_name || nullptr == db) {
    LOG_WARN("invalid argument. db=%p, table_name=%p", db, table_name);
    return RC::INVALID_ARGUMENT;
  }

  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  unordered_map<string, Table *> table_map;
  table_map.insert(pair<string, Table *>(string(table_name), table));

  FilterStmt *filter_stmt = nullptr;
  RC rc = FilterStmt::create(
      db, table, &table_map, update_sql.conditions.data(), static_cast<int>(update_sql.conditions.size()), filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create filter statement. rc=%d:%s", rc, strrc(rc));
    return rc;
  }

  const vector<unique_ptr<UpdatePairNode>> &update_pairs = update_sql.update_pairs;
  if (update_pairs.empty()) {
    LOG_WARN("no update pairs. table=%s", table_name);
    delete filter_stmt;
    return RC::INVALID_ARGUMENT;
  }
  int value_amount = static_cast<int>(update_pairs.size());
  vector<unique_ptr<Field>> fields;
  fields.reserve(value_amount);

  vector<Value> values;
  values.reserve(value_amount);

  vector<unique_ptr<Expression>> exprs;
  exprs.reserve(value_amount);

  // 绑定表达式中的字段引用（将 UnboundFieldExpr 转换为 FieldExpr）
  BinderContext binder_context;
  binder_context.add_table(table);
  ExpressionBinder expression_binder(binder_context);
  for (int i = 0; i < value_amount; i++) {
    vector<unique_ptr<Expression>> bound_expressions;
    rc = expression_binder.bind_expression(update_pairs[i]->value, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      delete filter_stmt;
      return rc;
    }
    if (!bound_expressions.empty()) {
      update_pairs[i]->value = std::move(bound_expressions[0]);
    }
  }

  // 找到表里面的字段，并尝试提前求值
  for (int i = 0; i < value_amount; i++) {
    const char *attribute_name = update_sql.update_pairs[i]->attr.attribute_name.c_str();
    const TableMeta &table_meta = table->table_meta();
    const FieldMeta *field_meta = table_meta.field(attribute_name);

    if (nullptr == field_meta) {
      LOG_WARN("no such field. table=%s, field=%s", table_name, attribute_name);
      delete filter_stmt;
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }

    auto field = std::make_unique<Field>(table, field_meta);
    if (field == nullptr) {
      LOG_WARN("failed to create field. table=%s, field=%s", table_name, attribute_name);
      delete filter_stmt;
      return RC::NOMEM;
    }
    fields.push_back(std::move(field));

    Expression *expr = update_pairs[i]->value.get();
    Value value;
    rc = expr->try_get_value(value);
    if (rc == RC::SUCCESS) {
      values.push_back(value);
      exprs.push_back(nullptr);
    } else {
      values.push_back(Value());
      exprs.push_back(std::move(update_pairs[i]->value));
    }
  }

  stmt = new UpdateStmt(table, std::move(fields), std::move(update_sql.update_pairs),
                        std::move(values), std::move(exprs),
                        value_amount, filter_stmt);

  return RC::SUCCESS;
}
