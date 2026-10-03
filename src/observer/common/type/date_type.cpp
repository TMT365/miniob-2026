//
// Created by ZhuanZ（无密码） on 2026/10/3.
//
#include "common/lang/comparator.h"
#include "common/lang/sstream.h"
#include "common/log/log.h"
#include "common/type/date_type.h"
#include "common/value.h"
#include "storage/common/column.h"

#include <algorithm>
#include <iomanip>

int DateType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::DATES && right.attr_type() == AttrType::DATES, "invalid type");

  const auto &ld = left.get_date();
  const auto &rd = right.get_date();

  if (ld->year_ != rd->year_) {
    return ld->year_ < rd->year_ ? -1 : 1;
  }
  if (ld->month_ != rd->month_) {
    return ld->month_ < rd->month_ ? -1 : 1;
  }
  return (ld->day_ < rd->day_) ? -1 : (ld->day_ > rd->day_ ? 1 : 0);
}

int DateType::compare(const Column &left, const Column &right, int left_idx, int right_idx) const {
  ASSERT(left.attr_type() == AttrType::DATES && right.attr_type() == AttrType::DATES, "invalid type");
  ASSERT(left_idx >=0 && left_idx < left.count(), "left index out of range");
  ASSERT(right_idx >=0 && right_idx < right.count(), "right index out of range");

  // 每条日期占用 8字节 = sizeof(DateValue)，与 set_date 的 length_ 保持一致
  const size_t record_size = 8;
  const char *left_base = left.data() + left_idx * record_size;
  const char *right_base = right.data() + right_idx * record_size;

  // 本机字节序读取，与 set_data 保持一致
  const int16_t *left_d  = reinterpret_cast<const int16_t *>(left_base);
  const int16_t *right_d = reinterpret_cast<const int16_t *>(right_base);

  int16_t ly = left_d[0];
  int16_t lm = left_d[1];
  int16_t ld = left_d[2];

  int16_t ry = right_d[0];
  int16_t rm = right_d[1];
  int16_t rd = right_d[2];

  if (ly != ry) return ly < ry ? -1 : 1;
  if (lm != rm) return lm < rm ? -1 : 1;
  return ld < rd ? -1 : (ld > rd ? 1 : 0);
}


RC DateType::set_value_from_str(Value &val, const string &data) const
{
  int16_t year, month, day;
  if (!parse_date(data.c_str(), year, month, day)) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  val.set_date(year,month,day); //TODO 其实这里2个set_date只能使用一个，后续考虑删除
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &val, string &result) const
{
  const auto &d = val.get_date();
  // stringstream ss;
  // ss << d.year_ << "-"
  //    << std::setw(2) << std::setfill('0') << d.month_ << "-"
  //    << std::setw(2) << std::setfill('0') << d.day_;
  // result = ss.str();
  result = date_to_string(d->year_, d->month_, d->day_);
  return RC::SUCCESS;
}

RC DateType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::CHARS: {
      if (!my_transform_to_char(val, result)) {
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    case AttrType::INTS : {
      if (!my_transform_to_int(val, result)) {
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    case AttrType::FLOATS : {
      if (!my_transform_to_float(val, result)) {
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    case AttrType::BOOLEANS : {
      if (!my_transform_to_bool(val, result)) {
        return RC::CANNOT_TO_SWITCH_TYPE;
      }
      break;
    }
    default:
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  return RC::SUCCESS;
}

int DateType::cast_cost(AttrType type)
{
  if (type == AttrType::DATES) {
    return 0;
  }
  if (type == AttrType::CHARS) {
    return 10;
  }
  if (type == AttrType::INTS) {
    return 11;
  }
  if (type == AttrType::FLOATS) {
    return 12;
  }
  return INT32_MAX;
}

bool DateType::parse_date(const char *c, int16_t &year, int16_t &month, int16_t &day,string *format_str)
{
  if (c == nullptr) {
    return false;
  }
  const char *p = c;
  //首先去掉内部多余的空格，2026  - 02  - 24 ---> 2026-02-24 YYYY-MM-DD
  char *buffer = new char[12];    // 多加一位 用来测试是否是 个数超过10 这种情况
  int i = 0;
  while (i < 11 && *p != '\0') {
    if ( *p == 0x20 || *p == 0x09 || *p == 0x0A || *p == 0x0D || *p == 0x0B || *p == 0x0C) {
      p++;
    }else {
      buffer[i++] = *(p++);
    }
  }
  buffer[i] = '\0';
  if ( i == 11) {
    // 超过10个
    return false;
  }
  // 看是否是格式正确
  // 判断格式是那种 1. YYYY-MM-DD 2. YYYYMMDD , 直接格式化成 YYYY-MM-DD
  if (!format_date_str(buffer,year,month,day)) {
    delete[] buffer;
    return false;
  }
  if (format_str != nullptr) {
    *format_str = string(buffer);
  }
  delete[] buffer;
  return true;
}
// 输入：原始字符串存放在 buffer，解析成功后，buffer 被改写为标准 YYYY-MM-DD
bool DateType::format_date_str(char *buffer,  int16_t &year, int16_t &month, int16_t &day)
{
  char *p = buffer;
  int nums[3] = {0};      // 存 year, month, day 的整数
  int num_idx = 0;        // 当前正在解析第几个数字：0年，1月，2日
  int digit_cnt = 0;      // 当前数字累计的位数
  // ========== 阶段1：逐个字符扫描，提取数字，识别分隔符 ==========
  while (*p != '\0')
  {
    char ch = *p;
    if (ch == '-' || ch == '/')
    {
      // 分隔符不能连续出现，且前面必须读到数字
      if (digit_cnt == 0 || num_idx >= 2)
      {
        return false;
      }
      num_idx++;
      digit_cnt = 0;
      p++;
    }
    else if (ch >= '0' && ch <= '9')
    {
      // 数字，转成整数
      nums[num_idx] = nums[num_idx] * 10 + (ch - '0');
      digit_cnt++;
      // 限制每一段最多4位数字（年份最多4位，月日最多2位）
      // if ((num_idx == 0 && digit_cnt > 4) || (num_idx != 0 && digit_cnt > 2))
      // {
      //   // return false;
      //   // digit_cnt = 0;
      //   // 对于 YYYYMMDD 也会进来，所以不处理
      // }
      p++;
    }
    else
    {
      // 出现非数字、非分隔符字符，直接非法
      return false;
    }

  }
  int y, m, d;
  if (num_idx == 2)
  {
    // 有2个分隔符：三段式 Y-M-D
    if (digit_cnt == 0)
    {
      return false;
    }
    y = nums[0];
    m = nums[1];
    d = nums[2];
  }
  else if (num_idx == 0)
  {
    // 无分隔符 YYYYMMDD，必须刚好8位数字
    if (digit_cnt != 8)
    {
      return false;
    }
    int full = nums[0];
    y = full / 10000;
    m = (full % 10000) / 100;
    d = full % 100;
  }
  else
  {// 只有1个分隔符，或者其它情况，直接非法（例如 2026-0224）
    return false;
  }
  // ========== 阶段2：处理年份：两位年份补齐世纪（MySQL规则） ==========
  if (y < 100)
  {
    if (y <= 69)
    {
      y += 2000;
    }
    else
    {
      y += 1900;
    }
  }
  // 校验日期合法性，复用你写好的 is_valid_date
  if (!is_valid_date(y, m, d)){
    return false;
  }
  // ========== 阶段3：把 y,m,d 写回 buffer，格式 YYYY-MM-DD ==========
  buffer[0] = (y / 1000) + '0';
  buffer[1] = ((y / 100) % 10) + '0';
  buffer[2] = ((y / 10) % 10) + '0';
  buffer[3] = (y % 10) + '0';
  buffer[4] = '-';
  buffer[5] = (m / 10) + '0';
  buffer[6] = (m % 10) + '0';
  buffer[7] = '-';
  buffer[8] = (d / 10) + '0';
  buffer[9] = (d % 10) + '0';
  buffer[10] = '\0'; // 字符串结束符

  year = y;
  month = m;
  day = d;
  return true;
}


bool DateType::is_valid_date(int year, int month, int day)
{
  if (year < 1000 || year > 9999) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }
  if (day < 1) {
    return false;
  }

  static const int days_of_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int max_day = days_of_month[month];

  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (leap) {
      max_day = 29;
    }
  }
  return day <= max_day;
}
bool DateType::my_transform_to_char(const Value &val, Value &result)
{
  const auto &d = val.get_date();
  // stringstream ss;
  // ss << d->year_ << "-"
  //    << std::setw(2) << std::setfill('0') << d->month_ << "-"
  //    << std::setw(2) << std::setfill('0') << d->day_;
  // result.set_string(ss.str().c_str());
  string date = date_to_string(d->year_, d->month_, d->day_);
  result.set_string(date.c_str());
  return true;
}

bool DateType::my_transform_to_int(const Value &val, Value &result)
{
  // 把 YYYY-MM-DD 变成 int 直接去掉空格即可
  // TODO 只实现存储和转换 8 位 字符格式
  const auto &d = val.get_date();
  int16_t year = d->year_;
  int16_t month = d->month_;
  int16_t day = d->day_;
  // TODO int16_t 的大小肯定不会超过 int
  result.set_int(year * 1000 + month * 100 + day);
  return true;
}
bool DateType::my_transform_to_float(const Value &val, Value &result)
{
  const auto &d = val.get_date();
  int16_t year = d->year_;
  int16_t month = d->month_;
  int16_t day = d->day_;
  result.set_float(year * 1000 + month * 100 + day);
  return true;
}

bool DateType::my_transform_to_bool(const Value &val, Value &result)
{
  int16_t year = val.get_date()->year_;
  int16_t month = val.get_date()->month_;
  int16_t day = val.get_date()->day_;
  int date = year * 1000 + month * 100 + day;
  if (date == 0) {
    result.set_boolean(false);
  } else {
    result.set_boolean(true);
  }
  return true;
}
