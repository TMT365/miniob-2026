//
// Created by ZhuanZ（无密码） on 2026/10/2.
//

#include "drop_table_stmt.h"

#include "common/log/log.h"
#include "event/sql_debug.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

DropTableStmt::DropTableStmt(const string &table_name): table_name_(table_name)
{

}

StmtType DropTableStmt::type() const
{
  return StmtType::DROP_TABLE;
}

const string &DropTableStmt::table_name() const
{
  return table_name_;
}

RC DropTableStmt::create(Db *db, DropTableSqlNode &drop_table, Stmt *&stmt)
{
  RC rc = RC::SUCCESS;
  stmt = nullptr;
  // 第一步，获取 DropTableSqlNode 里面的值
  const char *table_name = drop_table.relation_name.c_str();
  // using namespace common;
  if (common::is_blank(table_name)) {
    LOG_WARN("invalid table name");
    return RC::INVALID_ARGUMENT;
  }

  // 第二步, 判断这个表名在数据库里面有没有
  Table *table = db->find_table(table_name);
  if (table == nullptr) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // 第三步，创建对象
  stmt = new DropTableStmt(table_name);
  sql_debug("drop table statement: table name %s", drop_table.relation_name.c_str());
  return rc;
}
