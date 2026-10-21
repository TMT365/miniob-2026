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
// Created by Wangyunlai on 2022/07/08.
//

#pragma once

#include "sql/expr/tuple.h"
#include "sql/operator/physical_operator.h"
#include "storage/record/record_manager.h"

/** 支持区间 [a,b], (a,b], [a,b), (a,b) 四种情况
  * left_inclusive_ = true, right_inclusive_ = true  => [a,b]
  * left_inclusive_ = false, right_inclusive_ = true => (a,b]
  * left_inclusive_ = true, right_inclusive_ = false => [a,b)
  * left_inclusive_ = false, right_inclusive_ = false => (a,b)
  * NOTE 边界用原始字节表示。空的边界向量表示这一侧无界(-infinity / +infinity)。
  *      复合索引的边界键是各个索引列的字节依次拼接，长度是所有列长度之和。
  * @brief 索引扫描物理算子
  * @ingroup PhysicalOperator
  */
class IndexScanPhysicalOperator : public PhysicalOperator
{
public:
  IndexScanPhysicalOperator(Table *table, Index *index, ReadWriteMode mode, vector<char> left_key,
      bool left_inclusive, vector<char> right_key, bool right_inclusive);

  virtual ~IndexScanPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::INDEX_SCAN; }

  string param() const override;

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;

  void set_predicates(vector<unique_ptr<Expression>> &&exprs);

private:
  // TODO 与TableScanPhysicalOperator代码相同，可以优化
  RC filter(RowTuple &tuple, bool &result);

  Trx          *trx_           = nullptr;
  Table        *table_         = nullptr;
  Index        *index_         = nullptr;
  ReadWriteMode mode_          = ReadWriteMode::READ_WRITE;
  IndexScanner *index_scanner_ = nullptr;

  Record   current_record_;
  RowTuple tuple_;

  vector<char> left_key_;        ///< 左边界键的字节，空表示无界
  vector<char> right_key_;       ///< 右边界键的字节，空表示无界
  bool         left_inclusive_  = false;
  bool         right_inclusive_ = false;

  vector<unique_ptr<Expression>> predicates_;
};
