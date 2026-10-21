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
// Created by Wangyunlai on 2022/12/30.
//

#include "sql/optimizer/predicate_pushdown_rewriter.h"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/log/log.h"
#include "sql/expr/expression.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/join_logical_operator.h"
#include "sql/operator/predicate_logical_operator.h"
#include "sql/operator/table_get_logical_operator.h"

namespace
{

/**
 * @brief 沿 JOIN 子树向下收集所有 TableGetLogicalOperator，按表名索引。
 *        遇到 JOIN 节点就递归两边；遇到 TABLE_GET 就收入；遇到其它类型节点不递归
 *        （表扫描之上的 PREDICATE/PROJECTION 之类的算子本来就不应出现在我们关心的 spine 上）。
 */
void collect_table_gets_in_subtree( LogicalOperator *op, std::unordered_map<std::string, TableGetLogicalOperator *> &out)
{
  if (op == nullptr) {
    return;
  }
  if (op->type() == LogicalOperatorType::TABLE_GET) {
    auto *tget = static_cast<TableGetLogicalOperator *>(op);
    if (tget->table() != nullptr) {
      out[std::string(tget->table()->name())] = tget;
    }
    return;
  }
  if (op->type() == LogicalOperatorType::JOIN) {
    for (auto &child : op->children()) {
      collect_table_gets_in_subtree(child.get(), out);
    }
  }
}

/**
 * @brief 判断 expr 引用的表集合，如果只引用 allowed 中恰好一张表，返回该表名；
 *        引用 0 张（常量谓词）或 2+ 张、或引用了不在 allowed 里的表，返回 ""。
 * @note 允许 expr 嵌套 ConjunctionExpr / ComparisonExpr / CastExpr / ArithmeticExpr / FunctionExpr，
 *       按 Expression 子类各自的 accessor 递归。
 */
std::string find_single_referenced_table(
    const Expression *expr, const std::unordered_set<std::string> &allowed)
{
  bool   any_field  = false;
  bool   ambiguous  = false;
  std::string first;

  std::function<void(Expression *)> walk = [&](Expression *e) {
    if (e == nullptr || ambiguous) {
      return;
    }
    if (e->type() == ExprType::FIELD) {
      const char *tname = static_cast<FieldExpr *>(e)->table_name();
      any_field = true;
      if (allowed.find(tname) == allowed.end()) {
        // 引用的表不属于当前 JOIN 子树 —— 留上层处理
        ambiguous = true;
        return;
      }
      if (first.empty()) {
        first = tname;
      } else if (first != tname) {
        ambiguous = true;
      }
      return;
    }
    switch (e->type()) {
      case ExprType::CONJUNCTION: {
        for (const auto &c : static_cast<ConjunctionExpr *>(e)->children()) {
          walk(c.get());
        }
      } break;
      case ExprType::COMPARISON: {
        auto *cmp = static_cast<ComparisonExpr *>(e);
        walk(cmp->left().get());
        walk(cmp->right().get());
      } break;
      case ExprType::CAST: {
        walk(static_cast<CastExpr *>(e)->child().get());
      } break;
      case ExprType::ARITHMETIC: {
        auto *arith = static_cast<ArithmeticExpr *>(e);
        walk(arith->left().get());
        walk(arith->right().get());
      } break;
      case ExprType::FUNCTION: {
        for (const auto &c : static_cast<FunctionExpr *>(e)->args()) {
          walk(c.get());
        }
      } break;
      default:
        break;
    }
  };
  walk(const_cast<Expression *>(expr));
  if (!any_field || ambiguous) {
    return std::string();
  }
  return first;
}

}  // namespace

/**
 * @brief 共用下推核心：把 expressions 中只引用 join_subtree_root 子树里某张表的 ComparisonExpr
 *        推到对应 TableGet::predicates_ 上。跨表 / 常量 / OR 留在原位。
 *        出口清理（空 WHERE 用 ValueExpr(true) 占位 / 空 ON 删 on_predicate_）由各自 wrapper 处理。
 * @return RC::UNIMPLEMENTED 表示遇到了 OR；RC::SUCCESS 表示处理完（即使 change_made 为 false）。
 */
RC PredicatePushdownRewriter::pushdown_single_side_into_subtree_tables(
    LogicalOperator &join_subtree_root,
    vector<unique_ptr<Expression>> &expressions,
    bool &change_made)
{
  std::unordered_map<std::string, TableGetLogicalOperator *> tables;
  collect_table_gets_in_subtree(&join_subtree_root, tables);
  if (tables.empty()) {
    return RC::SUCCESS;
  }

  std::unordered_set<std::string> allowed;
  allowed.reserve(tables.size());
  for (const auto &kv : tables) {
    allowed.insert(kv.first);
  }

  for (auto &pred : expressions) {
    if (pred == nullptr) {
      continue;
    }
    std::unordered_map<std::string, std::vector<unique_ptr<Expression>>> pushed_by_table;
    bool                                                              sub_change = false;
    RC rc = split_predicate_by_table(pred, tables, allowed, pushed_by_table, sub_change);
    if (rc != RC::SUCCESS) {
      // OR 或其它原因 —— 不动这个谓词
      continue;
    }
    if (sub_change) {
      change_made = true;
    }
    for (auto &kv : pushed_by_table) {
      auto it = tables.find(kv.first);
      if (it == tables.end()) {
        continue;
      }
      for (auto &p : kv.second) {
        it->second->predicates().emplace_back(std::move(p));
      }
    }
  }
  return RC::SUCCESS;
}

RC PredicatePushdownRewriter::rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made)
{
  RC rc = RC::SUCCESS;

  // Step 3：JOIN 节点上挂着的 ON 谓词，单边条件可以下推到子树里对应 TableGet。
  // 必须放在 PREDICATE 检查之前，否则会错过 JOIN 节点。
  if (oper->type() == LogicalOperatorType::JOIN) {
    auto &join_oper = static_cast<JoinLogicalOperator &>(*oper);
    rc = pushdown_on_conditions(join_oper, change_made);
    if (rc != RC::SUCCESS) {
      return rc;
    }
  }

  if (oper->type() != LogicalOperatorType::PREDICATE) {
    return rc;
  }

  if (oper->children().size() != 1) {
    return rc;
  }

  unique_ptr<LogicalOperator> &child_oper = oper->children().front();
  if (child_oper->type() != LogicalOperatorType::TABLE_GET) {
    // Step 2：PredicateLogicalOperator 的子节点是 JOIN —— 把单边 WHERE 谓词下推到 JOIN 子树里对应的 TableGet
    if (child_oper->type() == LogicalOperatorType::JOIN) {
      rc = pushdown_predicates_into_join(*child_oper, oper->expressions(), change_made);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to pushdown predicates into join. rc=%s", strrc(rc));
        return rc;
      }
    }
    return rc;
  }

  auto table_get_oper = static_cast<TableGetLogicalOperator *>(child_oper.get());
  vector<unique_ptr<Expression>> &predicate_oper_exprs = oper->expressions();
  if (predicate_oper_exprs.size() != 1) {
    return rc;
  }

  unique_ptr<Expression> &predicate_expr = predicate_oper_exprs.front();
  vector<unique_ptr<Expression>> pushdown_exprs;
  rc = get_exprs_can_pushdown(predicate_expr, pushdown_exprs);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get exprs can pushdown. rc=%s", strrc(rc));
    return rc;
  }

  if (!predicate_expr || is_empty_predicate(predicate_expr)) {
    // 所有的表达式都下推到了下层算子
    // 这个predicate operator其实就可以不要了。但是这里没办法删除，弄一个空的表达式吧
    LOG_TRACE("all expressions of predicate operator were pushdown to table get operator, then make a fake one");

    Value value((bool)true);
    predicate_expr = unique_ptr<Expression>(new ValueExpr(value));
  }

  if (!pushdown_exprs.empty()) {
    change_made = true;
    table_get_oper->set_predicates(std::move(pushdown_exprs));
  }
  return rc;
}

RC PredicatePushdownRewriter::pushdown_predicates_into_join(LogicalOperator &join_oper,
    vector<unique_ptr<Expression>> &predicate_exprs,
    bool &change_made)
{
  // WHERE 谓词下推到 JOIN 子树里对应的 TableGet（含中间表）
  bool sub_change = false;
  RC   rc        = pushdown_single_side_into_subtree_tables(join_oper, predicate_exprs, sub_change);
  if (rc == RC::UNIMPLEMENTED) {
    return rc;
  }
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to pushdown predicates into join. rc=%s", strrc(rc));
    return rc;
  }
  if (sub_change) {
    change_made = true;
  }

  // WHERE 出口清理：被推空的谓词用 ValueExpr(true) 占位，保持 PredicateLogicalOperator 的不变量
  for (auto &pred : predicate_exprs) {
    if (is_empty_predicate(pred)) {
      pred = make_unique<ValueExpr>(Value((bool)true));
    }
  }
  return RC::SUCCESS;
}

RC PredicatePushdownRewriter::pushdown_on_conditions(JoinLogicalOperator &join_oper, bool &change_made)
{
  if (!join_oper.on_predicate()) {
    return RC::SUCCESS;
  }
  auto &on_pred = join_oper.on_predicate();
  ASSERT(on_pred->type() == LogicalOperatorType::PREDICATE,
         "JoinLogicalOperator::on_predicate_ should be PredicateLogicalOperator");

  vector<unique_ptr<Expression>> &exprs = on_pred->expressions();
  if (exprs.empty()) {
    return RC::SUCCESS;
  }

  bool sub_change = false;
  RC   rc        = pushdown_single_side_into_subtree_tables(join_oper, exprs, sub_change);
  if (rc == RC::UNIMPLEMENTED) {
    return rc;
  }
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to pushdown on conditions. rc=%s", strrc(rc));
    return rc;
  }
  if (sub_change) {
    change_made = true;
  }

  // ON 出口清理：被推空的谓词置 null；全部推空则整个 on_predicate_ 置 null
  // 这样下游 physical_plan_generator 不会再包一个无意义的 PredicatePhysicalOperator
  for (auto &pred : exprs) {
    if (is_empty_predicate(pred)) {
      pred.reset();
    }
  }
  // nullptr 集中到容器尾部再统一删除，避免遍历中改容器
  exprs.erase(std::remove(exprs.begin(), exprs.end(), nullptr), exprs.end());
  if (exprs.empty()) {
    join_oper.on_predicate().reset();
  }
  return RC::SUCCESS;
}

RC PredicatePushdownRewriter::split_predicate_by_table(unique_ptr<Expression> &expr,
    const std::unordered_map<std::string, TableGetLogicalOperator *> &tables,
    const std::unordered_set<std::string> &allowed,
    std::unordered_map<std::string, std::vector<unique_ptr<Expression>>> &pushed_by_table,
    bool &change_made)
{
  if (expr == nullptr) {
    return RC::SUCCESS;
  }

  // OR 连接暂时不下推
  if (expr->type() == ExprType::CONJUNCTION) {
    auto *conj = static_cast<ConjunctionExpr *>(expr.get());
    if (conj->conjunction_type() == ConjunctionExpr::Type::OR_EXPR) {
      return RC::UNIMPLEMENTED;
    }
  }

  if (expr->type() == ExprType::CONJUNCTION) {
    auto &children = static_cast<ConjunctionExpr *>(expr.get())->children();
    for (auto it = children.begin(); it != children.end();) {
      RC rc = split_predicate_by_table(*it, tables, allowed, pushed_by_table, change_made);
      if (rc != RC::SUCCESS) {
        return rc;
      }
      if (!(*it)) {
        it = children.erase(it);
      } else {
        ++it;
      }
    }
    return RC::SUCCESS;
  }

  if (expr->type() == ExprType::COMPARISON) {
    std::string tname = find_single_referenced_table(expr.get(), allowed);
    if (!tname.empty()) {
      pushed_by_table[tname].emplace_back(std::move(expr));
      change_made = true;
    }
    // 跨表 / 引用了子树外的表 / 常量 —— 保留在原位置
    return RC::SUCCESS;
  }

  // 其它表达式类型（算术、Cast 等）—— 不动
  return RC::SUCCESS;
}

RC PredicatePushdownRewriter::get_exprs_can_pushdown(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &pushdown_exprs)
{
  RC rc = RC::SUCCESS;
  if (expr->type() == ExprType::CONJUNCTION) {
    ConjunctionExpr *conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
    // 或 操作的比较，太复杂，现在不考虑
    if (conjunction_expr->conjunction_type() == ConjunctionExpr::Type::OR_EXPR) {
      LOG_WARN("unsupported or operation");
      rc = RC::UNIMPLEMENTED;
      return rc;
    }

    vector<unique_ptr<Expression>> &child_exprs = conjunction_expr->children();
    for (auto iter = child_exprs.begin(); iter != child_exprs.end();) {
      // 对每个子表达式，判断是否可以下放到table get 算子
      // 如果可以的话，就从当前孩子节点中删除他
      rc = get_exprs_can_pushdown(*iter, pushdown_exprs);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to get pushdown expressions. rc=%s", strrc(rc));
        return rc;
      }

      if (!*iter) {
        iter = child_exprs.erase(iter);
      } else {
        ++iter;
      }
    }
  } else if (expr->type() == ExprType::COMPARISON) {
    // 如果是比较操作，并且比较的左边或右边是表某个列值，那么就下推下去

    pushdown_exprs.emplace_back(std::move(expr));
  }
  return rc;
}

bool PredicatePushdownRewriter::is_empty_predicate(unique_ptr<Expression> &expr)
{
  if (!expr) {
    return true;
  }

  if (expr->type() == ExprType::CONJUNCTION) {
    ConjunctionExpr *conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
    if (conjunction_expr->children().empty()) {
      return true;
    }
  }

  return false;
}