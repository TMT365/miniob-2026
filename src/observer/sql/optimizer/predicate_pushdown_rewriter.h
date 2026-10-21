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

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/lang/string.h"
#include "sql/optimizer/rewrite_rule.h"

class JoinLogicalOperator;
class TableGetLogicalOperator;

/**
 * @brief 将一些谓词表达式下推到表数据扫描中
 * @ingroup Rewriter
 * @details 这样可以提前过滤一些数据。
 * 除了最常见的 PredicateLogicalOperator → TableGetLogicalOperator 的下推，
 * 本 rewriter 还会：
 *   - 穿过 JOIN，把 WHERE 中只引用一侧表的谓词推到对应 TableGet
 *   - 把 JoinLogicalOperator::on_predicate_ 中只引用一侧表的谓词推到对应 TableGet
 * 跨表谓词仍然保留在原位置。
 * 注意：本实现假设所有 join 都是 INNER_JOIN/COMMA_CARTESIAN；
 * 一旦加入 OUTER JOIN，需要重新审视单边下推的正确性（外连接保留侧不可过滤）。
 */
class PredicatePushdownRewriter : public RewriteRule
{
public:
  PredicatePushdownRewriter()          = default;
  virtual ~PredicatePushdownRewriter() = default;

  RC rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made) override;

private:
  RC   get_exprs_can_pushdown(unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &pushdown_exprs);
  bool is_empty_predicate(unique_ptr<Expression> &expr);

  /**
   * @brief 把挂在 JOIN 之上的 PredicateLogicalOperator 中的谓词按"只引用左/只引用右/跨表"分类，
   *        单边谓词推到对应 spine 的 TableGet 上，跨表部分保留回 expressions_ 中。
   *        若表达式全部被推掉，对应位置用 ValueExpr(true) 占位（与原 TABLE_GET 分支保持一致）。
   */
  RC pushdown_predicates_into_join(LogicalOperator               &join_oper,
                                   vector<unique_ptr<Expression>> &predicate_exprs,
                                   bool                          &change_made);

  /**
   * @brief 处理 JoinLogicalOperator::on_predicate_ 的单边谓词下推。
   *        若 ON 谓词整体被推空，把 on_predicate_ 置空。
   */
  RC pushdown_on_conditions(JoinLogicalOperator &join_oper, bool &change_made);

  /**
   * @brief 递归地把 AND-ConjunctionExpr 中所有 ComparisonExpr 叶子按"只引用 allowed 中一张表"分类。
   *        单边 ComparisonExpr 被 move 到 pushed_by_table[table_name]；跨表 / 引用了不在 allowed
   *        中的表 / 常量节点仍留在 expr 中（ConjunctionExpr 中被移走的子节点会被 erase）。
   *        若 expr 是 OR-ConjunctionExpr，返回 RC::UNIMPLEMENTED（调用方应保持原状）。
   */
  static RC pushdown_single_side_into_subtree_tables(
      LogicalOperator               &join_subtree_root,
      vector<unique_ptr<Expression>> &expressions,
      bool                          &change_made);

  static RC split_predicate_by_table(
      unique_ptr<Expression>                                                  &expr,
      const std::unordered_map<std::string, TableGetLogicalOperator *>        &tables,
      const std::unordered_set<std::string>                                    &allowed,
      std::unordered_map<std::string, std::vector<unique_ptr<Expression>>>     &pushed_by_table,
      bool                                                                    &change_made);
};
