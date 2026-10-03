//
// Created by ZhuanZ（无密码） on 2026/10/3.
//

#pragma once
#include "common/sys/rc.h"
#include "./data_type.h"

/**
 * @brief 日期类型
 * @ingroup DataType
 */
class DateType : public DataType
{
public:
  DateType() : DataType(AttrType::DATES) {}
  virtual ~DateType() = default;

  int compare(const Value &left, const Value &right) const override;

  int compare(const Column &left, const Column &right, int left_idx, int right_idx) const override;

  RC set_value_from_str(Value &val, const string &data) const override;

  RC to_string(const Value &val, string &result) const override;

  RC cast_to(const Value &val, AttrType type, Value &result) const override;

  int cast_cost(AttrType type) override;

  static bool parse_date(const char* c, int16_t &year, int16_t &month, int16_t &day,string *format_str = nullptr);
private:
  static bool format_date_str(char *buffer, int16_t &year, int16_t &month, int16_t &day);
  static bool is_valid_date(int year, int month, int day);
  static bool my_transform_to_char(const Value &val, Value &result);
  static bool my_transform_to_int(const Value &val, Value &result);
  static bool my_transform_to_float(const Value &val, Value &result);
  static bool my_transform_to_bool(const Value &val, Value &result);
};

// 辅助：把数字转字符串，至少width位，前面补'0'
inline string int_to_str_pad(int16_t num, int width) {
  string s = to_string(num);
  if (s.size() < (size_t)width) {
    // 前面补0
    s.insert(s.begin(), width - s.size(), '0');
  }
  return s;
}

// 日期转 YYYY-MM-DD
inline string date_to_string(int16_t year, int16_t month, int16_t day) {
  string y = int_to_str_pad(year, 4);
  string m = int_to_str_pad(month, 2);
  string d = int_to_str_pad(day, 2);
  return y + "-" + m + "-" + d;
}