//
// Created by ZhuanZ（无密码） on 2026/10/2.
//

#pragma once

/**
 * @brief index的类型
 * @details IndexType 枚举列出了index的各种类型。
 */
enum class IndexType
{
  UNDEFINED,
  COMMON_INDEX,
  UNIQUE_INDEX,
  PRIMARY_KEY,
  MAXTYPE,   ///< 请在 UNDEFINED 与 MAXTYPE 之间增加新类型
};

const char *index_type_to_string(IndexType type);
IndexType   index_type_from_string(const char *s);