/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <string.h>

#include "storage/field/field_meta.h"
#include "storage/common/vector_buffer.h"
#include "common/lang/memory.h"

/**
 * @brief A column containing either fixed-length values or variable-length strings.
 *
 * Fixed-length values are stored in data_. Variable-length strings are stored in
 * TextData, whose lifetime can be shared by referenced columns.
 */
class Column
{
public:
  enum class Type
  {
    NORMAL_COLUMN,   /// Normal column represents a list of values
    CONSTANT_COLUMN  /// Constant column represents a single value
  };

  Column() = default;

  Column(const Column &other) { copy_from(other); }
  Column(Column &&other)
  {
    data_           = other.data_;
    count_          = other.count_;
    capacity_       = other.capacity_;
    own_            = other.own_;
    attr_type_      = other.attr_type_;
    attr_len_       = other.attr_len_;
    column_type_    = other.column_type_;
    text_data_      = std::move(other.text_data_);
    other.data_     = nullptr;
    other.count_    = 0;
    other.capacity_ = 0;
    other.own_      = false;
  }

  Column &operator=(const Column &other)
  {
    if (this != &other) {
      reset();
      copy_from(other);
    }
    return *this;
  }

  Column &operator=(Column &&other) noexcept
  {
    if (this != &other) {
      reset();
      data_           = other.data_;
      count_          = other.count_;
      capacity_       = other.capacity_;
      own_            = other.own_;
      attr_type_      = other.attr_type_;
      attr_len_       = other.attr_len_;
      column_type_    = other.column_type_;
      text_data_      = std::move(other.text_data_);
      other.data_     = nullptr;
      other.count_    = 0;
      other.capacity_ = 0;
      other.own_      = false;
    }
    return *this;
  }

  Column(const FieldMeta &meta, size_t size = DEFAULT_CAPACITY);
  Column(AttrType attr_type, int attr_len, size_t size = DEFAULT_CAPACITY);

  void init(const FieldMeta &meta, size_t size = DEFAULT_CAPACITY);
  void init(AttrType attr_type, int attr_len, size_t size = DEFAULT_CAPACITY);
  void init_text(size_t size = DEFAULT_CAPACITY);
  void init(const Value &value, size_t size);

  // 克隆出一份独立的数据副本。
  unique_ptr<Column> clone() const { return make_unique<Column>(*this); }

  virtual ~Column() { reset(); }

  void reset();

  RC append_one(const char *data);

  RC append_value(const Value &val);

  /**
   * @brief 向 Column 追加写入数据
   * @param data 要被写入数据的起始地址
   * @param count 要写入数据的长度（这里指列值的个数，而不是字节）
   */
  RC append(const char *data, int count);

  /**
   * @brief 获取 index 位置的列值。
   */
  Value get_value(int index) const;

  RC copy_to(void *dest, int start_rows, int insert_rows) const
  {
    if (text_data_ != nullptr) {
      return RC::UNIMPLEMENTED;
    }
    memcpy(dest, data_ + start_rows * attr_len_, insert_rows * attr_len_);
    return RC::SUCCESS;
  }

  /// 获取定长列数据的字节数；变长字符串列没有连续的 data_ 缓冲区。
  int data_len() const { return text_data_ == nullptr ? count_ * attr_len_ : 0; }

  // 获取定长列的起始地址；变长字符串列应使用 get_value()。
  char *data() const { return data_; }

  // 前面的是增加定长的数据类型比如INT, FLOAT, DATE，这里是增加一个非定常的字符
  string_t add_text(const char *str, int len);

  /**
   * @brief 重置列数据，但不修改元信息
   */
  void reset_data()
  {
    count_         = 0;
    if (text_data_ != nullptr) {
      text_data_ = make_shared<TextData>();
      text_data_->values.reserve(capacity_);
      text_data_->nulls.reserve(capacity_);
    }
  }

  /**
   * @brief 引用另一个 Column
   */
  void reference(const Column &column);

  void set_column_type(const Type column_type) { column_type_ = column_type; }
  void set_attr_type(const AttrType attr_type) { attr_type_ = attr_type; }
  void set_count(const int count) { count_ = count; }

  int                     count() const { return count_; }
  int                     capacity() const { return capacity_; }
  AttrType                attr_type() const { return attr_type_; }
  int                     attr_len() const { return attr_len_; }
  Type                    column_type() const { return column_type_; }
  static constexpr size_t DEFAULT_CAPACITY = 8192;

  bool is_null_column() const { return column_type_ == Type::CONSTANT_COLUMN && attr_type_ == AttrType::NULL_VALS; }

  void set_null(int index)
  {
    if (index < 0 || index >= count_)
      return;
    if (text_data_ != nullptr && index < static_cast<int>(text_data_->nulls.size())) {
      text_data_->nulls[index] = 1;
    }
  }

private:
  struct TextData
  {
    VectorBuffer              buffer;
    vector<string_t>          values;
    vector<uint8_t>           nulls;
  };

  void copy_from(const Column &other)
  {
    count_       = other.count_;
    capacity_    = other.capacity_;
    own_         = true;
    attr_type_   = other.attr_type_;
    attr_len_    = other.attr_len_;
    column_type_ = other.column_type_;
    data_        = nullptr;
    text_data_   = nullptr;

    if (other.text_data_ != nullptr) {
      text_data_ = make_shared<TextData>();
      text_data_->values.reserve(other.text_data_->values.size());
      text_data_->nulls.reserve(other.text_data_->nulls.size());
      for (size_t i = 0; i < other.text_data_->values.size(); ++i) {
        const bool is_null = i < other.text_data_->nulls.size() && other.text_data_->nulls[i] != 0;
        text_data_->nulls.emplace_back(is_null ? 1 : 0);
        if (is_null) {
          text_data_->values.emplace_back(string_t());
        } else {
          text_data_->values.emplace_back(text_data_->buffer.add_string(other.text_data_->values[i]));
        }
      }
    } else if (other.data_ != nullptr) {
      data_ = new char[capacity_ * attr_len_];
      memcpy(data_, other.data_, capacity_ * attr_len_);
    }
  }

  // 底层定长列数据的起始地址；变长字符串列使用 text_data_。
  char *data_ = nullptr;
  /// 当前列值数量
  int count_ = 0;
  /// 当前容量，count_ <= capacity_
  int capacity_ = 0;
  /// 是否拥有内存
  bool own_ = true;
  /// 列属性类型
  AttrType attr_type_ = AttrType::UNDEFINED;
  /// 定长列的元素长度；变长字符串列保存 string_t 的描述长度。
  int attr_len_ = -1;
  /// 列类型
  Type                     column_type_   = Type::NORMAL_COLUMN;
  /// 管理变长字符串的值、NULL 标记和 Arena。
  shared_ptr<TextData> text_data_ = nullptr;
};
