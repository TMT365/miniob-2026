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

#include "common/log/log.h"
#include "common/lang/string.h"
#include "common/lang/ranges.h"
#include "expression_binder.h"
#include "sql/expr/expression_iterator.h"

using namespace common;

Table *BinderContext::find_table(const char *table_name) const
{
  /* C++ lambda 表达式 [table_name](Table *table) mutable -> 返回类型 { return 0 == strcasecmp(table_name, table->name()); };
   * strcasecmp() 函数用于比较两个字符串，忽略大小写。返回值为0表示两个字符串相等。
   */
  auto pred = [table_name](Table *table) { return 0 == strcasecmp(table_name, table->name()); };

  //在一个范围 (range / 容器)里，找到第一个满足断言（predicate）的元素，返回对应的迭代器。ranges::borrowed_iterator_t<R>，对其解引用就是容器对象
  auto iter = ranges::find_if(query_tables_, pred);
  if (iter == query_tables_.end()) {
    return nullptr;
  }
  // 解引用，获取Table *
  return *iter;
}

/**
 * @brief 绑定通配符字段表达式
 * @details 绑定通配符字段表达式，就是将通配符(*)解析为具体的字段表达式
 * @param table Table* 表对象，表示要绑定的表
 * @param expressions vector<unique_ptr<Expression>>& 表达式集合，表示绑定后的表达式集合
 */
static void wildcard_fields(Table *table, vector<unique_ptr<Expression>> &expressions)
{
  const TableMeta &table_meta = table->table_meta();
  const int        field_num  = table_meta.field_num();
  // 循环：从【系统字段数量】开始遍历，跳过隐藏的系统内置列，只取用户可见字段
  for (int i = table_meta.sys_field_num(); i < field_num; i++) {
    Field      field(table, table_meta.field(i));
    FieldExpr *field_expr = new FieldExpr(field);
    field_expr->set_name(field.field_name());
    expressions.emplace_back(field_expr);
  }
}

RC ExpressionBinder::bind_expression(unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  switch (expr->type()) {
    case ExprType::STAR: {
      return bind_star_expression(expr, bound_expressions);
    }

    case ExprType::UNBOUND_FIELD: {
      return bind_unbound_field_expression(expr, bound_expressions);
    }

    case ExprType::UNBOUND_AGGREGATION: {
      return bind_aggregate_expression(expr, bound_expressions);
    }

    case ExprType::FIELD: {
      return bind_field_expression(expr, bound_expressions);
    }

    case ExprType::VALUE: {
      return bind_value_expression(expr, bound_expressions);
    }

    case ExprType::CAST: {
      return bind_cast_expression(expr, bound_expressions);
    }

    case ExprType::COMPARISON: {
      return bind_comparison_expression(expr, bound_expressions);
    }

    case ExprType::CONJUNCTION: {
      return bind_conjunction_expression(expr, bound_expressions);
    }

    case ExprType::ARITHMETIC: {
      return bind_arithmetic_expression(expr, bound_expressions);
    }

    case ExprType::AGGREGATION: {
      ASSERT(false, "shouldn't be here");
    }

    default: {
      LOG_WARN("unknown expression type: %d", static_cast<int>(expr->type()));
      return RC::INTERNAL;
    }
  }
}

RC ExpressionBinder::bind_star_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto star_expr = static_cast<StarExpr *>(expr.get()); /// 将表达式转换为StarExpr类型的指针

  vector<Table *> tables_to_wildcard; /// 存储需要绑定通配符(*)的表对象

  const char *table_name = star_expr->table_name();
  // 满足条件：table_name不为空，并且table_name不等于"*"
  if (!is_blank(table_name) && 0 != strcmp(table_name, "*")) {
    Table *table = context_.find_table(table_name);
    if (nullptr == table) {
      LOG_INFO("no such table in from list: %s", table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    tables_to_wildcard.push_back(table);
  } else {
    const vector<Table *> &all_tables = context_.query_tables();
    tables_to_wildcard.insert(tables_to_wildcard.end(), all_tables.begin(), all_tables.end());
  }

  // 遍历需要绑定通配符(*)的表对象，将通配符(*)解析为具体的字段表达式，从原来的 Unbound 转换为 FieldExpr，并将其添加到 bound_expressions 中
  for (Table *table : tables_to_wildcard) {
    wildcard_fields(table, bound_expressions);
  }

  return RC::SUCCESS;
}

RC ExpressionBinder::bind_unbound_field_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto unbound_field_expr = static_cast<UnboundFieldExpr *>(expr.get()); /// 将表达式转换为UnboundFieldExpr类型的指针

  const char *table_name = unbound_field_expr->table_name().c_str();
  const char *field_name = unbound_field_expr->field_name().c_str();

  Table *table = nullptr;
  if (is_blank(table_name)) {
    // 如果没有指定表名，那么就只能在查询的表中查找字段了，如果查询的表不止一个，那么就无法确定字段属于哪张表，就会报错
    if (context_.query_tables().size() != 1) {
      LOG_INFO("cannot determine table for field: %s", field_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    table = context_.query_tables()[0];
  } else {
    table = context_.find_table(table_name);
    if (nullptr == table) {
      LOG_INFO("no such table in from list: %s", table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
  }
  // 如果字段名是通配符(*)，那么就将表中的所有字段都绑定为具体的字段表达式
  // TODO 注意，strcmp(A,B)的返回值是 0 表示 A 和 B 相等，< 0 表示 A 小于 B，> 0 表示 A 大于 B
  if (0 == strcmp(field_name, "*")) {
    wildcard_fields(table, bound_expressions);
  } else {
    const FieldMeta *field_meta = table->table_meta().field(field_name);
    if (nullptr == field_meta) {
      LOG_INFO("no such field in table: %s.%s", table_name, field_name);
      return RC::SCHEMA_FIELD_MISSING;
    }

    Field      field(table, field_meta);
    FieldExpr *field_expr = new FieldExpr(field);
    field_expr->set_name(field_name);
    bound_expressions.emplace_back(field_expr);
  }

  return RC::SUCCESS;
}

RC ExpressionBinder::bind_field_expression(
    unique_ptr<Expression> &field_expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  bound_expressions.emplace_back(std::move(field_expr));
  return RC::SUCCESS;
}

RC ExpressionBinder::bind_value_expression(
    unique_ptr<Expression> &value_expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  bound_expressions.emplace_back(std::move(value_expr));
  return RC::SUCCESS;
}

RC ExpressionBinder::bind_cast_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto cast_expr = static_cast<CastExpr *>(expr.get());

  vector<unique_ptr<Expression>> child_bound_expressions;
  unique_ptr<Expression>        &child_expr = cast_expr->child();

  RC rc = bind_expression(child_expr, child_bound_expressions);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  if (child_bound_expressions.size() != 1) {
    LOG_WARN("invalid children number of cast expression: %d", child_bound_expressions.size());
    return RC::INVALID_ARGUMENT;
  }
  // 递归绑定子表达式，如果子表达式已经被绑定过了，那么就直接使用绑定后的子表达式，否则就将绑定后的子表达式替换原来的子表达式
  unique_ptr<Expression> &child = child_bound_expressions[0];
  // 判断 child 和 child_expr 是否指向同一个对象，如果是，那么就说明子表达式已经被绑定过了，直接返回成功
  if (child.get() == child_expr.get()) {
    return RC::SUCCESS;
  }
  // 如果 child 和 child_expr 不指向同一个对象，那么就说明子表达式还没有被绑定过，那么就将绑定后的子表达式替换原来的子表达式
  child_expr.reset(child.release());
  bound_expressions.emplace_back(std::move(expr));
  return RC::SUCCESS;
}

RC ExpressionBinder::bind_comparison_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto comparison_expr = static_cast<ComparisonExpr *>(expr.get());

  vector<unique_ptr<Expression>> child_bound_expressions;
  unique_ptr<Expression>        &left_expr  = comparison_expr->left();
  unique_ptr<Expression>        &right_expr = comparison_expr->right();

  RC rc = bind_expression(left_expr, child_bound_expressions);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  if (child_bound_expressions.size() != 1) {
    LOG_WARN("invalid left children number of comparison expression: %d", child_bound_expressions.size());
    return RC::INVALID_ARGUMENT;
  }

  unique_ptr<Expression> &left = child_bound_expressions[0];
  if (left.get() != left_expr.get()) {
    left_expr.reset(left.release());
  }

  child_bound_expressions.clear();
  rc = bind_expression(right_expr, child_bound_expressions);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  if (child_bound_expressions.size() != 1) {
    LOG_WARN("invalid right children number of comparison expression: %d", child_bound_expressions.size());
    return RC::INVALID_ARGUMENT;
  }

  unique_ptr<Expression> &right = child_bound_expressions[0];
  if (right.get() != right_expr.get()) {
    right_expr.reset(right.release());
  }

  bound_expressions.emplace_back(std::move(expr));
  return RC::SUCCESS;
}

RC ExpressionBinder::bind_conjunction_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());

  vector<unique_ptr<Expression>>  child_bound_expressions;
  vector<unique_ptr<Expression>> &children = conjunction_expr->children();

  for (unique_ptr<Expression> &child_expr : children) {
    child_bound_expressions.clear();

    RC rc = bind_expression(child_expr, child_bound_expressions);
    if (rc != RC::SUCCESS) {
      return rc;
    }

    if (child_bound_expressions.size() != 1) {
      LOG_WARN("invalid children number of conjunction expression: %d", child_bound_expressions.size());
      return RC::INVALID_ARGUMENT;
    }

    unique_ptr<Expression> &child = child_bound_expressions[0];
    if (child.get() != child_expr.get()) {
      child_expr.reset(child.release());
    }
  }

  bound_expressions.emplace_back(std::move(expr));

  return RC::SUCCESS;
}

RC ExpressionBinder::bind_arithmetic_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto arithmetic_expr = static_cast<ArithmeticExpr *>(expr.get());

  vector<unique_ptr<Expression>> child_bound_expressions;
  unique_ptr<Expression>        &left_expr  = arithmetic_expr->left();
  unique_ptr<Expression>        &right_expr = arithmetic_expr->right();

  RC rc = bind_expression(left_expr, child_bound_expressions);
  if (OB_FAIL(rc)) {
    return rc;
  }

  if (child_bound_expressions.size() != 1) {
    LOG_WARN("invalid left children number of comparison expression: %d", child_bound_expressions.size());
    return RC::INVALID_ARGUMENT;
  }

  unique_ptr<Expression> &left = child_bound_expressions[0];
  if (left.get() != left_expr.get()) {
    left_expr.reset(left.release());
  }

  child_bound_expressions.clear();
  if (right_expr != nullptr) {
    rc = bind_expression(right_expr, child_bound_expressions);
    if (OB_FAIL(rc)) {
      return rc;
    }

    if (child_bound_expressions.size() != 1) {
      LOG_WARN("invalid right children number of comparison expression: %d", child_bound_expressions.size());
      return RC::INVALID_ARGUMENT;
    }

    unique_ptr<Expression> &right = child_bound_expressions[0];
    if (right.get() != right_expr.get()) {
      right_expr.reset(right.release());
    }
  }

  bound_expressions.emplace_back(std::move(expr));
  return RC::SUCCESS;
}

RC check_aggregate_expression(AggregateExpr &expression)
{
  // 必须有一个子表达式
  Expression *child_expression = expression.child().get();
  if (nullptr == child_expression) {
    LOG_WARN("child expression of aggregate expression is null");
    return RC::INVALID_ARGUMENT;
  }

  // 校验数据类型与聚合类型是否匹配
  AggregateExpr::Type aggregate_type   = expression.aggregate_type();
  AttrType            child_value_type = child_expression->value_type();
  switch (aggregate_type) {
    case AggregateExpr::Type::SUM:
    case AggregateExpr::Type::AVG: {
      // 仅支持数值类型
      if (!is_numerical_type(child_value_type)) {
        LOG_WARN("invalid child value type for aggregate expression: %d", static_cast<int>(child_value_type));
        return RC::INVALID_ARGUMENT;
      }
    } break;

    case AggregateExpr::Type::COUNT:
    case AggregateExpr::Type::MAX:
    case AggregateExpr::Type::MIN: {
      // COUNT、MAX、MIN 支持任意类型，运行时由聚合器处理 NULL。
    } break;
  }

  // 子表达式中不能再包含聚合表达式 TODO 后续可以改进使聚合函数里面可以有聚合函数
  function<RC(unique_ptr<Expression>&)> check_aggregate_expr = [&](unique_ptr<Expression> &expr) -> RC {
    if (expr->type() == ExprType::AGGREGATION) {
      LOG_WARN("aggregate expression cannot be nested");
      return RC::INVALID_ARGUMENT;
    }
    RC rc = ExpressionIterator::iterate_child_expr(*expr, check_aggregate_expr);
    return rc;
  };

  RC rc = ExpressionIterator::iterate_child_expr(expression, check_aggregate_expr);

  return rc;
}

RC ExpressionBinder::bind_aggregate_expression(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &bound_expressions)
{
  if (nullptr == expr) {
    return RC::SUCCESS;
  }

  auto unbound_aggregate_expr = static_cast<UnboundAggregateExpr *>(expr.get());
  const char *aggregate_name = unbound_aggregate_expr->aggregate_name();
  AggregateExpr::Type aggregate_type;
  RC rc = AggregateExpr::type_from_string(aggregate_name, aggregate_type);
  if (OB_FAIL(rc)) {
    LOG_WARN("invalid aggregate name: %s", aggregate_name);
    return rc;
  }

  unique_ptr<Expression>        &child_expr = unbound_aggregate_expr->child();
  vector<unique_ptr<Expression>> child_bound_expressions;

  if (child_expr->type() == ExprType::STAR && aggregate_type == AggregateExpr::Type::COUNT) {
    ValueExpr *value_expr = new ValueExpr(Value(1));
    child_expr.reset(value_expr);
  } else {
    /* NOTE 如果不是 COUNT(*)，那么就需要绑定子表达式，子表达式可能是字段表达式、算术表达式、常量表达式等 */
    rc = bind_expression(child_expr, child_bound_expressions);
    if (OB_FAIL(rc)) {
      return rc;
    }

    if (child_bound_expressions.size() != 1) {
      LOG_WARN("invalid children number of aggregate expression: %d", child_bound_expressions.size());
      return RC::INVALID_ARGUMENT;
    }

    if (child_bound_expressions[0].get() != child_expr.get()) {
      child_expr.reset(child_bound_expressions[0].release());
    }
  }

  auto aggregate_expr = make_unique<AggregateExpr>(aggregate_type, std::move(child_expr));
  /* NOTE 这个是基类Expression的成员变量name_，用于输出的表头，在没有实现AS的情况下就是alias */
  aggregate_expr->set_name(unbound_aggregate_expr->name());
  rc = check_aggregate_expression(*aggregate_expr);
  if (OB_FAIL(rc)) {
    return rc;
  }
  /* NOTE 每个子类的表达式都有放到vector<基类指针>里面 , 这是因为查询表达式是肯定是AND扁平市的，如果是where,join
   * on则需要后期处理*/
  bound_expressions.emplace_back(std::move(aggregate_expr));
  return RC::SUCCESS;
}
