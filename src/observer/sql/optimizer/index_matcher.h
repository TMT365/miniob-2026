/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of the Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY IMPLIED WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/expr/expression.h"
#include "storage/table/table.h"

/**
 * @brief 索引扫描的方案
 * @details 选中了哪个索引，以及扫描的左右边界。
 *          边界用原始字节表示，长度是索引所有列的长度之和；
 *          空的边界向量表示这一侧无界(-infinity / +infinity)。
 */
struct IndexScanPlan
{
  Index       *index = nullptr;   ///< 选中的索引，没有合适的索引时是 nullptr
  vector<char> left_key;          ///< 左边界键
  bool        left_inclusive  = true;
  vector<char> right_key;         ///< 右边界键
  bool        right_inclusive = true;
};

/**
 * @brief 索引选择与边界键构造
 * @details 支持复合索引的"最左前缀"匹配规则：
 *          - 索引的第 0 列必须有可用的谓词，否则这个索引用不了
 *          - 前面连续的列如果是等值条件，可以继续往后匹配
 *          - 遇到第一个范围条件(> < >= <=)就停，后面的列不再参与
 *          - 遇到没有谓词的列就停
 *          选出来的边界键是一个完整的元组：等值列填等值，范围列填范围值，
 *          后面无界的列填该类型的极值。
 */
class IndexMatcher
{
public:
  /**
   * @brief 为一个表选择最合适的索引，并构造出扫描的左右边界
   * @param table 表
   * @param predicates 表上的谓词
   * @param plan 输出的扫描方案，index 为 nullptr 表示应该走全表扫描
   * @return RC
   */
  static RC match(Table *table, vector<unique_ptr<Expression>> &predicates, IndexScanPlan &plan);
};
