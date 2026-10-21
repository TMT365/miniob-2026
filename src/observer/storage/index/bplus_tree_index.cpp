/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of the Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY IMPLIED WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the Mulan PSL v2 for more details. */

//
// Created by wangyunlai.wyl on 2021/5/19.
//

#include "storage/index/bplus_tree_index.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/db/db.h"

BplusTreeIndex::~BplusTreeIndex() noexcept { close(); }

/**
 * 创建一个新索引
 * @param table 那张表
 * @param file_name 文件名
 * @param index_meta 索引元数据
 * @param field_metas 索引涉及的各个字段的元数据，单列索引只有一个
 * @return RC
 */
RC BplusTreeIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const vector<FieldMeta> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to create index due to the index has been created before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.fields_str());
    return RC::RECORD_OPENNED;
  }

  Index::init(index_meta, field_metas);

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  // 创建B+树索引处理器
  if (field_metas.empty()) {
    LOG_WARN("Failed to create index due to the field_metas is empty. file_name=%s, index=%s, field:%s",
        file_name, index_meta.name(), index_meta.fields_str());
    return RC::INVALID_ARGUMENT;
  }
  if (field_metas.size() > MAX_INDEX_FIELDS) {
    LOG_WARN("Failed to create index due to too many fields. file_name=%s, index=%s, field:%s, size=%zu, max=%d",
        file_name, index_meta.name(), index_meta.fields_str(), field_metas.size(), MAX_INDEX_FIELDS);
    return RC::INVALID_ARGUMENT;
  }

  // 索引列的描述信息。它既用于写索引文件头，也是运行时从记录中切出用户键的唯一依据，
  // 这样 B+树内部与索引层对用户键格式的理解始终是一致的。
  vector<IndexAttrMeta> attrs;
  attrs.reserve(field_metas.size());
  for (const FieldMeta &field_meta : field_metas) {
    IndexAttrMeta attr;
    attr.type   = field_meta.type();
    attr.length = field_meta.len();
    attr.offset = field_meta.offset();
    attrs.push_back(attr);
  }

  RC rc = index_handler_.create(table->db()->log_handler(), bpm, file_name, attrs);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to create index_handler, file_name=%s, index=%s, field:%s, rc=%s",
        file_name, index_meta.name(), index_meta.fields_str(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully create index, file_name=%s, index=%s, field:%s",
    file_name, index_meta.name(), index_meta.fields_str());
  return RC::SUCCESS;
}

RC BplusTreeIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const vector<FieldMeta> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to open index due to the index has been initedd before. file_name=%s, index=%s, field:%s",
        file_name, index_meta.name(), index_meta.fields_str());
    return RC::RECORD_OPENNED;
  }

  Index::init(index_meta, field_metas);

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  RC rc = index_handler_.open(table->db()->log_handler(), bpm, file_name);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to open index_handler, file_name=%s, index=%s, field:%s, rc=%s",
        file_name, index_meta.name(), index_meta.fields_str(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully open index, file_name=%s, index=%s, field:%s",
    file_name, index_meta.name(), index_meta.fields_str());
  return RC::SUCCESS;
}

RC BplusTreeIndex::close()
{
  if (inited_) {
    LOG_INFO("Begin to close index, index=%s, field=%s", index_meta_.name(), index_meta_.fields_str());
    index_handler_.close();
    inited_ = false;
  }
  LOG_INFO("Successfully close index.");
  return RC::SUCCESS;
}

/**
 * @brief 按索引列的顺序，从记录中切出用户键
 * @details 复合索引的用户键是各个索引列的字节依次拼接。切列的依据取自索引文件头，
 *          与 B+树内部对用户键格式的理解保持一致。
 */
RC BplusTreeIndex::build_key(const char *record, char *key, int key_len) const
{
  const IndexFileHeader &header = index_handler_.file_header();
  int                   offset  = 0;
  for (int i = 0; i < header.num_fields; i++) {
    const IndexAttrMeta &attr = header.attrs[i];
    if (offset + attr.length > key_len) {
      LOG_WARN("build key overflow. offset=%d, length=%d, key_len=%d", offset, attr.length, key_len);
      return RC::INVALID_ARGUMENT;
    }
    memcpy(key + offset, record + attr.offset, attr.length);
    offset += attr.length;
  }
  if (offset != key_len) {
    LOG_WARN("build key length mismatch. offset=%d, key_len=%d", offset, key_len);
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

RC BplusTreeIndex::insert_entry(const char *record, const RID *rid)
{
  const IndexFileHeader &header = index_handler_.file_header();
  if (header.num_fields == 1) {
    // 单列索引直接指向记录中该列的字节，不需要拷贝
    return index_handler_.insert_entry(record + header.attrs[0].offset, rid);
  }

  // 复合索引：按列顺序拼接各列的字节
  vector<char> key(header.attr_length);
  RC          rc = build_key(record, key.data(), header.attr_length);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  return index_handler_.insert_entry(key.data(), rid);
}

RC BplusTreeIndex::delete_entry(const char *record, const RID *rid)
{
  const IndexFileHeader &header = index_handler_.file_header();
  if (header.num_fields == 1) {
    return index_handler_.delete_entry(record + header.attrs[0].offset, rid);
  }

  vector<char> key(header.attr_length);
  RC          rc = build_key(record, key.data(), header.attr_length);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  return index_handler_.delete_entry(key.data(), rid);
}

RC BplusTreeIndex::check_exists(const char *record, bool &exists)
{
  const IndexFileHeader &header = index_handler_.file_header();
  list<RID>             rids;
  if (header.num_fields == 1) {
    index_handler_.get_entry(record + header.attrs[0].offset, header.attrs[0].length, rids);
  } else {
    vector<char> key(header.attr_length);
    RC          rc = build_key(record, key.data(), header.attr_length);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    index_handler_.get_entry(key.data(), header.attr_length, rids);
  }
  exists = !rids.empty();
  return RC::SUCCESS;
}

IndexScanner *BplusTreeIndex::create_scanner(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  BplusTreeIndexScanner *index_scanner = new BplusTreeIndexScanner(index_handler_);
  RC rc = index_scanner->open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open index scanner. rc=%d:%s", rc, strrc(rc));
    delete index_scanner;
    return nullptr;
  }
  return index_scanner;
}

RC BplusTreeIndex::sync() { return index_handler_.sync(); }

////////////////////////////////////////////////////////////////////////////////
BplusTreeIndexScanner::BplusTreeIndexScanner(BplusTreeHandler &tree_handler) : tree_scanner_(tree_handler) {}

BplusTreeIndexScanner::~BplusTreeIndexScanner() noexcept { tree_scanner_.close(); }

RC BplusTreeIndexScanner::open(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  return tree_scanner_.open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
}

RC BplusTreeIndexScanner::next_entry(RID *rid) { return tree_scanner_.next_entry(*rid); }

RC BplusTreeIndexScanner::destroy()
{
  delete this;
  return RC::SUCCESS;
}
