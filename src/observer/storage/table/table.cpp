/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Meiyi & Wangyunlai on 2021/5/13.
//

#include <limits.h>
#include <string.h>

#include "common/defs.h"
#include "common/lang/string.h"
#include "common/lang/span.h"
#include "common/lang/algorithm.h"
#include "common/log/log.h"
#include "common/global_context.h"
#include "storage/db/db.h"
#include "storage/buffer/disk_buffer_pool.h"
#include "storage/common/condition_filter.h"
#include "storage/common/meta_util.h"
#include "storage/index/bplus_tree_index.h"
#include "storage/index/index.h"
#include "storage/record/record_manager.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"
#include "storage/record/heap_record_scanner.h"
#include "storage/record/lsm_record_scanner.h"
#include "storage/table/heap_table_engine.h"
#include "storage/table/lsm_table_engine.h"
#include "src/common/sys/rc.h"

Table::~Table()
{
  if (lob_handler_ != nullptr) {
    delete lob_handler_;
    lob_handler_ = nullptr;
  }
}

RC Table::create(Db *db, int32_t table_id, const char *path, const char *name, const char *base_dir,
    span<const AttrInfoSqlNode> attributes, const vector<string> &primary_keys, StorageFormat storage_format, StorageEngine storage_engine)
{
  // 检查参数合法性
  if (table_id < 0) {
    LOG_WARN("invalid table id. table_id=%d, table_name=%s", table_id, name);
    return RC::INVALID_ARGUMENT;
  }

  if (common::is_blank(name)) {
    LOG_WARN("Name cannot be empty");
    return RC::INVALID_ARGUMENT;
  }
  LOG_INFO("Begin to create table %s:%s", base_dir, name);

  if (attributes.size() == 0) {
    LOG_WARN("Invalid arguments. table_name=%s, attribute_count=%d", name, attributes.size());
    return RC::INVALID_ARGUMENT;
  }

  RC rc = RC::SUCCESS;

  // 使用 table_name.table记录一个表的元数据
  // 判断表文件是否已经存在
  int fd = ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (EEXIST == errno) {
      LOG_ERROR("Failed to create table file, it has been created. %s, EEXIST, %s", path, strerror(errno));
      return RC::SCHEMA_TABLE_EXIST;
    }
    LOG_ERROR("Create table file failed. filename=%s, errmsg=%d:%s", path, errno, strerror(errno));
    return RC::IOERR_OPEN;
  }

  close(fd);

  // 创建文件
  const vector<FieldMeta> *trx_fields = db->trx_kit().trx_fields(); // 事务相关的字段，当前选择的事务管理器决定了是否需要这些字段, 如果不需要事务管理器，则为 nullptr, 也不会报错
  if ((rc = table_meta_.init(table_id, name, trx_fields, attributes, primary_keys, storage_format, storage_engine)) != RC::SUCCESS) {
    LOG_ERROR("Failed to init table meta. name:%s, ret:%d", name, rc);
    return rc;  // delete table file
  }
  // fstream 打开文件
  fstream fs;
  fs.open(path, ios_base::out | ios_base::binary);
  if (!fs.is_open()) {
    LOG_ERROR("Failed to open file for write. file name=%s, errmsg=%s", path, strerror(errno));
    return RC::IOERR_OPEN;
  }

  // 记录元数据到文件中
  table_meta_.serialize(fs);
  fs.close();

  db_ = db;
  string data_file = table_data_file(base_dir, name);
  // 获取数据库的 BufferPoolManager
  BufferPoolManager &bpm = db->buffer_pool_manager();
  rc = bpm.create_file(data_file.c_str());
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to create disk buffer pool of data file. file name=%s", data_file.c_str());
    return rc;
  }

  if (table_meta_.storage_engine() == StorageEngine::HEAP) {
    engine_ = make_unique<HeapTableEngine>(&table_meta_, db_, this);
  } else if (table_meta_.storage_engine() == StorageEngine::LSM) {
    engine_ = make_unique<LsmTableEngine>(&table_meta_, db_, this);
  } else {
    rc = RC::UNSUPPORTED;
    LOG_WARN("Unsupported storage engine type: %d", table_meta_.storage_engine());
    return rc;
  }
  rc = engine_->open();
  if (rc != RC::SUCCESS) {
    LOG_WARN("Failed to open table %s due to engine open failed.", data_file.c_str());
    return rc;
  }

  LOG_INFO("Successfully create table %s:%s", base_dir, name);
  return rc;
}

RC Table::open(Db *db, const char *meta_file, const char *base_dir)
{
  // 加载元数据文件
  fstream fs;
  string  meta_file_path = string(base_dir) + common::FILE_PATH_SPLIT_STR + meta_file;
  fs.open(meta_file_path, ios_base::in | ios_base::binary);
  if (!fs.is_open()) {
    LOG_ERROR("Failed to open meta file for read. file name=%s, errmsg=%s", meta_file_path.c_str(), strerror(errno));
    return RC::IOERR_OPEN;
  }
  if (table_meta_.deserialize(fs) < 0) {
    LOG_ERROR("Failed to deserialize table meta. file name=%s", meta_file_path.c_str());
    fs.close();
    return RC::INTERNAL;
  }
  fs.close();

  db_ = db;

  // // 加载数据文件
  // RC rc = init_record_handler(base_dir);
  // if (rc != RC::SUCCESS) {
  //   LOG_ERROR("Failed to open table %s due to init record handler failed.", base_dir);
  //   // don't need to remove the data_file
  //   return rc;
  // }
  RC rc = RC::SUCCESS;

  if (table_meta_.storage_engine() == StorageEngine::HEAP) {
    engine_ = make_unique<HeapTableEngine>(&table_meta_, db_, this);
  }  else if (table_meta_.storage_engine() == StorageEngine::LSM) {
    engine_ = make_unique<LsmTableEngine>(&table_meta_, db_, this);
  } else {
    rc = RC::UNSUPPORTED;
    LOG_ERROR("Unsupported storage engine type: %d", table_meta_.storage_engine());
    return rc;
  }

  rc = engine_->open();
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to open table %s due to engine open failed.", base_dir);
    return rc;
  }

  return rc;
}

RC Table::insert_record(Record &record)
{
  return engine_->insert_record(record);
}

RC Table::insert_chunk(const Chunk& chunk)
{
  return engine_->insert_chunk(chunk);
}

RC Table::visit_record(const RID &rid, function<bool(Record &)> visitor)
{
  return engine_->visit_record(rid, visitor);
}

RC Table::update_record(const Record &old_record, Record &new_record,Field *field, const Value *value)
{
  // 把旧记录复制一份到新记录里面
  RC rc = make_record(old_record, new_record, field, value);
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to make new record for update. table name:%s, ret:%d", table_meta_.name(), strrc(rc));
    return rc;
  }
  return engine_->update_record(old_record, new_record);
}

RC Table::insert_record_with_trx(Record &record, Trx *trx)
{
  return engine_->insert_record_with_trx(record, trx);
}
RC Table::delete_record_with_trx(const Record &record, Trx *trx)
{
  return engine_->delete_record_with_trx(record, trx);
}

RC Table::update_record_with_trx(const Record &old_record, const Record &new_record, Trx* trx)
{
  return engine_->update_record_with_trx(old_record, new_record, trx);
}

RC Table::get_record(const RID &rid, Record &record)
{
  return engine_->get_record(rid, record);
}

const char *Table::name() const { return table_meta_.name(); }

const TableMeta &Table::table_meta() const { return table_meta_; }

RC Table::make_record(int value_num, const Value *values, Record &record)
{
  RC rc = RC::SUCCESS;
  // TODO 这种检查方式，依旧是全部字段都需要传入，后续可以考虑只传入部分字段，其他字段使用默认值
  if (value_num + table_meta_.sys_field_num() != table_meta_.field_num()) {
    LOG_WARN("Input values don't match the table's schema, table name:%s", table_meta_.name());
    return RC::SCHEMA_FIELD_MISSING;
  }

  // 在表中，前几个字段是事务相关的字段，后面才是用户定义的字段，所以需要跳过事务相关的字段
  const int normal_field_start_index = table_meta_.sys_field_num();
  // 复制所有字段的值
  int   record_size = table_meta_.record_size();
  char *record_data = (char *)malloc(record_size);
  memset(record_data, 0, record_size);

  for (int i = 0; i < value_num && OB_SUCC(rc); i++) {
    const FieldMeta *field = table_meta_.field(i + normal_field_start_index);
    const Value &    value = values[i];
    if (field->type() != value.attr_type()) {
      Value real_value;
      rc = Value::cast_to(value, field->type(), real_value);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to cast value. table name:%s,field name:%s,value:%s ",
            table_meta_.name(), field->name(), value.to_string().c_str());
        break;
      }
      rc = set_value_to_record(record_data, real_value, field);
    } else {
      rc = set_value_to_record(record_data, value, field);
    }
  }
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to make record. table name:%s", table_meta_.name());
    free(record_data);
    return rc;
  }

  record.set_data_owner(record_data, record_size);
  return RC::SUCCESS;
}

// 重载一个 make_record
RC Table::make_record(const Record &old_record, Record &new_record, Field *field, const Value *value)
{
  // 先获取旧字段里面的原数据
  const char *old_data = old_record.data();
  int record_size = table_meta_.record_size();

  // 分配新记录的内存
  char *new_data = (char *)malloc(record_size);
  if (new_data == nullptr) {
    LOG_WARN("failed to allocate memory for new record");
    return RC::NOMEM;
  }
  memcpy(new_data, old_data, record_size);

  // 获取要更新的字段的元信息
  const FieldMeta *field_meta = field->meta();

  LOG_INFO("create_new_record called, field_name=%s", field_meta->name());

  // 处理值类型转换
  const Value *real_value = value;
  Value converted_value;
  if (value->attr_type() != field_meta->type()) {
    // 进行类型转换
    RC rc = Value::cast_to(*value, field_meta->type(), converted_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to cast value. field type=%d, value type=%d", field_meta->type(), value->attr_type());
      free(new_data);
      return rc;
    }
    real_value = &converted_value;
  }
  // 把新值写入新记录的指定字段位置
  RC rc = set_value_to_record(new_data, *real_value, field_meta);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to set value to new record: %s", strrc(rc));
    free(new_data);
    return rc;
  }

  // 新记录设置数据
  new_record.set_data_owner(new_data, record_size);
  new_record.set_rid(old_record.rid());
  return RC::SUCCESS;
}

RC Table::make_record(const Record &old_record, Record &new_record)
{
  // 只复制旧记录到新记录，不修改任何字段
  const char *old_data = old_record.data();
  int record_size = table_meta_.record_size();

  // 分配新记录的内存
  char *new_data = (char *)malloc(record_size);
  if (new_data == nullptr) {
    LOG_WARN("failed to allocate memory for new record");
    return RC::NOMEM;
  }
  memcpy(new_data, old_data, record_size);

  // 新记录设置数据
  new_record.set_data_owner(new_data, record_size);
  new_record.set_rid(old_record.rid());
  return RC::SUCCESS;
}

RC Table::make_record(const Record &old_record, Record &new_record, const vector<Field *> &fields, const vector<Value> &values, int value_num)
{
  // 多字段更新：从旧记录创建新记录并设置多个字段的值
  const char *old_data = old_record.data();
  int record_size = table_meta_.record_size();

  // 分配新记录的内存
  char *new_data = (char *)malloc(record_size);
  if (new_data == nullptr) {
    LOG_WARN("failed to allocate memory for new record");
    return RC::NOMEM;
  }
  memcpy(new_data, old_data, record_size);

  // 设置每个字段的值
  for (int i = 0; i < value_num; i++) {
    const FieldMeta *field_meta = fields[i]->meta();
    const Value &value = values[i];

    // 处理类型转换
    const Value *real_value = &value;
    Value converted_value;
    if (value.attr_type() != field_meta->type()) {
      RC rc = Value::cast_to(value, field_meta->type(), converted_value);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to cast value. field type=%d, value type=%d", field_meta->type(), value.attr_type());
        free(new_data);
        return rc;
      }
      real_value = &converted_value;
    }

    // 设置值到记录
    RC rc = set_value_to_record(new_data, *real_value, field_meta);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to set value to record: %s", strrc(rc));
      free(new_data);
      return rc;
    }
  }

  // 新记录设置数据
  new_record.set_data_owner(new_data, record_size);
  new_record.set_rid(old_record.rid());
  return RC::SUCCESS;
}

RC Table::set_value_to_record(char *record_data, const Value &value, const FieldMeta *field)
{
  size_t       copy_len = field->len();
  const size_t data_len = value.length();
  if (field->type() == AttrType::CHARS) {
    if (data_len > copy_len) {
      LOG_WARN("value too long for field %s, field_len=%zu, value_len=%zu",
               field->name(), copy_len, data_len);
      return RC::VALUE_TOO_LONG;
    }
    if (copy_len > data_len) {
      copy_len = data_len + 1;
    }
  }
  memcpy(record_data + field->offset(), value.data(), copy_len);
  return RC::SUCCESS;
}

RC Table::get_record_scanner(RecordScanner *&scanner, Trx *trx, ReadWriteMode mode)
{
  return engine_->get_record_scanner(scanner, trx, mode);
}

RC Table::get_chunk_scanner(ChunkFileScanner &scanner, Trx *trx, ReadWriteMode mode)
{
  return engine_->get_chunk_scanner(scanner, trx, mode);
}

RC Table::create_index(Trx *trx, const vector<FieldMeta> &field_metas, const char *index_name, const IndexType &index_type)
{
  return engine_->create_index(trx, field_metas, index_name,index_type);
}

RC Table::delete_record(const Record &record)
{
  return engine_->delete_record(record);
}

Index *Table::find_index(const char *index_name) const
{
  return engine_->find_index(index_name);
}
Index *Table::find_index_by_field(const char *field_name) const
{
  return engine_->find_index_by_field(field_name);
}
RC Table::find_all_index(vector<Index *> &indexes) const
{
  return engine_->find_all_index(indexes);
}
RC Table::find_all_index_by_field_name(const char *field_name, vector<Index *> &out, IndexType *index_type) const
{
  return engine_->find_all_index_by_field_name(field_name,out,index_type);
}

RC Table::sync()
{
  return engine_->sync();
}
