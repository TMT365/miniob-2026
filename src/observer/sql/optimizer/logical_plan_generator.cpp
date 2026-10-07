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

#include "sql/optimizer/logical_plan_generator.h"

#include "common/log/log.h"

#include "sql/operator/calc_logical_operator.h"
#include "sql/operator/delete_logical_operator.h"
#include "sql/operator/explain_logical_operator.h"
#include "sql/operator/insert_logical_operator.h"
#include "sql/operator/join_logical_operator.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/predicate_logical_operator.h"
#include "sql/operator/project_logical_operator.h"
#include "sql/operator/table_get_logical_operator.h"
#include "sql/operator/group_by_logical_operator.h"
#include "sql/operator/update_logical_operator.h"

#include "sql/stmt/calc_stmt.h"
#include "sql/stmt/delete_stmt.h"
#include "sql/stmt/explain_stmt.h"
#include "sql/stmt/filter_stmt.h"
#include "sql/stmt/insert_stmt.h"
#include "sql/stmt/select_stmt.h"
#include "sql/stmt/update_stmt.h"
#include "sql/stmt/stmt.h"

#include "sql/expr/expression_iterator.h"

using namespace std;
using namespace common;

RC LogicalPlanGenerator::create(Stmt *stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  RC rc = RC::SUCCESS;
  switch (stmt->type()) {
    case StmtType::CALC: {
      CalcStmt *calc_stmt = static_cast<CalcStmt *>(stmt);

      rc = create_plan(calc_stmt, logical_operator);
    } break;

    case StmtType::SELECT: {
      SelectStmt *select_stmt = static_cast<SelectStmt *>(stmt);

      rc = create_plan(select_stmt, logical_operator);
    } break;

    case StmtType::INSERT: {
      InsertStmt *insert_stmt = static_cast<InsertStmt *>(stmt);

      rc = create_plan(insert_stmt, logical_operator);
    } break;

    case StmtType::DELETE: {
      DeleteStmt *delete_stmt = static_cast<DeleteStmt *>(stmt);

      rc = create_plan(delete_stmt, logical_operator);
    } break;

    case StmtType::UPDATE: {
      UpdateStmt *update_stmt = static_cast<UpdateStmt *>(stmt);

      rc = create_plan(update_stmt, logical_operator);
    } break;

    case StmtType::EXPLAIN: {
      ExplainStmt *explain_stmt = static_cast<ExplainStmt *>(stmt);

      rc = create_plan(explain_stmt, logical_operator);
    } break;
    default: {
      rc = RC::UNIMPLEMENTED;
    }
  }
  return rc;
}

RC LogicalPlanGenerator::create_plan(CalcStmt *calc_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  logical_operator.reset(new CalcLogicalOperator(std::move(calc_stmt->expressions())));
  return RC::SUCCESS;
}

RC LogicalPlanGenerator::create_join_on_pred_oper(
    SelectStmt *select_stmt, JoinSqlNode *join, unique_ptr<LogicalOperator> &join_on_pred_oper)
{
  // 递归产生逻辑计划
  RC rc = RC::SUCCESS;
  if (join->type == TableRefType::BASE_TABLE) {
    // TableGetLogicalOperator 要从表里面那记录了
    // 从JoinSqlNode 节点里面获取表名
    auto iter = select_stmt->table_map().find(join->table_name);
    if (iter == select_stmt->table_map().end()) {
      LOG_WARN("table %s not found", join->table_name.c_str());
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
    Table *table = iter->second;
    if (table) {
      // 创建 TableGetLogicalOperator
      join_on_pred_oper = make_unique<TableGetLogicalOperator>(table, ReadWriteMode::READ_ONLY);
      return RC::SUCCESS;
    }
  } else if (join->type == TableRefType::JOIN_NODE) {
    unique_ptr<LogicalOperator> left_op;
    rc = create_join_on_pred_oper(select_stmt, join->left.get(), left_op);
    if (OB_FAIL(rc))
      return rc;

    unique_ptr<LogicalOperator> right_op;
    rc = create_join_on_pred_oper(select_stmt, join->right.get(), right_op);
    if (OB_FAIL(rc))
      return rc;

    auto join_op = make_unique<JoinLogicalOperator>();
    join_op->add_child(std::move(left_op));
    join_op->add_child(std::move(right_op));
    join_op->set_join_type(join->join_type);

    // 直接使用已经构造好的 FilterStmt
    // join->join_type == JoinType::INNER_JOIN &&
    if (join->on_filter_stmt != nullptr) {
      unique_ptr<LogicalOperator> on_pred_oper;
      rc = create_plan(join->on_filter_stmt, on_pred_oper);
      if (OB_FAIL(rc)) {
        LOG_WARN("create join on predicate logical plan failed");
        return rc;
      }
      join_op->set_on_predicate(std::move(on_pred_oper));
    }
    join_on_pred_oper = std::move(join_op);
    return RC::SUCCESS;
  }
  return RC::UNIMPLEMENTED;
}

RC LogicalPlanGenerator::create_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  unique_ptr<LogicalOperator> *last_oper = nullptr;
  unique_ptr<LogicalOperator>  join_on_pred_oper(nullptr);
  last_oper = &join_on_pred_oper;

  // ========== 1. 构建JOIN子树：递归生成TableGet / JoinLogicalOperator（内部自带ON条件） ==========
  // create_join_on_filter_oper：生成完整的join树，ON条件挂载在Join算子内部
  RC rc = create_join_on_pred_oper(select_stmt, select_stmt->join_sql_node(), join_on_pred_oper);
  if (OB_FAIL(rc)) {
    LOG_WARN("create from join sql node plan failed, rc=%s", strrc(rc));
    return rc;
  }

  // ========== 2. WHERE 过滤算子（在JOIN结果之上） ==========
  unique_ptr<LogicalOperator> where_pred_oper;
  if (select_stmt->where_filter_stmt() != nullptr) {
    rc = create_plan(select_stmt->where_filter_stmt(), where_pred_oper);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to create where predicate logical plan. rc=%s", strrc(rc));
      return rc;
    }
  }

  if (where_pred_oper) {
    if (*last_oper) {
      where_pred_oper->add_child(std::move(*last_oper));
    }
    last_oper = &where_pred_oper;
  }

  // ========== 3. GROUP BY 聚合 ==========
  unique_ptr<LogicalOperator> group_by_oper;
  rc = create_group_by_plan(select_stmt, group_by_oper);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create group by logical plan. rc=%s", strrc(rc));
    return rc;
  }

  if (group_by_oper) {
    if (*last_oper) {
      group_by_oper->add_child(std::move(*last_oper));
    }
    last_oper = &group_by_oper;
  }

  // ========== 4. Project投影 ==========
  unique_ptr<LogicalOperator> project_oper = make_unique<ProjectLogicalOperator>(std::move(select_stmt->query_expressions()));
  if (*last_oper) {
    project_oper->add_child(std::move(*last_oper));
  }
  last_oper = &project_oper;

  logical_operator = std::move(*last_oper);
  return RC::SUCCESS;
}

RC LogicalPlanGenerator::create_plan(FilterStmt *filter_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  RC                                  rc = RC::SUCCESS;
  vector<unique_ptr<Expression>> cmp_exprs;
  const vector<FilterUnit *>    &filter_units = filter_stmt->filter_units();
  // NOTE 现在的循环只能支持AND这种扁平的where语句, 想要支持OR等需要使用树形结构，采用递归处理
  for (const FilterUnit *filter_unit : filter_units) {
    const FilterObj &filter_obj_left  = filter_unit->left();
    const FilterObj &filter_obj_right = filter_unit->right();
    // NOTE 从这里把原来conditionSqlNode里面的filterUnit变成expression
    /* 现在的 FilterObj 支持3种类型：字段，常量、复杂的表达式(Arithmetic = ValueExpr + FieldExpr)
     *
     */
    unique_ptr<Expression> left(filter_obj_left.is_attr
                                    ? static_cast<Expression *>(new FieldExpr(filter_obj_left.field))
                                    : filter_obj_left.is_expr ? filter_obj_left.expr->copy().release()
                                  : static_cast<Expression *>(new ValueExpr(filter_obj_left.value)));  // Value

    unique_ptr<Expression> right(filter_obj_right.is_attr
                                    ? static_cast<Expression *>(new FieldExpr(filter_obj_right.field))
        : filter_obj_right.is_expr ? filter_obj_right.expr->copy().release()
                                   : static_cast<Expression *>(new ValueExpr(filter_obj_right.value)));  // Value

    // 2. 递归做类型对齐
    rc = align_expression_types(left, right);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to align expression types");
      return rc;
    }

    cmp_exprs.emplace_back(std::make_unique<ComparisonExpr>(filter_unit->comp(), std::move(left), std::move(right)));
  }

  unique_ptr<PredicateLogicalOperator> predicate_oper;
  if (!cmp_exprs.empty()) {
    unique_ptr<ConjunctionExpr> conjunction_expr(new ConjunctionExpr(ConjunctionExpr::Type::AND_EXPR, cmp_exprs));
    predicate_oper = unique_ptr<PredicateLogicalOperator>(new PredicateLogicalOperator(std::move(conjunction_expr)));
  }

  logical_operator = std::move(predicate_oper);
  return rc;
}

int LogicalPlanGenerator::implicit_cast_cost(AttrType from, AttrType to)
{
  if (from == to) {
    return 0;
  }
  return DataType::type_instance(from)->cast_cost(to);
}

// 递归地把 left 和 right 两棵表达式树对齐类型
// 策略：比较两棵树的根类型，选择 cast 代价小的方向，然后递归处理子节点
RC LogicalPlanGenerator::align_expression_types(unique_ptr<Expression> &left, unique_ptr<Expression> &right)
{
  // 如果左是 Arithmetic，先递归对齐它的左右子节点与 right
  if (left->type() == ExprType::ARITHMETIC) {
    auto                  *left_arith = static_cast<ArithmeticExpr *>(left.get());
    unique_ptr<Expression> ll(left_arith->left().release());
    unique_ptr<Expression> lr;
    if (left_arith->right()) {
      lr.reset(left_arith->right().release());
    }
    // 先递归左子节点
    RC rc = align_expression_types(ll, right);
    if (rc != RC::SUCCESS)
      return rc;
    // 再递归右子节点
    if (lr) {
      rc = align_expression_types(lr, right);
      if (rc != RC::SUCCESS)
        return rc;
    }
    // 重建 Arithmetic 节点（子节点已对齐）
    left.reset(new ArithmeticExpr(left_arith->arithmetic_type(), ll.release(), lr.release()));
  }
  // 【修复】else if，不会同时执行两个分支
  else if (right->type() == ExprType::ARITHMETIC) {
    auto                  *right_arith = static_cast<ArithmeticExpr *>(right.get());
    unique_ptr<Expression> rl(right_arith->left().release());
    unique_ptr<Expression> rr;
    if (right_arith->right()) {
      rr.reset(right_arith->right().release());
    }
    // 先递归左子节点
    RC rc = align_expression_types(left, rl);
    if (rc != RC::SUCCESS)
      return rc;
    // 再递归右子节点
    if (rr) {
      rc = align_expression_types(left, rr);
      if (rc != RC::SUCCESS)
        return rc;
    }
    // 重建 Arithmetic 节点（子节点已对齐）
    right.reset(new ArithmeticExpr(right_arith->arithmetic_type(), rl.release(), rr.release()));
  }

  // 此时左右都不是 Arithmetic（或已经是重建后的），比较根类型
  AttrType left_type  = left->value_type();
  AttrType right_type = right->value_type();
  if (left_type == right_type) {
    return RC::SUCCESS;
  }

  // 类型不同，选择 cast 代价小的方向
  auto left_to_right_cost = implicit_cast_cost(left_type, right_type);
  auto right_to_left_cost = implicit_cast_cost(right_type, left_type);

  if (left_to_right_cost <= right_to_left_cost && left_to_right_cost != INT32_MAX) {
    left = make_unique<CastExpr>(std::move(left), right_type);
  } else if (right_to_left_cost < left_to_right_cost && right_to_left_cost != INT32_MAX) {
    right = make_unique<CastExpr>(std::move(right), left_type);
  } else {
    LOG_WARN("unsupported cast from %s to %s",
        attr_type_to_string(left_type), attr_type_to_string(right_type));
    return RC::UNSUPPORTED;
  }
  return RC::SUCCESS;
}

RC LogicalPlanGenerator::create_plan(InsertStmt *insert_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  Table        *table = insert_stmt->table();
  vector<Value> values(insert_stmt->values(), insert_stmt->values() + insert_stmt->value_amount());

  InsertLogicalOperator *insert_operator = new InsertLogicalOperator(table, values);
  logical_operator.reset(insert_operator);
  return RC::SUCCESS;
}

RC LogicalPlanGenerator::create_plan(DeleteStmt *delete_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  Table                      *table       = delete_stmt->table();
  FilterStmt                 *filter_stmt = delete_stmt->filter_stmt();
  unique_ptr<LogicalOperator> table_get_oper(new TableGetLogicalOperator(table, ReadWriteMode::READ_WRITE));

  unique_ptr<LogicalOperator> predicate_oper;

  RC rc = create_plan(filter_stmt, predicate_oper);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  unique_ptr<LogicalOperator> delete_oper(new DeleteLogicalOperator(table));

  if (predicate_oper) {
    predicate_oper->add_child(std::move(table_get_oper));
    delete_oper->add_child(std::move(predicate_oper));
  } else {
    delete_oper->add_child(std::move(table_get_oper));
  }

  logical_operator = std::move(delete_oper);
  return rc;
}

// TODO : 目前update语句支持更新多个个字段，后续可以扩展为多个表的更新
RC LogicalPlanGenerator::create_plan(UpdateStmt *update_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  Table                      *table       = update_stmt->table();
  FilterStmt                 *filter_stmt = update_stmt->filter_stmt();

  unique_ptr<LogicalOperator> table_get_oper(new TableGetLogicalOperator(table, ReadWriteMode::READ_WRITE));

  unique_ptr<LogicalOperator> predicate_oper;

  // 创建过滤条件的逻辑计划
  RC rc = create_plan(filter_stmt, predicate_oper);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  // 创建更新操作的逻辑计划
  // 使用新的构造函数，传递提前求值的 values 和需要运行时求值的 exprs
  unique_ptr<LogicalOperator> update_oper(new UpdateLogicalOperator(
      table,
      update_stmt->take_fields(),
      update_stmt->take_values(),    // 转移所有权
      update_stmt->take_exprs(),     // 转移所有权
      update_stmt->value_amount(),
      filter_stmt));

  if (predicate_oper) {
    predicate_oper->add_child(std::move(table_get_oper));
    update_oper->add_child(std::move(predicate_oper));
  } else {
    update_oper->add_child(std::move(table_get_oper));
  }

  logical_operator = std::move(update_oper);
  return rc;
}

RC LogicalPlanGenerator::create_plan(ExplainStmt *explain_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  unique_ptr<LogicalOperator> child_oper;

  Stmt *child_stmt = explain_stmt->child();

  RC rc = create(child_stmt, child_oper);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create explain's child operator. rc=%s", strrc(rc));
    return rc;
  }

  logical_operator = unique_ptr<LogicalOperator>(new ExplainLogicalOperator);
  logical_operator->add_child(std::move(child_oper));
  return rc;
}

RC LogicalPlanGenerator::create_group_by_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  vector<unique_ptr<Expression>> &group_by_expressions = select_stmt->group_by();
  vector<Expression *> aggregate_expressions;
  vector<unique_ptr<Expression>> &query_expressions = select_stmt->query_expressions();
  // 匿名函数递归收集聚合表达式
  function<RC(unique_ptr<Expression>&)> collector = [&](unique_ptr<Expression> &expr) -> RC {
    RC rc = RC::SUCCESS;
    if (expr->type() == ExprType::AGGREGATION) {
      expr->set_pos(aggregate_expressions.size() + group_by_expressions.size());
      aggregate_expressions.push_back(expr.get());
    }
    rc = ExpressionIterator::iterate_child_expr(*expr, collector);
    return rc;
  };
  // 匿名函数递归收集分组表达式
  function<RC(unique_ptr<Expression>&)> bind_group_by_expr = [&](unique_ptr<Expression> &expr) -> RC {
    RC rc = RC::SUCCESS;
    for (size_t i = 0; i < group_by_expressions.size(); i++) {
      auto &group_by = group_by_expressions[i];
      if (expr->type() == ExprType::AGGREGATION) {
        break;
      } else if (expr->equal(*group_by)) {
        expr->set_pos(i);
        continue;
      } else {
        rc = ExpressionIterator::iterate_child_expr(*expr, bind_group_by_expr);
      }
    }
    return rc;
  };

 bool found_unbound_column = false;
  function<RC(unique_ptr<Expression>&)> find_unbound_column = [&](unique_ptr<Expression> &expr) -> RC {
    RC rc = RC::SUCCESS;
    if (expr->type() == ExprType::AGGREGATION) {
      // do nothing
    } else if (expr->pos() != -1) {
      // do nothing
    } else if (expr->type() == ExprType::FIELD) {
      found_unbound_column = true;
    }else {
      rc = ExpressionIterator::iterate_child_expr(*expr, find_unbound_column);
    }
    return rc;
  };
  

  for (unique_ptr<Expression> &expression : query_expressions) {
    bind_group_by_expr(expression);
  }

  for (unique_ptr<Expression> &expression : query_expressions) {
    find_unbound_column(expression);
  }

  // collect all aggregate expressions
  for (unique_ptr<Expression> &expression : query_expressions) {
    collector(expression);
  }

  if (group_by_expressions.empty() && aggregate_expressions.empty()) {
    // 既没有group by也没有聚合函数，不需要group by
    return RC::SUCCESS;
  }

  if (found_unbound_column) {
    LOG_WARN("column must appear in the GROUP BY clause or must be part of an aggregate function");
    return RC::INVALID_ARGUMENT;
  }

  // 如果只需要聚合，但是没有group by 语句，需要生成一个空的group by 语句

  auto group_by_oper = make_unique<GroupByLogicalOperator>(std::move(group_by_expressions),
                                                           std::move(aggregate_expressions));
  logical_operator = std::move(group_by_oper);
  return RC::SUCCESS;
}
