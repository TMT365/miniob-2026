
//
// Created by tangmingtao  on 2022/6/27.
//

#include "sql/operator/update_physical_operator.h"
#include "sql/operator/predicate_physical_operator.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"
#include "sql/expr/tuple.h"

UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, unique_ptr<Field> field, const Value *value, FilterStmt *filter_stmt)
    : table_(table), field_(std::move(field)), value_(value), filter_stmt_(filter_stmt)
{}

RC UpdatePhysicalOperator::open(Trx *trx)
{
  LOG_INFO("success get one record from scan");
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  unique_ptr<PhysicalOperator> &child = children_[0];

  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  trx_ = trx;

  // 第一阶段：收集所有匹配的记录
  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get current record: %s", strrc(rc));
      return rc;
    }
    LOG_INFO("收集符号条件的记录");
    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record   &record    = row_tuple->record();
    records_.emplace_back(std::move(record));
  }

  child->close();  // 先关闭子节点，释放所有锁

  // 第二阶段：执行更新操作
  for (Record &record : records_) {
    Record new_record;
    rc = create_new_record(record, new_record);
    if (OB_SUCC(rc)) {
      LOG_INFO("开始执行更新操作");
      rc = trx_->update_record(table_, record, new_record);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to update record: %s", strrc(rc));
        return rc;
      }
    } else {
      LOG_WARN("failed to create new record: %s", strrc(rc));
      return rc;
    }
  }

  return RC::SUCCESS;
}


RC UpdatePhysicalOperator::next()
{
  return RC::RECORD_EOF;
}

RC UpdatePhysicalOperator::close()
{
  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::create_new_record(const Record &old_record, Record &new_record)
{
  // 先获取旧字段里面的原数据
  const char *old_data = old_record.data();
  int record_size = table_->table_meta().record_size();

  // 分配新记录的内存
  char *new_data = (char *)malloc(record_size);
  if (new_data == nullptr) {
    LOG_WARN("failed to allocate memory for new record");
    return RC::NOMEM;
  }
  memcpy(new_data, old_data, record_size);

  // 获取要更新的字段的元信息
  const FieldMeta *field_meta = field_->meta();

  LOG_INFO("create_new_record called, field_name=%s", field_meta->name());

  // 处理值类型转换
  Value real_value;
  const Value *value_to_use = value_;
  if (value_->attr_type() != field_meta->type()) {
    // 进行类型转换
    RC rc = Value::cast_to(*value_, field_meta->type(), real_value);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to cast value. field type=%d, value type=%d", field_meta->type(), value_->attr_type());
      free(new_data);
      return rc;
    }
    value_to_use = &real_value;
  }
  // 把新值写入新记录的指定字段位置
  RC rc = set_value_to_record(new_data, *value_to_use, field_meta);
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

RC UpdatePhysicalOperator::set_value_to_record(char *new_data, const Value &value, const FieldMeta *field_meta)
{
  size_t copy_len = field_meta->len();
  size_t data_len = value.length();

  if (field_meta->type() == AttrType::CHARS) {
    // 对于字符串类型，确保不超过字段长度
    if (data_len > copy_len) {
      LOG_WARN("value length exceeds field length. field length=%zu, value length=%zu", copy_len, data_len);
      return RC::INVALID_ARGUMENT;
    }
    // 填充剩余部分为0
    if (data_len <= copy_len) {
      copy_len = data_len + 1; // 包括字符串结束符
    }
    memcpy(new_data + field_meta->offset(), value.data(), copy_len);

  } else {
    // 对于非字符串类型，直接拷贝数据
    if (data_len != copy_len) {
      LOG_WARN("value length does not match field length. field length=%zu, value length=%zu", copy_len, data_len);
      return RC::INVALID_ARGUMENT;
    }
    memcpy(new_data + field_meta->offset(), value.data(), copy_len);
  }

  return RC::SUCCESS;
}