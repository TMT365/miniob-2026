/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/log/log.h"
#include "storage/common/column.h"

Column::Column(const FieldMeta &meta, size_t size)
    : data_(nullptr),
      count_(0),
      capacity_(0),
      own_(true),
      attr_type_(meta.type()),
      attr_len_(meta.len()),
      column_type_(Type::NORMAL_COLUMN)
{
  // TODO: optimized the memory usage if it doesn't need to allocate memory
  data_     = new char[size * attr_len_];
  memset(data_, 0, size * attr_len_);
  capacity_ = size;
}

Column::Column(AttrType attr_type, int attr_len, size_t capacity)
{
  attr_type_   = attr_type;
  attr_len_    = attr_len;
  data_        = new char[capacity * attr_len_];
  memset(data_, 0, capacity * attr_len_);
  count_       = 0;
  capacity_    = capacity;
  own_         = true;
  column_type_ = Type::NORMAL_COLUMN;
}

void Column::init(const FieldMeta &meta, size_t size)
{
  reset();
  data_        = new char[size * meta.len()];
  memset(data_, 0, size * meta.len());
  count_       = 0;
  capacity_    = size;
  attr_type_   = meta.type();
  attr_len_    = meta.len();
  own_         = true;
  column_type_ = Type::NORMAL_COLUMN;
}

void Column::init(AttrType attr_type, int attr_len, size_t capacity)
{
  reset();
  data_        = new char[capacity * attr_len];
  memset(data_, 0, capacity * attr_len);
  count_       = 0;
  capacity_    = capacity;
  own_         = true;
  attr_type_   = attr_type;
  attr_len_    = attr_len;
  column_type_ = Type::NORMAL_COLUMN;
}

void Column::init_text(size_t capacity)
{
  reset();
  attr_type_   = AttrType::CHARS;
  attr_len_    = sizeof(string_t);
  count_       = 0;
  capacity_    = capacity;
  own_         = true;
  column_type_ = Type::NORMAL_COLUMN;
  text_data_   = make_shared<TextData>();
  text_data_->values.reserve(capacity);
}

void Column::init(const Value &value, size_t size)
{
  reset();
  attr_type_ = value.attr_type();
  attr_len_  = value.length();
  if (attr_len_ == 0) {
    data_      = new char[1];
    data_[0] = '\0';
  } else {
    data_      = new char[attr_len_];
  }
  count_     = size;
  capacity_  = 1;
  own_       = true;
  memcpy(data_, value.data(), attr_len_);
  column_type_ = Type::CONSTANT_COLUMN;

  // 如果 value 是 NULL，记录为 NULL_VALS 类型
  if (value.is_null()) {
    attr_type_   = AttrType::NULL_VALS;
    column_type_ = Type::CONSTANT_COLUMN;
  }
}

void Column::reset()
{
  text_data_ = nullptr;
  if (data_ != nullptr && own_) {
    delete[] data_;
  }
  data_      = nullptr;
  count_     = 0;
  capacity_  = 0;
  own_       = false;
  attr_type_ = AttrType::UNDEFINED;
  attr_len_  = -1;
}

RC Column::append_one(const char *data) { return append(data, 1); }

RC Column::append(const char *data, int count)
{
  if (!own_) {
    LOG_WARN("append data to non-owned column");
    return RC::INTERNAL;
  }
  if (text_data_ != nullptr) {
    LOG_WARN("append fixed-length data to text column");
    return RC::UNIMPLEMENTED;
  }
  if (count_ + count > capacity_) {
    LOG_WARN("append data to full column");
    return RC::INTERNAL;
  }
  // Using a larger integer type to avoid overflow
  size_t total_bytes = static_cast<size_t>(count) * static_cast<size_t>(attr_len_);

  memcpy(data_ + count_ * attr_len_, data, total_bytes);
  count_ += count;
  return RC::SUCCESS;
}

RC Column::append_value(const Value &value)
{
  if (!own_) {
    LOG_WARN("append data to non-owned column");
    return RC::INTERNAL;
  }
  if (count_ >= capacity_) {
    LOG_WARN("append data to full column");
    return RC::INTERNAL;
  }

  if (text_data_ != nullptr) {
    if (!value.is_null() && value.attr_type() != AttrType::CHARS) {
      LOG_WARN("append non-string value to text column");
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    if (value.is_null()) {
      text_data_->values.emplace_back(string_t());
      text_data_->nulls.emplace_back(1);
    } else {
      text_data_->values.emplace_back(text_data_->buffer.add_string(value.data(), value.length()));
      text_data_->nulls.emplace_back(0);
    }
    count_ += 1;
    return RC::SUCCESS;
  }

  size_t total_bytes = std::min(value.length(), attr_len_);
  memcpy(data_ + count_ * attr_len_, value.data(), total_bytes);
  if (total_bytes < attr_len_)
    data_[count_ * attr_len_ + total_bytes] = 0;

  count_ += 1;
  return RC::SUCCESS;
}

string_t Column::add_text(const char *data, int length)
{
  if (text_data_ == nullptr) {
    text_data_ = make_shared<TextData>();
  }
  return text_data_->buffer.add_string(data, length);
}

Value Column::get_value(int index) const
{
  if (column_type_ == Type::CONSTANT_COLUMN) {
    index  = 0;
  }
  if (index >= count_ || index < 0) {
    return Value();
  }
  if (text_data_ != nullptr) {
    if (index >= static_cast<int>(text_data_->values.size())) {
      return Value();
    }
    if (index < static_cast<int>(text_data_->nulls.size()) && text_data_->nulls[index] != 0) {
      Value null_value;
      null_value.set_null();
      return null_value;
    }
    return Value(text_data_->values[index]);
  }
  // 如果是 CONSTANT_COLUMN 且类型是 NULL_VALS，说明存的是 NULL
  if (column_type_ == Type::CONSTANT_COLUMN && attr_type_ == AttrType::NULL_VALS) {
    Value null_val;
    null_val.set_null();
    return null_val;
  }
  // NOTE 使用date_[index * attr_len]
  return Value(attr_type_, &data_[index * attr_len_], attr_len_);
}

void Column::reference(const Column &column)
{
  if (this == &column) {
    return;
  }
  reset();

  this->data_     = column.data();
  this->capacity_ = column.capacity();
  this->count_    = column.count();
  this->own_      = false;

  this->column_type_ = column.column_type();
  this->attr_type_   = column.attr_type();
  this->attr_len_    = column.attr_len();
  this->text_data_   = column.text_data_;
}
