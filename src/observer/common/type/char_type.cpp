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
#include "common/log/log.h"
#include "common/type/char_type.h"

#include "date_type.h"
#include "common/value.h"

int CharType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::CHARS && right.attr_type() == AttrType::CHARS, "invalid type");
  return common::compare_string(
      (void *)left.value_.pointer_value_, left.length_, (void *)right.value_.pointer_value_, right.length_);
}

RC CharType::set_value_from_str(Value &val, const string &data) const
{
  val.set_string(data.c_str());
  return RC::SUCCESS;
}


RC CharType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::INTS: {
       std::string s = val.get_string();
      const char *str = s.c_str();
      // 将字符串转换为整数
      bool is_ok = my_atoi(str, result);
        if (!is_ok) {
            LOG_WARN("failed to cast string to int. s=%s", val.get_string().c_str());
            return RC::CANNOT_TO_SWITCH_TYPE;
        }
      break;
    }
    case AttrType::FLOATS: {
      std::string s = val.get_string();
      const char *str = s.c_str();

      bool is_ok = my_atof(str, result);
      if (!is_ok) {
        LOG_WARN("failed to cast string to float. s=%s", val.get_string().c_str());
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    case AttrType::DATES : {
      std::string s = val.get_string();
      if (!my_transform_to_dataTime(s.c_str(), result)) {
        LOG_WARN("failed to cast string to date. s=%s", s.c_str());
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    default: return RC::UNIMPLEMENTED;
  }
  return RC::SUCCESS;
}

int CharType::cast_cost(AttrType type)
{
  if (type == AttrType::CHARS) {
    return 0;
  }
  if (type == AttrType::DATES) {
    return 1;
  }
  if (type == AttrType::INTS) {
    return 2;
  }
  if (type == AttrType::FLOATS) {
    return 3;
  }
  return INT32_MAX;
}

RC CharType::to_string(const Value &val, string &result) const
{
  stringstream ss;
  ss << val.value_.pointer_value_;
  result = ss.str();
  return RC::SUCCESS;
}

bool CharType::my_atoi(const char *str, Value &result)
{
  if (str == nullptr) {
    return false;
  }

  const char *p = str;
  // 跳过前导空格
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') {
    p++;
  }

  // 检查符号
  int sign = *p == '-' ? -1 : 1;
  if (*p == '-' || *p == '+') {
    p++;
  }

  // MySQL 风格的隐式转换：没有有效数字前缀时按 0 处理。
  if (*p == '\0') {
    result.set_int(0);
    return true;
  }

  // 检查第一个字符是否为数字
  if (*p < '0' || *p > '9') {
    result.set_int(0);
    return true;
  }

  // 解析数字
  long long val = 0;
  while (*p >= '0' && *p <= '9') {
    // 当前位数字
    int digit = *p - '0';
    // 如果先有数字加上当前位数字会超过int范围，则返回false
    if (val > (INT32_MAX - digit) / 10) {
        return false;
    }
    val = val * 10 + digit;
    p++;
  }

  val *= sign;
  if (val < INT32_MIN || val > INT32_MAX) {
    return false;
  }

  result.set_int(static_cast<int>(val));
  return true;
}

bool CharType::my_atof(const char *str, Value &result)
{
    if (str == nullptr) {
        return false;
    }

    const char *p = str;
    // 跳过前导空格
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') {
        p++;
    }

    // 检查符号
    int sign = *p == '-' ? -1 : 1;
    if (*p == '-' || *p == '+') {
        p++;
    }

    // MySQL 风格的隐式转换：没有有效数字前缀时按 0.0 处理。
    if (*p == '\0') {
        result.set_float(0.0F);
        return true;
    }

    // 检查第一个字符是否为数字或小数点
    if ((*p < '0' || *p > '9') && *p != '.') {
        result.set_float(0.0F);
        return true;
    }

    // 解析整数部分
    double val = 0.0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10.0 + (*p - '0');
        p++;
    }

    // 如果有小数点，解析小数部分
    if (*p == '.') {
        p++;
        double frac = 0.0;
        double base = 0.1;
        while (*p >= '0' && *p <= '9') {
        frac += (*p - '0') * base;
        base *= 0.1;
        p++;
        }
        val += frac;
    }

    val *= sign;

    result.set_float(static_cast<float>(val));
    return true;
}

bool CharType::my_transform_to_dataTime(const char *str, Value &result)
{
  if (str == nullptr || *str == '\0') {
    return false;
  }
  // 调用 DateType::parse_date
  string formate_str;
  int16_t year, month, day;
  if (!DateType::parse_date(str,year,month,day)) {
    LOG_WARN("failed to cast string to date. s=%s", str);
    return false;
  }
  result.set_date(year, month, day);
  return true;
}