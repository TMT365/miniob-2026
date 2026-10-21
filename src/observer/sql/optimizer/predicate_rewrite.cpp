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
// Created by Wangyunlai on 2022/12/29.
//

#include "sql/optimizer/predicate_rewrite.h"
#include "sql/operator/logical_operator.h"

RC PredicateRewriteRule::rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = oper->children();
  if (child_opers.size() != 1) {
    return RC::SUCCESS;
  }

  auto &child_oper = child_opers.front();
  if (child_oper->type() != LogicalOperatorType::PREDICATE) {
    return RC::SUCCESS;
  }

  vector<unique_ptr<Expression>> &expressions = child_oper->expressions();
  if (expressions.size() != 1) {
    return RC::SUCCESS;
  }

  unique_ptr<Expression> &expr = expressions.front();
  if (expr->type() != ExprType::VALUE) {
    return RC::SUCCESS;
  }

  // 如果仅有的一个子节点是predicate
  // 并且这个子节点可以判断为恒为TRUE，那么可以省略这个子节点，并把他的子节点们（就是孙子节点）接管过来
  // 如果可以判断恒为false，谓词算子要保留：它求值出来是 false，结果就是空集
  auto value_expr = static_cast<ValueExpr *>(expr.get());
  bool bool_value = value_expr->get_value().get_boolean();
  if (true == bool_value) {
    vector<unique_ptr<LogicalOperator>> grand_child_opers;
    grand_child_opers.swap(child_oper->children());
    child_opers.clear();
    for (auto &grand_child_oper : grand_child_opers) {
      oper->add_child(std::move(grand_child_oper));
    }

    change_made = true;
  }
  // 恒为 false 的情况这里什么都不做。
  // 不能把谓词算子删掉：删掉之后谓词就失效了，数据反而会全部放行；
  // 而且父节点会因为没有子节点而生成不出物理计划(比如 group by 要求恰好一个子节点)。
  return RC::SUCCESS;
}
