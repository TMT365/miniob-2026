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
// Created by WangYunlai on 2023/06/28.
//

#include "common/value.h"

#include <cfloat>
#include <cstdint>

#include "common/lang/comparator.h"
#include "common/lang/exception.h"
#include "common/lang/sstream.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "common/time/datetime.h"
#include "type/date_type.h"
#include "type/char_type.h"

Value::Value(int val) { set_int(val); }

Value::Value(float val) { set_float(val); }

Value::Value(bool val) { set_boolean(val); }

Value::Value(const char *s, int len /*= 0*/) { set_string(s, len); }

Value::Value(const string_t& s) { set_string(s.data(), s.size()); }


Value::Value(const Value &other)
{
  this->attr_type_ = other.attr_type_;
  this->length_    = other.length_;
  this->own_data_  = other.own_data_;
  this->is_null_   = other.is_null_;
  switch (this->attr_type_) {
    case AttrType::CHARS: {
      set_string_from_other(other);
    } break;
    default: {
      this->value_ = other.value_;
    } break;
  }
}

Value::Value(Value &&other)
{
  this->attr_type_ = other.attr_type_;
  this->length_    = other.length_;
  this->own_data_  = other.own_data_;
  this->value_     = other.value_;
  this->is_null_   = other.is_null_;
  other.own_data_  = false;
  other.length_    = 0;
}

Value &Value::operator=(const Value &other)
{
  if (this == &other) {
    return *this;
  }
  reset();
  this->attr_type_ = other.attr_type_;
  this->length_    = other.length_;
  this->own_data_  = other.own_data_;
  this->is_null_   = other.is_null_;
  switch (this->attr_type_) {
    case AttrType::CHARS: {
      set_string_from_other(other);
    } break;
    default: {
      this->value_ = other.value_;
    } break;
  }
  return *this;
}

Value &Value::operator=(Value &&other)
{
  if (this == &other) {
    return *this;
  }
  reset();
  this->attr_type_ = other.attr_type_;
  this->length_    = other.length_;
  this->own_data_  = other.own_data_;
  this->value_     = other.value_;
  this->is_null_   = other.is_null_;
  other.own_data_  = false;  // 这是因为临时对象是没有内存的
  other.length_    = 0;
  return *this;
}

bool Value::operator==(const Value &other) const
{
  // 删掉 if(*this == other) 这一行！！！
  if (attr_type_ != other.attr_type_) {
    return false;
  }
  if (is_null_ != other.is_null_) {
    return false;
  }
  if (length_ != other.length_) {
    return false;
  }

  switch (attr_type_) {
    case AttrType::CHARS: {
      if (is_null_) {
        return true; // 两个都是null字符串，相等
      }
      // 防止空指针传入strcmp
      return strcmp(value_.pointer_value_, other.value_.pointer_value_) == 0;
    }
    case AttrType::INTS:
      return value_.int_value_ == other.value_.int_value_;
    case AttrType::FLOATS:
      return value_.float_value_ == other.value_.float_value_;
    case AttrType::DATES:
      return value_.date_value_.year_ == other.value_.date_value_.year_
          && value_.date_value_.month_ == other.value_.date_value_.month_
          && value_.date_value_.day_ == other.value_.date_value_.day_;
    case AttrType::BOOLEANS:
      return value_.bool_value_ == other.value_.bool_value_;
    default:
      return false;
  }
}

bool Value::operator<(const Value &other) const
{
  // 同类型才可比较
  if (attr_type_ != other.attr_type_) {
    return false;
  }
  if (is_null_ && !other.is_null_) {
    return true; // null < 非null
  }
  switch (attr_type_) {
    case AttrType::CHARS: {
      if (is_null_ && other.is_null_) {
        return false; // null == null
      }
      return strcmp(value_.pointer_value_, other.value_.pointer_value_) < 0;
    }
    case AttrType::INTS:
      return value_.int_value_ < other.value_.int_value_;
    case AttrType::FLOATS:
      return value_.float_value_ < other.value_.float_value_;
    case AttrType::DATES:
      if (value_.date_value_.year_ != other.value_.date_value_.year_) {
        return value_.date_value_.year_ < other.value_.date_value_.year_;
      }
      if (value_.date_value_.month_ != other.value_.date_value_.month_) {
        return value_.date_value_.month_ < other.value_.date_value_.month_;
      }
      return value_.date_value_.day_ < other.value_.date_value_.day_;
    case AttrType::BOOLEANS:
      return value_.bool_value_ < other.value_.bool_value_;
    default:
      return false;
  }

}

void Value::reset()
{
  switch (attr_type_) {
    case AttrType::CHARS: {
      if (own_data_ && value_.pointer_value_ != nullptr) {
        delete[] value_.pointer_value_;
        value_.pointer_value_ = nullptr;
      }
      break;
    }
    default: {
      value_.int_value_        = 0;
      value_.float_value_      = 0.0;
      value_.bool_value_       = false;
      value_.pointer_value_    = nullptr;
      value_.date_value_.year_ = 0;
    } break;
  }
  attr_type_ = AttrType::UNDEFINED;
  length_    = 0;
  own_data_  = false;
  this->is_null_ = false;
}

void Value::set_data(char *data, int length)
{
  switch (attr_type_) {
    case AttrType::CHARS: {
      set_string(data, length);
    } break;
    case AttrType::INTS: {
      value_.int_value_ = *(int *)data;
      length_           = length;
    } break;
    case AttrType::FLOATS: {
      value_.float_value_ = *(float *)data;
      length_             = length;
    } break;
    case AttrType::DATES: {
      const int16_t *d = reinterpret_cast<const int16_t *>(data);
      set_date(d[0], d[1], d[2]);
    } break;
    case AttrType::NULL_VALS: {
      set_null();
    } break;
    case AttrType::BOOLEANS: {
      value_.bool_value_ = *(int *)data != 0;
      length_            = length;
    } break;
    default: {
      LOG_WARN("unknown data type: %d", attr_type_);
    } break;
  }
}

void Value::set_int(int val)
{
  reset();
  attr_type_        = AttrType::INTS;
  value_.int_value_ = val;
  length_           = sizeof(val);
}

void Value::set_float(float val)
{
  reset();
  attr_type_          = AttrType::FLOATS;
  value_.float_value_ = val;
  length_             = sizeof(val);
}
void Value::set_boolean(bool val)
{
  reset();
  attr_type_         = AttrType::BOOLEANS;
  value_.bool_value_ = val;
  length_            = sizeof(val);
}

// 未解析
bool Value::set_date(const string &date)
{
  reset();
  // 对于字符串类型的要先解析
  int16_t year, month, day;
  if (DateType::parse_date(date.c_str(), year, month, day)) {
    attr_type_         = AttrType::DATES;
    value_.date_value_.year_ = year;
    value_.date_value_.month_ = month;
    value_.date_value_.day_ = day;
    value_.date_value_.padding_=0;
    length_            = sizeof(Value::Val::DateValue);
    return true;
  }else {
    return false;
  }
}

// 使用过解析
bool Value::set_date(const int16_t &year, const int16_t &month, const int16_t &day)
{
  reset();
  attr_type_         = AttrType::DATES;
  value_.date_value_.year_ = year;
  value_.date_value_.month_ = month;
  value_.date_value_.day_ = day;
  value_.date_value_.padding_=0;
  length_            = sizeof(Value::Val::DateValue);
  return true;
}

void Value::set_string(const char *s, int len /*= 0*/)
{
  reset();
  attr_type_ = AttrType::CHARS;
  if (s == nullptr) {
    value_.pointer_value_ = nullptr;
    length_               = 0;
  } else {
    own_data_ = true;
    if (len > 0) {
      len = strnlen(s, len);
    } else {
      len = strlen(s);
    }
    value_.pointer_value_ = new char[len + 1];
    length_               = len;
    memcpy(value_.pointer_value_, s, len);
    value_.pointer_value_[len] = '\0';
  }
}

void Value::set_empty_string(int len)
{
  reset();
  attr_type_ = AttrType::CHARS;

  own_data_ = true;
  value_.pointer_value_ = new char[len + 1];
  length_               = len;
  memset(value_.pointer_value_, 0, len);
  value_.pointer_value_[len] = '\0';
  
}

void Value::set_value(const Value &value)
{
  switch (value.attr_type_) {
    case AttrType::INTS: {
      set_int(value.get_int());
    } break;
    case AttrType::FLOATS: {
      set_float(value.get_float());
    } break;
    case AttrType::CHARS: {
      set_string(value.get_string().c_str());
    } break;
    case AttrType::DATES: {
      set_date(value.get_date_to_string());
    } break;
    case AttrType::NULL_VALS: {
      set_null();
    } break;
    case AttrType::BOOLEANS: {
      set_boolean(value.get_boolean());
    } break;
    default: {
      ASSERT(false, "got an invalid value type");
    } break;
  }
}

void Value::set_string_from_other(const Value &other)
{
  ASSERT(attr_type_ == AttrType::CHARS, "attr type is not CHARS");
  if (own_data_ && other.value_.pointer_value_ != nullptr && length_ != 0) {
    this->value_.pointer_value_ = new char[this->length_ + 1];
    memcpy(this->value_.pointer_value_, other.value_.pointer_value_, this->length_);
    this->value_.pointer_value_[this->length_] = '\0';
  }
}


char *Value::data() const
{
  switch (attr_type_) {
    case AttrType::CHARS: {
      return value_.pointer_value_;
    } break;
    default: {
      return (char *)&value_;
    } break;
  }
}

string Value::to_string() const
{
  // 如果是 NULL，返回 "NULL"
  if (is_null_) {
    return "NULL";
  }

  string res;
  RC     rc = DataType::type_instance(this->attr_type_)->to_string(*this, res);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to convert value to string. type=%s", attr_type_to_string(this->attr_type_));
    return "";
  }
  return res;
}

int Value::compare(const Value &other) const { return DataType::type_instance(this->attr_type_)->compare(*this, other); }

int Value::get_int() const
{
  switch (attr_type_) {
    case AttrType::CHARS: {
      try {
        return (int)(stol(value_.pointer_value_));
      } catch (exception const &ex) {
        LOG_TRACE("failed to convert string to number. s=%s, ex=%s", value_.pointer_value_, ex.what());
        return 0;
      }
    }
    case AttrType::INTS: {
      return value_.int_value_;
    }
    case AttrType::FLOATS: {
      return (int)(value_.float_value_);
    }
    case AttrType::DATES: {
      // 复用 DateType::my_transform_to_int 的逻辑
      return value_.date_value_.year_ * 1000 + value_.date_value_.month_ * 100 + value_.date_value_.day_;
    }

    case AttrType::BOOLEANS: {
      return (int)(value_.bool_value_);
    }
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return 0;
    }
  }
  return 0;
}

float Value::get_float() const
{
  switch (attr_type_) {
    case AttrType::CHARS: {
      try {
        return stof(value_.pointer_value_);
      } catch (exception const &ex) {
        LOG_TRACE("failed to convert string to float. s=%s, ex=%s", value_.pointer_value_, ex.what());
        return 0.0;
      }
    } break;
    case AttrType::INTS: {
      return float(value_.int_value_);
    } break;
    case AttrType::FLOATS: {
      return value_.float_value_;
    } break;
    case AttrType::BOOLEANS: {
      return float(value_.bool_value_);
    } break;
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return 0;
    }
  }
  return 0;
}

string Value::get_string() const { return this->to_string(); }

string_t Value::get_string_t() const
{
  ASSERT(attr_type_ == AttrType::CHARS, "attr type is not CHARS");
  return string_t(value_.pointer_value_, length_);
}

// TODO 还没有实现date类型
bool Value::get_boolean() const
{
  switch (attr_type_) {
    case AttrType::CHARS: {
      try {
        float val = stof(value_.pointer_value_);
        if (val >= EPSILON || val <= -EPSILON) {
          return true;
        }

        int int_val = stol(value_.pointer_value_);
        if (int_val != 0) {
          return true;
        }

        return value_.pointer_value_ != nullptr;
      } catch (exception const &ex) {
        LOG_TRACE("failed to convert string to float or integer. s=%s, ex=%s", value_.pointer_value_, ex.what());
        return value_.pointer_value_ != nullptr;
      }
    } break;
    case AttrType::INTS: {
      return value_.int_value_ != 0;
    } break;
    case AttrType::FLOATS: {
      float val = value_.float_value_;
      return val >= EPSILON || val <= -EPSILON;
    } break;
    case AttrType::DATES : {
      return true;
    }
    case AttrType::BOOLEANS: {
      return value_.bool_value_;
    } break;
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return false;
    }
  }
  return false;
}
const Value::Val::DateValue *Value::get_date() const {
  if (attr_type_ == AttrType::UNDEFINED || attr_type_ == AttrType::NULL_VALS || is_null_) {
    return nullptr;
  }

  if (attr_type_ == AttrType::DATES) {
    return &value_.date_value_;
  }

  Value converted_value;
  if (cast_to(*this, AttrType::DATES, converted_value) == RC::SUCCESS) {
    date_cache_ = converted_value.value_.date_value_;
    return &date_cache_;
  }
  return nullptr;
}

string Value::get_date_to_string() const
{
  // TODO 稍后实现
  switch (attr_type_) {
    case AttrType::DATES: {
      // 使用格式化输出
      string date = date_to_string(value_.date_value_.year_, value_.date_value_.month_, value_.date_value_.day_);
      return date;
    } break;
    case AttrType::CHARS: {
      return "";
    } break;
    case AttrType::INTS: {
      return "";
    } break;
    case AttrType::FLOATS: {
      return "";
    } break;
    case AttrType::BOOLEANS: {
      return "";
    } break;
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
    }
  }
  return "";
}
Value::Val::DateValue::DateValue()
{
  year_ = 0;
  month_   = 0;
  day_     = 0;
  padding_ = 0;
}

Value::Val::DateValue::DateValue(int x)
{
  year_    = x;
  month_   = x;
  day_     = x;
  padding_ = x;
}
Value::Val::DateValue::DateValue(const int16_t year, const int16_t month, const int16_t day)
{
  year_    = year;
  month_   = month;
  day_     = day;
  padding_ = 0;
}

string Value::Val::DateValue::get_four_year() const {
  if (year_ == 0 && month_ == 0 && day_ == 0) {
    return "0000";
  }
  return std::to_string(year_);
}

string Value::Val::DateValue::get_two_year() const
{
  if (year_ == 0 && month_ == 0 && day_ == 0) {
    return "00";
  }
  int year = year_ % 100;
  if (year < 10) {
    return "0" + std::to_string(year);
  }
  return std::to_string(year);
}

string Value::Val::DateValue::get_two_month() const
{
  if (month_ == 0 && day_ == 0) {
    return "00";
  }
  if (month_ < 10) {
    return "0" + std::to_string(month_);
  }
  return std::to_string(month_);
}

string Value::Val::DateValue::get_two_day() const
{
  if (day_ == 0) {
    return "00";
  }
  if (day_ < 10) {
    return "0" + std::to_string(day_);
  }
  return std::to_string(day_);
}
RC Value::serialize_to_key(const Value &value, AttrType type, int length, char *buf, bool want_greater, bool *adjusted)
{
  if (length <= 0) {
    return RC::INVALID_ARGUMENT;
  }
  if (nullptr != adjusted) {
    *adjusted = false;
  }

  if (value.is_null()) {
    memset(buf, 0, length);
    return RC::SUCCESS;
  }

  // 类型不一致时先转换，保证写进索引键的字节和记录里存的一致
  Value       converted;
  const Value *source = &value;
  if (value.attr_type() != type) {
    RC rc = cast_to(value, type, converted);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to cast value for index key. type=%s, value=%s", attr_type_to_string(type),
               value.to_string().c_str());
      return rc;
    }
    source = &converted;
  }

  switch (type) {
    case AttrType::INTS: {
      int32_t int_value = source->get_int();
      memcpy(buf, &int_value, sizeof(int32_t));
      memset(buf + sizeof(int32_t), 0, length - sizeof(int32_t));
    } break;
    case AttrType::FLOATS: {
      float float_value = source->get_float();
      memcpy(buf, &float_value, sizeof(float));
      memset(buf + sizeof(float), 0, length - sizeof(float));
    } break;
    case AttrType::DATES: {
      const Value::Val::DateValue *date_value = source->get_date();
      if (nullptr == date_value) {
        memset(buf, 0, length);
        break;
      }
      // 日期在记录里占 sizeof(DateValue) 字节
      memcpy(buf, date_value, sizeof(Value::Val::DateValue));
      memset(buf + sizeof(Value::Val::DateValue), 0, length - sizeof(Value::Val::DateValue));
    } break;
    case AttrType::CHARS: {
      const char *str        = source->data();
      int         str_length = source->length();
      if (str == nullptr) {
        str_length = 0;
      }
      if (str_length > length) {
        // 边界值比列还长。列里存不下这个值，只能截断；下界要把最后一个字节加一，
        // 这样截断后的边界仍然能表达原来的比较语义
        bool should_inclusive = false;
        RC   rc              = normalize_user_key(str, str_length, length, want_greater, buf, &should_inclusive);
        if (OB_FAIL(rc)) {
          return rc;
        }
        if (nullptr != adjusted) {
          *adjusted = should_inclusive;
        }
        break;
      }
      // 记录里存的是 字符串 + '\0' + 补零，这里保持一致
      int copy_length = str_length;
      if (copy_length > 0) {
        memcpy(buf, str, copy_length);
      }
      memset(buf + copy_length, 0, length - copy_length);
    } break;
    case AttrType::BOOLEANS: {
      int32_t bool_value = source->get_boolean() ? 1 : 0;
      memcpy(buf, &bool_value, sizeof(int32_t));
      memset(buf + sizeof(int32_t), 0, length - sizeof(int32_t));
    } break;
    default: {
      LOG_WARN("unsupported type for index key. type=%s", attr_type_to_string(type));
      return RC::UNIMPLEMENTED;
    }
  }

  return RC::SUCCESS;
}

void Value::fill_extreme(AttrType type, int length, char *buf, bool maximum)
{
  if (length <= 0) {
    return;
  }

  switch (type) {
    case AttrType::INTS: {
      int32_t value = maximum ? INT32_MAX : INT32_MIN;
      memcpy(buf, &value, sizeof(int32_t));
      memset(buf + sizeof(int32_t), 0, length - sizeof(int32_t));
    } break;
    case AttrType::FLOATS: {
      float value = maximum ? FLT_MAX : -FLT_MAX;
      memcpy(buf, &value, sizeof(float));
      memset(buf + sizeof(float), 0, length - sizeof(float));
    } break;
    case AttrType::DATES: {
      // 日期按 (年, 月, 日) 比较，用 int16 的上下限保证大于/小于所有合法日期
      Value::Val::DateValue date_value;
      date_value.year_    = maximum ? INT16_MAX : INT16_MIN;
      date_value.month_   = maximum ? INT16_MAX : INT16_MIN;
      date_value.day_     = maximum ? INT16_MAX : INT16_MIN;
      date_value.padding_ = 0;
      memcpy(buf, &date_value, sizeof(Value::Val::DateValue));
      memset(buf + sizeof(Value::Val::DateValue), 0, length - sizeof(Value::Val::DateValue));
    } break;
    case AttrType::CHARS: {
      // 变长字符串的最小值是全 0，最大值是全 0xFF
      memset(buf, maximum ? 0xFF : 0x00, length);
    } break;
    case AttrType::BOOLEANS: {
      int32_t value = maximum ? 1 : 0;
      memcpy(buf, &value, sizeof(int32_t));
      memset(buf + sizeof(int32_t), 0, length - sizeof(int32_t));
    } break;
    default: {
      memset(buf, maximum ? 0xFF : 0x00, length);
    } break;
  }
}

RC Value::normalize_user_key(
    const char *user_key, int key_len, int attr_length, bool want_greater, char *buf, bool *should_inclusive)
{
  if (nullptr == buf || nullptr == should_inclusive) {
    return RC::INVALID_ARGUMENT;
  }
  if (attr_length <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  *should_inclusive = false;

  if (key_len <= attr_length) {
    // 比列短，补零即可
    if (key_len > 0 && nullptr != user_key) {
      memcpy(buf, user_key, key_len);
    }
    memset(buf + (key_len > 0 ? key_len : 0), 0, attr_length - (key_len > 0 ? key_len : 0));
    return RC::SUCCESS;
  }

  // 比列长，截断到列的长度
  if (nullptr != user_key) {
    memcpy(buf, user_key, attr_length);
  } else {
    memset(buf, 0, attr_length);
  }

  if (nullptr == user_key || user_key[attr_length] == 0) {
    // 字符串刚好在列的长度处结束，不需要调整
    return RC::SUCCESS;
  }

  // 边界值比列长，下界要把最后一个字节加一，上界不加，并且边界要变成闭区间
  *should_inclusive = true;
  if (want_greater) {
    buf[attr_length - 1]++;
  }

  return RC::SUCCESS;
}
