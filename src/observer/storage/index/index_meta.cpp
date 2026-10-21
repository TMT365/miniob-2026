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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAMES("field_name");
const static Json::StaticString FIELD_INDEX_TYPE("index_type");

RC IndexMeta::init(const char *name, const vector<FieldMeta> &fields, IndexType type)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  if (fields.empty()) {
    LOG_ERROR("FieldMeta is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_  = name;
  fields_str_ = "[";
  for (const FieldMeta &field : fields) {
    fields_.emplace_back(field.name());
    fields_str_ += string(field.name()) + ",";
  }
  fields_str_ += "]";
  type_  = type;
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME]= name_;
  Json::Value arr(Json::arrayValue); // 序列化为数组
  for (const string &field : fields_) {
    arr.append(field);
  }
  json_value[FIELD_FIELD_NAMES] = arr;
  // 索引类型也要持久化，否则重启之后 UNIQUE/PRIMARY 的约束会失效
  json_value[FIELD_INDEX_TYPE] = index_type_to_string(type_);
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value  = json_value[FIELD_NAME];
  const Json::Value &fields_value = json_value[FIELD_FIELD_NAMES];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!fields_value.isArray()) {
    LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
        name_value.asCString(), fields_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  // 读取索引类型。旧版本的 table.meta 没有这个字段，此时按普通索引处理
  IndexType index_type = IndexType::COMMON_INDEX;
  const Json::Value &type_value = json_value[FIELD_INDEX_TYPE];
  if (type_value.isString()) {
    index_type = index_type_from_string(type_value.asCString());
    if (index_type == IndexType::UNDEFINED) {
      LOG_ERROR("Deserialize index [%s]: invalid index type: %s", name_value.asCString(), type_value.asCString());
      return RC::INTERNAL;
    }
  }

  vector<FieldMeta> fieldMeta;

  // 循环取出字段，在table里面查找fieldMeta
  for (const auto &field : fields_value) {
    if (common::is_blank(field.asCString())) {
      LOG_ERROR("Deserialize index [%s]: field name is empty", name_value.asCString());
      return RC::SCHEMA_FIELD_MISSING;
    }

    const FieldMeta *field_meta = table.field(field.asCString());

    if (nullptr == field_meta) {
      LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), fields_value.asCString());
      return RC::SCHEMA_FIELD_MISSING;
    }

    fieldMeta.emplace_back(*field_meta);

  }
  return index.init(name_value.asCString(), fieldMeta, index_type);
}

const char *IndexMeta::name() const { return name_.c_str(); }

const vector<string> &IndexMeta::fields() const { return fields_; }

const IndexType &IndexMeta::type() const { return type_; }


void IndexMeta::desc(ostream &os) const { os << "index name=" << name_ << ", fields=" << fields_str_ << ", type=" << index_type_to_string(type_); }

const char *IndexMeta::fields_str() const { return fields_str_.c_str(); }