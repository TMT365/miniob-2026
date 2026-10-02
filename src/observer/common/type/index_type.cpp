//
// Created by ZhuanZ（无密码） on 2026/10/2.
//
#include "common/lang/string.h"
#include "./index_type.h"

const char* INDEX_TYPE_NAME[] = {"undefined", "common_index", "unique_index", "primary_index", "maxtype"};
const char* index_type_to_string(IndexType type)
{
  if (type >= IndexType::UNDEFINED && type < IndexType::MAXTYPE) {
    return INDEX_TYPE_NAME[static_cast<int>(type)];
  }
  return "unknown";
}
IndexType  index_type_from_string(const char *s)
{
  for (unsigned int i = 0; i < sizeof(INDEX_TYPE_NAME) / sizeof(INDEX_TYPE_NAME[0]); i++) {
    if (0 == strcasecmp(INDEX_TYPE_NAME[i], s)) {
      return (IndexType)i;
    }
  }
  return IndexType::UNDEFINED;
}