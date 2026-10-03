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
#include "common/type/integer_type.h"
#include "common/value.h"
#include "storage/common/column.h"

int IntegerType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::INTS, "left type is not integer");
  ASSERT(right.attr_type() == AttrType::INTS || right.attr_type() == AttrType::FLOATS, "right type is not numeric");
  if (right.attr_type() == AttrType::INTS) {
    return common::compare_int((void *)&left.value_.int_value_, (void *)&right.value_.int_value_);
  } else if (right.attr_type() == AttrType::FLOATS) {
    float left_val  = left.get_float();
    float right_val = right.get_float();
    return common::compare_float((void *)&left_val, (void *)&right_val);
  }
  return INT32_MAX;
}

int IntegerType::compare(const Column &left, const Column &right, int left_idx, int right_idx) const
{
  ASSERT(left.attr_type() == AttrType::INTS, "left type is not integer");
  ASSERT(right.attr_type() == AttrType::INTS, "right type is not integer");
  return common::compare_int((void *)&((int*)left.data())[left_idx],
      (void *)&((int*)right.data())[right_idx]);
}

RC IntegerType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::FLOATS: {
      bool is_ok = my_transform_to_float(val, result);
      if (!is_ok) {
          LOG_WARN("failed to cast int to float. val=%d", val.get_int());
          return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    case AttrType::CHARS: {
      bool is_ok = my_transform_to_char(val, result);
      if (!is_ok) {
        LOG_WARN("failed to cast int to string. val=%d", val.get_int());
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    case AttrType::DATES : {
      if (!my_transform_to_dataTime(val, result)) {
        LOG_WARN("failed to cast int to datetime. val=%d", val.get_int());
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    default:
      LOG_WARN("unsupported type %d", type);
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
  return RC::SUCCESS;
}

RC IntegerType::add(const Value &left, const Value &right, Value &result) const
{
  result.set_int(left.get_int() + right.get_int());
  return RC::SUCCESS;
}

RC IntegerType::subtract(const Value &left, const Value &right, Value &result) const
{
  result.set_int(left.get_int() - right.get_int());
  return RC::SUCCESS;
}

RC IntegerType::multiply(const Value &left, const Value &right, Value &result) const
{
  result.set_int(left.get_int() * right.get_int());
  return RC::SUCCESS;
}

RC IntegerType::negative(const Value &val, Value &result) const
{
  result.set_int(-val.get_int());
  return RC::SUCCESS;
}

RC IntegerType::set_value_from_str(Value &val, const string &data) const
{
  RC                rc = RC::SUCCESS;
  stringstream deserialize_stream;
  deserialize_stream.clear();  // 清理stream的状态，防止多次解析出现异常
  deserialize_stream.str(data);
  int int_value;
  deserialize_stream >> int_value;
  if (!deserialize_stream || !deserialize_stream.eof()) {
    rc = RC::SCHEMA_FIELD_TYPE_MISMATCH;
  } else {
    val.set_int(int_value);
  }
  return rc;
}

RC IntegerType::to_string(const Value &val, string &result) const
{
  stringstream ss;
  ss << val.value_.int_value_;
  result = ss.str();
  return RC::SUCCESS;
}

bool IntegerType::my_transform_to_float(const Value &val, Value &result)
{
  int32_t int_val = val.get_int();
  // 2^24 = 16777216, 因为float的精度是24位，所以超过这个范围的整数可能无法精确表示为float
  if (int_val < -16777216 || int_val > 16777216) {
    return false; // 可以自定义错误码或日志记录，表示转换失败
  }
  float float_value = static_cast<float>(int_val);
  result.set_float(float_value);
  return true;
}

bool IntegerType::my_transform_to_char(const Value &val, Value &result)
{
  int num = val.get_int();
  if (num == 0) {
    result.set_string("0");
    return true;
  }

  char buffer[32]; // 足够大以容纳32位整数的字符串表示
  int  index = 31; // 从buffer的末尾开始填充
  char *p = buffer + index;
  *p = '\0'; // 终止符

  bool negative = false;
  if (num < 0) {
    negative = true;
    // INI_MIN 的绝对值大于INI_MAX，所以需要特殊处理
    if (num == INT32_MIN) {
        // INT32_MIN 的字符串表示为 "-2147483648"
        result.set_string("-2147483648");
        return true;
    }

    num = -num;
  }

  while (num != 0) {
    // 获取最后一位数字，并将其转换为字符
    int digit = num % 10;
    *(--p) = '0' + digit; // 将数字转换为字符并
    num = num / 10;
  }

  if (negative) {
    *(--p) = '-';
  }

  result.set_string(p);
  return true;
}

bool IntegerType::my_transform_to_dataTime(const Value &val, Value &result) {
  // 1. 获取整数
  int num = val.get_int();
  if (num < 0) {
    return false; // 负数不可能是日期
  }

  int y, m, d;
  if (num >= 10000000) {
    // 8位数字 YYYYMMDD
    if (num > 99991231) { //8 位最大不能超过 `99991231`
      return false;
    }
    y = num / 10000;
    m = (num % 10000) / 100;
    d = num % 100;
  } else if (num >= 100000) {
    // 6位数字 YYMMDD
    if (num > 991231) {
      return false;
    }
    int yy = num / 10000;
    m = (num % 10000) / 100;
    d = num % 100;
    // MySQL两位年份规则
    if (yy <= 69) {
      y = yy + 2000;
    } else {
      y = yy + 1900;
    }
  } else {
    // 不是6位、8位数字，直接非法
    return false;
  }

  if (!result.set_date(y, m, d)) {
    return false;
  }
  return true;
}
