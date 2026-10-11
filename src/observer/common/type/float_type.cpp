/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/lang/comparator.h"
#include "common/lang/sstream.h"
#include "common/log/log.h"
#include "common/type/float_type.h"
#include "common/value.h"
#include "common/lang/limits.h"
#include "common/value.h"
#include "storage/common/column.h"

#include <cmath>

int FloatType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::FLOATS, "left type is not float");
  ASSERT(right.attr_type() == AttrType::INTS || right.attr_type() == AttrType::FLOATS, "right type is not numeric");
  float left_val  = left.get_float();
  float right_val = right.get_float();
  return common::compare_float((void *)&left_val, (void *)&right_val);
}

int FloatType::compare(const Column &left, const Column &right, int left_idx, int right_idx) const
{
  ASSERT(left.attr_type() == AttrType::FLOATS, "left type is not float");
  ASSERT(right.attr_type() == AttrType::FLOATS, "right type is not float");
  return common::compare_float((void *)&((float*)left.data())[left_idx],
      (void *)&((float*)right.data())[right_idx]);
}

RC FloatType::add(const Value &left, const Value &right, Value &result) const
{
  result.set_float(left.get_float() + right.get_float());
  return RC::SUCCESS;
}
RC FloatType::subtract(const Value &left, const Value &right, Value &result) const
{
  result.set_float(left.get_float() - right.get_float());
  return RC::SUCCESS;
}
RC FloatType::multiply(const Value &left, const Value &right, Value &result) const
{
  result.set_float(left.get_float() * right.get_float());
  return RC::SUCCESS;
}

RC FloatType::divide(const Value &left, const Value &right, Value &result) const
{
  if (right.get_float() > -EPSILON && right.get_float() < EPSILON) {
    result.set_null();
  } else
    result.set_float(left.get_float() / right.get_float());
  return RC::SUCCESS;
}

RC FloatType::negative(const Value &val, Value &result) const
{
  result.set_float(-val.get_float());
  return RC::SUCCESS;
}

RC FloatType::set_value_from_str(Value &val, const string &data) const
{
  RC                rc = RC::SUCCESS;
  stringstream deserialize_stream;
  deserialize_stream.clear();
  deserialize_stream.str(data);

  float float_value;
  deserialize_stream >> float_value;
  if (!deserialize_stream || !deserialize_stream.eof()) {
    rc = RC::SCHEMA_FIELD_TYPE_MISMATCH;
  } else {
    val.set_float(float_value);
  }
  return rc;
}

RC FloatType::to_string(const Value &val, string &result) const
{
  stringstream ss;
  ss << common::double_to_str(val.value_.float_value_);
  result = ss.str();
  return RC::SUCCESS;
}

RC FloatType::cast_to(const Value &val, AttrType type, Value &result) const
{
    switch (type) {
      case AttrType::CHARS: {
        if (!my_transform_to_char(val, result)) {
          return RC::CANNOT_TO_SWITCH_TYPE;
        }
      } break;
      case AttrType::INTS: {
        if (!my_transform_to_int(val, result)) {
          return RC::CANNOT_TO_SWITCH_TYPE;
        }
      } break;
      case AttrType::DATES : {
        if (!my_transform_to_dataTime(val, result)) {
          return RC::CANNOT_TO_SWITCH_TYPE;
        }
      } break;
      default: {
        return RC::SCHEMA_FIELD_TYPE_MISMATCH;
      } break;
    }
    return RC::SUCCESS;
}

bool FloatType::my_transform_to_char(const Value &val, Value &result)
{
  float f = val.get_float();
  std::string s = std::to_string(f);
  if (s.empty()) {
    return false;
  }

  // std::to_string 默认输出 6 位小数，去掉多余的末尾零和小数点。
  size_t len = s.size();
  while (len > 0 && s[len - 1] == '0') {
    --len;
  }
  if (len > 0 && s[len - 1] == '.') {
    --len;
  }

  result.set_string(s.data(), static_cast<int>(len));
  return true;
}

bool FloatType::my_transform_to_int(const Value &val, Value &result)
{
  const double INT_MAX_D = 2147483647.0;
  const double INT_MIN_D = -2147483648.0;
  float f = val.get_float();
  // 先判断NaN、无穷
  if (!std::isfinite(f)) {
    LOG_WARN("float is nan or inf, cannot cast to int");
    return false;
  }
  // 范围检查
  if (f < INT_MIN_D || f > INT_MAX_D) {
    LOG_WARN("float value out of int32 range");
    return false;
  }
  int int_val = static_cast<int>(f);
  result.set_int(int_val);
  return true;
}

bool FloatType::my_transform_to_dataTime(const Value &val, Value &result) {
  float f = val.get_float();
  if (!std::isfinite(f)) {
    return false;
  }
  if (f < static_cast<float>(INT64_MIN) || f > static_cast<float>(INT64_MAX)) {
    return false;
  }

  // 1. 截断小数部分，得到整数（正数直接强转，丢弃小数）
  int64_t num = static_cast<int64_t>(f);

  // 2. 校验：必须是8位(YYYYMMDD) 或者6位(YYMMDD)
  int16_t y, m, d;
  if (num >= 10000000 && num <= 99991231) {
    // 8位 YYYYMMDD
    y = static_cast<int16_t>(num / 10000);
    m = static_cast<int16_t>((num % 10000) / 100);
    d = static_cast<int16_t>(num % 100);
  } else if (num >= 100000 && num <= 999999) {
    // 6位 YYMMDD
    int yy = static_cast<int16_t>(num / 10000);
    m = static_cast<int16_t>((num % 10000) / 100);
    d = static_cast<int16_t>(num % 100);
    // MySQL两位年份规则
    if (yy <= 69) {
      y = yy + 2000;
    } else {
      y = yy + 1900;
    }
  } else {
    // 不是6位/8位数字，转换失败
    return false;
  }

  // 3. 校验日期合法性，复用你之前的 is_valid_date
  if (!result.set_date(y, m, d)) {
    return false;
  }
  return true;
}
