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
// Created by Wangyunlai on 2022/07/05.
//

#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"
#include "sql/expr/arithmetic_operator.hpp"

#include <cmath>

using namespace std;

RC FieldExpr::get_value(const Tuple &tuple, Value &value) const
{
  return tuple.find_cell(TupleCellSpec(table_name(), field_name()), value);
}

bool FieldExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (other.type() != ExprType::FIELD) {
    return false;
  }
  const auto &other_field_expr = static_cast<const FieldExpr &>(other);
  return table_name() == other_field_expr.table_name() && field_name() == other_field_expr.field_name();
}

// TODO: 在进行表达式计算时，`chunk` 包含了所有列，因此可以通过 `field_id` 获取到对应列。
// 后续可以优化成在 `FieldExpr` 中存储 `chunk` 中某列的位置信息。
RC FieldExpr::get_column(Chunk &chunk, Column &column)
{
  if (pos_ != -1) {
    column.reference(chunk.column(pos_));
  } else {
    column.reference(chunk.column(field().meta()->field_id()));
  }
  return RC::SUCCESS;
}

bool ValueExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (other.type() != ExprType::VALUE) {
    return false;
  }
  const auto &other_value_expr = static_cast<const ValueExpr &>(other);
  return value_.compare(other_value_expr.get_value()) == 0;
}

RC ValueExpr::get_value(const Tuple &tuple, Value &value) const
{
  value = value_;
  return RC::SUCCESS;
}

RC ValueExpr::get_column(Chunk &chunk, Column &column)
{
  column.init(value_, chunk.rows());
  return RC::SUCCESS;
}

/////////////////////////////////////////////////////////////////////////////////
CastExpr::CastExpr(unique_ptr<Expression> child, AttrType cast_type) : child_(std::move(child)), cast_type_(cast_type)
{}

CastExpr::~CastExpr() {}

RC CastExpr::cast(const Value &value, Value &cast_value) const
{
  RC rc = RC::SUCCESS;
  if (this->value_type() == value.attr_type()) {
    cast_value = value;
    return rc;
  }
  rc = Value::cast_to(value, cast_type_, cast_value);
  return rc;
}

RC CastExpr::get_value(const Tuple &tuple, Value &result) const
{
  Value value;
  RC rc = child_->get_value(tuple, value);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  return cast(value, result);
}

RC CastExpr::get_column(Chunk &chunk, Column &column)
{
  Column child_column;
  RC rc = child_->get_column(chunk, child_column);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  column.init(cast_type_, child_column.attr_len());
  for (int i = 0; i < child_column.count(); ++i) {
    Value value = child_column.get_value(i);
    Value cast_value;
    rc = cast(value, cast_value);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    column.append_value(cast_value);
  }
  return rc;
}

RC CastExpr::try_get_value(Value &result) const
{
  Value value;
  RC rc = child_->try_get_value(value);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  return cast(value, result);
}

////////////////////////////////////////////////////////////////////////////////

ComparisonExpr::ComparisonExpr(CompOp comp, unique_ptr<Expression> left, unique_ptr<Expression> right)
    : comp_(comp), left_(std::move(left)), right_(std::move(right))
{
}

ComparisonExpr::~ComparisonExpr() {}

RC ComparisonExpr::compare_value(const Value &left, const Value &right, bool &result) const
{
  RC  rc         = RC::SUCCESS;
  // NULL 与任何值比较，结果都是 UNKNOWN（视为 false）
  if (left.is_null() || right.is_null()) {
    result = false;
    return RC::SUCCESS;
  }
  int cmp_result = left.compare(right);
  result         = false;
  switch (comp_) {
    case EQUAL_TO: {
      result = (0 == cmp_result);
    } break;
    case LESS_EQUAL: {
      result = (cmp_result <= 0);
    } break;
    case NOT_EQUAL: {
      result = (cmp_result != 0);
    } break;
    case LESS_THAN: {
      result = (cmp_result < 0);
    } break;
    case GREAT_EQUAL: {
      result = (cmp_result >= 0);
    } break;
    case GREAT_THAN: {
      result = (cmp_result > 0);
    } break;
    default: {
      LOG_WARN("unsupported comparison. %d", comp_);
      rc = RC::INTERNAL;
    } break;
  }

  return rc;
}

RC ComparisonExpr::try_get_value(Value &cell) const
{
  Value left_value;
  Value right_value;

  RC rc = left_->try_get_value(left_value);
  if (OB_FAIL(rc)) {
    return rc;
  }

  rc = right_->try_get_value(right_value);
  if (OB_FAIL(rc)) {
    return rc;
  }

  bool value = false;
  rc = compare_value(left_value, right_value, value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to compare tuple cells. rc=%s", strrc(rc));
  } else {
    cell.set_boolean(value);
  }
  return rc;
}

RC ComparisonExpr::get_value(const Tuple &tuple, Value &value) const
{
  Value left_value;
  Value right_value;

  RC rc = left_->get_value(tuple, left_value);  // 遇到 ValueExpr(AttrType::NULL_VALS) 会返回SUCCESS
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }
  rc = right_->get_value(tuple, right_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
    return rc;
  }

  bool bool_value = false;

  rc = compare_value(left_value, right_value, bool_value);
  if (rc == RC::SUCCESS) {
    value.set_boolean(bool_value);
  }
  return rc;
}


RC ComparisonExpr::eval(Chunk &chunk, vector<uint8_t> &select)
{
  RC     rc = RC::SUCCESS;
  Column left_column;
  Column right_column;

  rc = left_->get_column(chunk, left_column);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }
  rc = right_->get_column(chunk, right_column);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
    return rc;
  }
  if (left_column.attr_type() != right_column.attr_type()) {
    LOG_WARN("cannot compare columns with different types");
    return RC::INTERNAL;
  }
  if (left_column.attr_type() == AttrType::INTS) {
    rc = compare_column<int>(left_column, right_column, select);
  } else if (left_column.attr_type() == AttrType::FLOATS) {
    rc = compare_column<float>(left_column, right_column, select);
  } else if (left_column.attr_type() == AttrType::CHARS) {
    int rows = 0;
    if (left_column.column_type() == Column::Type::CONSTANT_COLUMN) {
      rows = right_column.count();
    } else {
      rows = left_column.count();
    }
    for (int i = 0; i < rows; ++i) {
      Value left_val = left_column.get_value(i);
      Value right_val = right_column.get_value(i);
      bool        result   = false;
      rc                   = compare_value(left_val, right_val, result);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to compare tuple cells. rc=%s", strrc(rc));
        return rc;
      }
      select[i] &= result ? 1 : 0;
    }

  } else {
    LOG_WARN("unsupported data type %d", left_column.attr_type());
    return RC::INTERNAL;
  }
  return rc;
}

template <typename T>
RC ComparisonExpr::compare_column(const Column &left, const Column &right, vector<uint8_t> &result) const
{
  RC rc = RC::SUCCESS;

  bool left_const  = left.column_type() == Column::Type::CONSTANT_COLUMN;
  bool right_const = right.column_type() == Column::Type::CONSTANT_COLUMN;
  if (left_const && right_const) {
    compare_result<T, true, true>((T *)left.data(), (T *)right.data(), left.count(), result, comp_);
  } else if (left_const && !right_const) {
    compare_result<T, true, false>((T *)left.data(), (T *)right.data(), right.count(), result, comp_);
  } else if (!left_const && right_const) {
    compare_result<T, false, true>((T *)left.data(), (T *)right.data(), left.count(), result, comp_);
  } else {
    compare_result<T, false, false>((T *)left.data(), (T *)right.data(), left.count(), result, comp_);
  }
  return rc;
}

////////////////////////////////////////////////////////////////////////////////
ConjunctionExpr::ConjunctionExpr(Type type, vector<unique_ptr<Expression>> &children)
    : conjunction_type_(type), children_(std::move(children))
{}

RC ConjunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  RC rc = RC::SUCCESS;
  if (children_.empty()) {
    value.set_boolean(true);
    return rc;
  }

  Value tmp_value;
  for (const unique_ptr<Expression> &expr : children_) {
    rc = expr->get_value(tuple, tmp_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get value by child expression. rc=%s", strrc(rc));
      return rc;
    }
    bool bool_value = tmp_value.get_boolean();
    if ((conjunction_type_ == Type::AND_EXPR && !bool_value) || (conjunction_type_ == Type::OR_EXPR && bool_value)) {
      value.set_boolean(bool_value);
      return rc;
    }
  }

  bool default_value = (conjunction_type_ == Type::AND_EXPR);
  value.set_boolean(default_value);
  return rc;
}

////////////////////////////////////////////////////////////////////////////////
// 全局的一元负号处理方式
Value ArithmeticExpr::zero_ = []() {
  Value v;
  v.set_int(0);
  return v;
}();

ArithmeticExpr::ArithmeticExpr(ArithmeticExpr::Type type, Expression *left, Expression *right)
    : arithmetic_type_(type), left_(left), right_(right)
{}
ArithmeticExpr::ArithmeticExpr(ArithmeticExpr::Type type, unique_ptr<Expression> left, unique_ptr<Expression> right)
    : arithmetic_type_(type), left_(std::move(left)), right_(std::move(right))
{}

bool ArithmeticExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (type() != other.type()) {
    return false;
  }
  auto &other_arith_expr = static_cast<const ArithmeticExpr &>(other);
  return arithmetic_type_ == other_arith_expr.arithmetic_type() && left_->equal(*other_arith_expr.left_) &&
         right_->equal(*other_arith_expr.right_);
}
AttrType ArithmeticExpr::value_type() const
{
  if (!right_) {
    return left_->value_type();
  }

  if ((left_->value_type() == AttrType::INTS) &&
   (right_->value_type() == AttrType::INTS) &&
      arithmetic_type_ != Type::DIV) {
    return AttrType::INTS;
  }

  return AttrType::FLOATS;
}

RC ArithmeticExpr::calc_value(const Value &left_value, const Value &right_value, Value &value) const
{
  RC rc = RC::SUCCESS;

  // 任一操作数为 NULL,结果必为 NULL
  if (left_value.is_null() || right_value.is_null()) {
    value.set_null();
    return RC::SUCCESS;
  }

  const AttrType target_type = value_type();
  value.set_type(target_type);

  switch (arithmetic_type_) {
    case Type::ADD: {
      rc = Value::add(left_value, right_value, value);
    } break;

    case Type::SUB: {
      rc = Value::subtract(left_value, right_value, value);
    } break;

    case Type::MUL: {
      rc = Value::multiply(left_value, right_value, value);
    } break;

    case Type::DIV: {
      rc = Value::divide(left_value, right_value, value);
    } break;

    case Type::NEGATIVE: {
      rc = Value::negative(left_value, value);
    } break;

    default: {
      rc = RC::INTERNAL;
      LOG_WARN("unsupported arithmetic type. %d", arithmetic_type_);
    } break;
  }
  return rc;
}

template <bool LEFT_CONSTANT, bool RIGHT_CONSTANT>
RC ArithmeticExpr::execute_calc(
    const Column &left, const Column &right, Column &result, Type type, AttrType attr_type) const
{
  RC rc = RC::SUCCESS;
  switch (type) {
    case Type::ADD: {
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, AddOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, AddOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
    } break;
    case Type::SUB:
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, SubtractOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, SubtractOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    case Type::MUL:
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, MultiplyOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, MultiplyOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    case Type::DIV:
      if (attr_type == AttrType::INTS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, int, DivideOperator>(
            (int *)left.data(), (int *)right.data(), (int *)result.data(), result.capacity());
        for (int i = 0; i < result.capacity(); i++) {
          if (DivideOperator::is_sentinel(((int *)result.data())[i])) {
            result.set_null(i);
          }
        }
      } else if (attr_type == AttrType::FLOATS) {
        binary_operator<LEFT_CONSTANT, RIGHT_CONSTANT, float, DivideOperator>(
            (float *)left.data(), (float *)right.data(), (float *)result.data(), result.capacity());
        for (int i = 0; i < result.capacity(); i++) {
          if (DivideOperator::is_sentinel(((float *)result.data())[i])) {
            result.set_null(i);
          }
        }
      }
      break;

    case Type::NEGATIVE:
      if (attr_type == AttrType::INTS) {
        unary_operator<LEFT_CONSTANT, int, NegateOperator>((int *)left.data(), (int *)result.data(), result.capacity());
      } else if (attr_type == AttrType::FLOATS) {
        unary_operator<LEFT_CONSTANT, float, NegateOperator>(
            (float *)left.data(), (float *)result.data(), result.capacity());
      } else {
        rc = RC::UNIMPLEMENTED;
      }
      break;
    default: rc = RC::UNIMPLEMENTED; break;
  }
  if (rc == RC::SUCCESS) {
    result.set_count(result.capacity());
  }
  return rc;
}

RC ArithmeticExpr::get_value(const Tuple &tuple, Value &value) const
{
  RC rc = RC::SUCCESS;

  Value left_value;
  Value right_value;

  rc = left_->get_value(tuple, left_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }
  // 如果是 Type::NEGATIVE 是没有右表达式的
  if (arithmetic_type_ == Type::NEGATIVE || right_ == nullptr) {
    // 构造类型是 Value 值是0 的常量返回
    return calc_value(left_value, zero_, value);
  }
  rc = right_->get_value(tuple, right_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
    return rc;
  }
  return calc_value(left_value, right_value, value);
}

RC ArithmeticExpr::get_column(Chunk &chunk, Column &column)
{
  RC rc = RC::SUCCESS;
  if (pos_ != -1) {
    column.reference(chunk.column(pos_));
    return rc;
  }
  Column left_column;
  Column right_column;

  rc = left_->get_column(chunk, left_column);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get column of left expression. rc=%s", strrc(rc));
    return rc;
  }
  // 处理 NEGATIVE 类型（一元负号），right_ 为 nullptr
  if (arithmetic_type_ == Type::NEGATIVE || right_ == nullptr) {
    right_column.init(zero_, chunk.rows());
  } else {
    rc = right_->get_column(chunk, right_column);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get column of right expression. rc=%s", strrc(rc));
      return rc;
    }
  }
  return calc_column(left_column, right_column, column);
}

RC ArithmeticExpr::calc_column(const Column &left_column, const Column &right_column, Column &column) const
{
  RC rc = RC::SUCCESS;

  const AttrType target_type = value_type();
  column.init(target_type, left_column.attr_len(), max(left_column.count(), right_column.count()));
  bool left_const  = left_column.column_type() == Column::Type::CONSTANT_COLUMN;
  bool right_const = right_column.column_type() == Column::Type::CONSTANT_COLUMN;
  if (left_const && right_const) {
    column.set_column_type(Column::Type::CONSTANT_COLUMN);
    rc = execute_calc<true, true>(left_column, right_column, column, arithmetic_type_, target_type);
  } else if (left_const && !right_const) {
    column.set_column_type(Column::Type::NORMAL_COLUMN);
    rc = execute_calc<true, false>(left_column, right_column, column, arithmetic_type_, target_type);
  } else if (!left_const && right_const) {
    column.set_column_type(Column::Type::NORMAL_COLUMN);
    rc = execute_calc<false, true>(left_column, right_column, column, arithmetic_type_, target_type);
  } else {
    column.set_column_type(Column::Type::NORMAL_COLUMN);
    rc = execute_calc<false, false>(left_column, right_column, column, arithmetic_type_, target_type);
  }
  return rc;
}

RC ArithmeticExpr::try_get_value(Value &value) const
{
  RC rc = RC::SUCCESS;

  Value left_value;
  Value right_value;

  rc = left_->try_get_value(left_value);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get value of left expression. rc=%s", strrc(rc));
    return rc;
  }

  if (right_) {
    rc = right_->try_get_value(right_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get value of right expression. rc=%s", strrc(rc));
      return rc;
    }
  }

  return calc_value(left_value, right_value, value);
}

////////////////////////////////////////////////////////////////////////////////

UnboundAggregateExpr::UnboundAggregateExpr(const char *aggregate_name, Expression *child)
    : aggregate_name_(aggregate_name), child_(child)
{}

UnboundAggregateExpr::UnboundAggregateExpr(const char *aggregate_name, unique_ptr<Expression> child)
    : aggregate_name_(aggregate_name), child_(std::move(child))
{}

////////////////////////////////////////////////////////////////////////////////
AggregateExpr::AggregateExpr(Type type, Expression *child) : aggregate_type_(type), child_(child) {}

AggregateExpr::AggregateExpr(Type type, unique_ptr<Expression> child) : aggregate_type_(type), child_(std::move(child))
{}

RC AggregateExpr::get_column(Chunk &chunk, Column &column)
{
  RC rc = RC::SUCCESS;
  if (pos_ != -1) {
    column.reference(chunk.column(pos_));
  } else {
    rc = RC::INTERNAL;
  }
  return rc;
}

bool AggregateExpr::equal(const Expression &other) const
{
  if (this == &other) {
    return true;
  }
  if (other.type() != type()) {
    return false;
  }
  const AggregateExpr &other_aggr_expr = static_cast<const AggregateExpr &>(other);
  return aggregate_type_ == other_aggr_expr.aggregate_type() && child_->equal(*other_aggr_expr.child());
}
unique_ptr<Aggregator> AggregateExpr::create_aggregator() const
{
  unique_ptr<Aggregator> aggregator;
  switch (aggregate_type_) {

    case Type::COUNT: {
      aggregator = make_unique<CountAggregator>();
      break;
    }
    case Type::SUM: {
      aggregator = make_unique<SumAggregator>();
      break;
    }
    case Type::AVG: {
      aggregator = make_unique<AvgAggregator>();
      break;
    }
    case Type::MIN: {
      aggregator = make_unique<MinAggregator>();
      break;
    }
    case Type::MAX: {
      aggregator = make_unique<MaxAggregator>();
      break;
    }
    default: {
      ASSERT(false, "unsupported aggregate type");
      break;
    }
  }
  return aggregator;
}

RC AggregateExpr::get_value(const Tuple &tuple, Value &value) const
{
  return tuple.find_cell(TupleCellSpec(name()), value);
}

RC AggregateExpr::type_from_string(const char *type_str, AggregateExpr::Type &type)
{
  RC rc = RC::SUCCESS;
  if (0 == strcasecmp(type_str, "count")) {
    type = Type::COUNT;
  } else if (0 == strcasecmp(type_str, "sum")) {
    type = Type::SUM;
  } else if (0 == strcasecmp(type_str, "avg")) {
    type = Type::AVG;
  } else if (0 == strcasecmp(type_str, "max")) {
    type = Type::MAX;
  } else if (0 == strcasecmp(type_str, "min")) {
    type = Type::MIN;
  } else {
    rc = RC::INVALID_ARGUMENT;
  }
  return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
FunctionExpr::FunctionExpr(const char *function_name, vector<unique_ptr<Expression>> &args)
{
  function_name_ = function_name;
  args_.swap(args);
}

namespace {
// 使用匿名命名空间来限制 evaluate_function 的作用域，使其仅在当前文件中可见
RC evaluate_function(FunctionExpr::Type type, const vector<Value> &args, Value &value)
{
  for (const Value &arg : args) {
    // NULL 类型不支持这些函数的计算，直接返回 NULL
    if (arg.is_null()) {
      value.set_null();
      return RC::SUCCESS;
    }
  }

  switch (type) {
    case FunctionExpr::Type::LENGTH: {
      //miniob大赛要求只是char类型, 这里也可以使用断言
      if (args.size() != 1 || args[0].attr_type() != AttrType::CHARS) {
        return RC::INVALID_ARGUMENT;
      }
      value.set_int(args[0].length());
      return RC::SUCCESS;
    }
    // 比赛要求float
    case FunctionExpr::Type::ROUND: {
      if (args.size() != 1 || args[0].attr_type() != AttrType::FLOATS) {
        return RC::INVALID_ARGUMENT;
      }
      value.set_float(static_cast<float>(std::round(args[0].get_float())));
      return RC::SUCCESS;
    }
    // 这个是Date和char
    case FunctionExpr::Type::DATE_FORMAT: {
      if (args.size() != 2 || args[0].attr_type() != AttrType::DATES ||
          args[1].attr_type() != AttrType::CHARS) {
        return RC::INVALID_ARGUMENT;
      }
      static const char *month_names[] = {
          "", "January", "February", "March", "April", "May", "June",
          "July", "August", "September", "October", "November", "December"};

      // get_date()会进行隐式的类型转换，不合理会放回nullptr
      const Value::Val::DateValue *date = args[0].get_date();
      // 这里的检测主要是为了防止Char类型不合理，但是应该不存在这样情况，因为我在给Value添加Date类型的时候已经做了检测
      if (date == nullptr) {
        return RC::INVALID_ARGUMENT;
      }

      const int day = date->day_;
      string ordinal;
      if (day % 100 >= 11 && day % 100 <= 13) {
        ordinal = to_string(day) + "th";
      } else {
        switch (day % 10) {
          case 1: ordinal = to_string(day) + "st"; break;
          case 2: ordinal = to_string(day) + "nd"; break;
          case 3: ordinal = to_string(day) + "rd"; break;
          default: ordinal = to_string(day) + "th"; break;
        }
      }
      /**
       * %Y 4位数的年份 2026
       * %y 2位数的年份 26
       * %m 2位数的月份 01
       * %d 2位数的日期 01
       * %D 日期的序数后缀 1st, 2nd, 20th
       * %M 月份的英文名称 January
       * %z... 这些不合理的不进行替换原样输出
       */
      const string format = args[1].get_string();
      string result;
      for (size_t i = 0; i < format.size(); ++i) {
        // 只要不是%就直接输出
        if (format[i] != '%') {
          result.push_back(format[i]);
          continue;
        }
        if (++i >= format.size()) {
          return RC::INVALID_ARGUMENT;
        }

        switch (format[i]) {
          case 'D': result += ordinal; break;
          case 'M': result += month_names[date->month_]; break;
          case 'Y': result += date->get_four_year(); break;
          case 'y': result += date->get_two_year(); break;
          case 'm': result += date->get_two_month(); break;
          case 'd': result += date->get_two_day(); break;
          // 如果用户输入的不合理就原样输出，不报错
          default: result.push_back(format[i]);
        }
      }

      value.set_string(result.c_str(), static_cast<int>(result.size()));
      return RC::SUCCESS;
    }
  }

  return RC::INVALID_ARGUMENT;
}

}  // namespace

// 这个就是来获取值的，比如SELECT里面的LENGTH(field1), ROUND(field2), DATE_FORMAT(field3, '%D %M %Y')，这些函数的值就是通过这个函数来获取的
RC FunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  vector<Value> arg_values;
  arg_values.reserve(args_.size());
  for (const auto &arg : args_) {
    Value arg_value;
    RC rc = arg->get_value(tuple, arg_value);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to get value of argument expression. rc=%s", strrc(rc));
      return rc;
    }
    arg_values.emplace_back(std::move(arg_value)); // 这里存储的就是一个函数表达式里面的子表达式的值，ROUND(field2 * 3.45)
  }

  Type type;
  RC rc = type_from_string(function_name_.c_str(), type);
  if (OB_FAIL(rc)) {
    LOG_WARN("invalid function name: %s", function_name_.c_str());
    return rc;
  }
  return evaluate_function(type, arg_values, value);
}

//只有当函数的所有参数都能在没有 Tuple 的情况下求值时，函数才能提前计算
RC FunctionExpr::try_get_value(Value &value) const
{
  vector<Value> arg_values;
  arg_values.reserve(args_.size());

  for (const auto &arg : args_) {
    Value arg_value;
    RC rc = arg->try_get_value(arg_value);
    if (OB_FAIL(rc)) {
      return rc;
    }
    arg_values.emplace_back(std::move(arg_value));
  }

  Type type;
  RC rc = type_from_string(function_name_.c_str(), type);
  if (OB_FAIL(rc)) {
    LOG_WARN("invalid function name: %s", function_name_.c_str());
    return rc;
  }
  return evaluate_function(type, arg_values, value);
}

RC FunctionExpr::get_column(Chunk &chunk, Column &column)
{
  Type type;
  RC rc = type_from_string(function_name_.c_str(), type);
  if (OB_FAIL(rc)) {
    LOG_WARN("invalid function name: %s", function_name_.c_str());
    return rc;
  }

  vector<Column> arg_columns;
  arg_columns.reserve(args_.size());
  for (auto &arg : args_) {
    Column arg_column;
    rc = arg->get_column(chunk, arg_column);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to get function argument column. function=%s, rc=%s",
          function_name_.c_str(), strrc(rc));
      return rc;
    }
    arg_columns.emplace_back(std::move(arg_column));
  }

  const int row_count = chunk.rows();
  if (type == Type::DATE_FORMAT) {
    column.init_text(row_count);
  } else {
    column.init(value_type(), value_length(), row_count);
  }

  for (int row = 0; row < row_count; ++row) {
    vector<Value> arg_values;
    arg_values.reserve(arg_columns.size());
    for (const Column &arg_column : arg_columns) {
      const int arg_row = arg_column.column_type() == Column::Type::CONSTANT_COLUMN ? 0 : row;
      arg_values.emplace_back(arg_column.get_value(arg_row));
    }

    Value result;
    rc = evaluate_function(type, arg_values, result);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to evaluate function. function=%s, row=%d, rc=%s",
          function_name_.c_str(), row, strrc(rc));
      return rc;
    }

    rc = column.append_value(result);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to append function result. function=%s, row=%d, rc=%s",
          function_name_.c_str(), row, strrc(rc));
      return rc;
    }
  }

  return RC::SUCCESS;
}

AttrType FunctionExpr::value_type() const
{
  Type type;
  if (type_from_string(function_name_.c_str(), type) != RC::SUCCESS) {
    return AttrType::UNDEFINED;
  }

  switch (type) {
    case Type::LENGTH: return AttrType::INTS;
    case Type::ROUND: return AttrType::FLOATS;
    case Type::DATE_FORMAT: return AttrType::CHARS;
  }
  return AttrType::UNDEFINED;
}

int FunctionExpr::value_length() const
{
  switch (value_type()) {
    case AttrType::INTS: return sizeof(int);
    case AttrType::FLOATS: return sizeof(float);
    case AttrType::CHARS: return -1;
    default: return -1;
  }
}

RC FunctionExpr::type_from_string(const char *type_str, Type &type)
{
  RC rc = RC::SUCCESS;
  if (0 == strcasecmp(type_str, "length")) {
    type = Type::LENGTH;
  } else if (0 == strcasecmp(type_str, "round")) {
    type = Type::ROUND;
  } else if (0 == strcasecmp(type_str, "date_format")) {
    type = Type::DATE_FORMAT;
  } else {
    rc = RC::INVALID_ARGUMENT;
  }
  return rc;
}