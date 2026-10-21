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

#include "common/sys/rc.h"
#include "common/log/log.h"
#include "common/lang/memory.h"
#include "common/lang/vector.h"
#include "storage/common/column.h"

/**
 * @brief A Chunk represents a set of columns.
 */
class Chunk
{
public:
  static const int MAX_ROWS = Column::DEFAULT_CAPACITY;  /// 8192 / 1024 = 8 KB
  Chunk()                   = default;
  Chunk(const Chunk &other)
  {
    for (size_t i = 0; i < other.columns_.size(); ++i) {
      columns_.emplace_back(other.columns_[i]->clone());
    }
    column_ids_ = other.column_ids_;
  }
  // NOTE 使用移动构造函数，传递临时的右值，复杂度O(1). 可以提升性能。因为chunk区域是8KB大小，内存区域太大，不适合深拷贝
  Chunk(Chunk &&chunk) noexcept
  {
    columns_    = std::move(chunk.columns_);
    column_ids_ = std::move(chunk.column_ids_);
  }

  int column_num() const { return columns_.size(); }

  Column &column(const size_t idx)
  {
    ASSERT(idx < columns_.size(), "invalid column index");
    return *columns_[idx];
  }

  const Column &column(const size_t idx) const
  {
    ASSERT(idx < columns_.size(), "invalid column index");
    return *columns_[idx];
  }

  Column *column_ptr(const size_t idx)
  {
    ASSERT(idx < columns_.size(), "invalid column index");
    return &column(idx);
  }

  int column_ids(const size_t i) const
  {
    ASSERT(i < column_ids_.size(), "invalid column index");
    return column_ids_[i];
  }

  void add_column(unique_ptr<Column> col, int col_id);

  RC reference(Chunk &chunk);

  /**
   * @brief 获取 Chunk 中的行数
   */
  int rows() const;

  /**
   * @brief 获取 Chunk 的容量
   */
  int capacity() const;

  /**
   * @brief 从 Chunk 中获得指定行指定列的 Value
   * @param col_idx 列索引
   * @param row_idx 行索引
   * @return Value
   * @note 没有检查 col_idx 和 row_idx 是否越界
   *
   */
  Value get_value(const int col_idx, const int row_idx) const { return columns_[col_idx]->get_value(row_idx); }

  /**
   * @brief 重置 Chunk 中的数据，不会修改 Chunk 的列属性。
   */
  void reset_data();

  void reset();

private:
  // TODO
  // 在多表查询的时候这里可以删除除了，不需要处理所有数据的列，只存需要输出的列就行。column_ids_就是存要输出那些列的id
  vector<unique_ptr<Column>> columns_;
  vector<int> column_ids_;
};
