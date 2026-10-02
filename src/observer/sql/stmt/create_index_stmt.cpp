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
// Created by Wangyunlai on 2023/4/25.
//

#include "sql/stmt/create_index_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

using namespace std;
using namespace common;

RC CreateIndexStmt::create(Db *db, const CreateIndexSqlNode &create_index, Stmt *&stmt)
{
  stmt = nullptr;

  const char *table_name = create_index.relation_name.c_str();
  if (is_blank(table_name) || is_blank(create_index.index_name.c_str()) ||
      is_blank(create_index.attribute_name.c_str())) {
    LOG_WARN("invalid argument. db=%p, table_name=%p, index name=%s, attribute name=%s",
        db, table_name, create_index.index_name.c_str(), create_index.attribute_name.c_str());
    return RC::INVALID_ARGUMENT;
  }

  // check whether the table exists
  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  const FieldMeta *field_meta = table->table_meta().field(create_index.attribute_name.c_str());
  if (nullptr == field_meta) {
    LOG_WARN("no such field in table. db=%s, table=%s, field name=%s", 
             db->name(), table_name, create_index.attribute_name.c_str());
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  // 这里只是判断了索引的名字有没有重复，并没有判断给创建的索引字段是否重复
  Index *index = table->find_index(create_index.index_name.c_str());
  if (nullptr != index) {
    LOG_WARN("index with name(%s) already exists. table name=%s", create_index.index_name.c_str(), table_name);
    return RC::SCHEMA_INDEX_NAME_REPEAT;
  }

  IndexType index_type =  create_index.index_type;

  // 从这里开始就加入不同索引对字段的判断
  vector<Index *> *indexes = new vector<Index *>();
  RC rc = table->find_all_index_by_field_name(create_index.attribute_name.c_str(), *indexes, &index_type);

  if (rc != RC::SUCCESS) {
    LOG_WARN("cannot find index type %s in table %s", index_type_to_string(create_index.index_type), table_name);
    delete indexes;
    return rc;
  }

  // 如果该字段已有索引，检查是否冲突
  if (!indexes->empty()) {
    switch (index_type) {
      case IndexType::UNIQUE_INDEX:
      case IndexType::PRIMARY_KEY: {
        LOG_WARN("unique index with name %s already exists. table name=%s", create_index.index_name.c_str(), table_name);
        delete indexes;
        return RC::INDEX_DUPLICATE;
      }
      case IndexType::COMMON_INDEX: {
        // 普通索引允许同一字段有多个，这里继续创建
        break;
      }
      default: {
        delete indexes;
        return RC::UNSUPPORTED;
      }
    }
  }

  // 创建索引语句（所有情况都需要）
  stmt = new CreateIndexStmt(table, field_meta, create_index.index_name, index_type);
  delete indexes;
  return RC::SUCCESS;
}
