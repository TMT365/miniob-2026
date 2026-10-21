//
// Created by ZhuanZ（无密码） on 2026/10/2.
//
#pragma once
#include "./stmt.h"
#include "common/lang/string.h"

class DropTableStmt : public Stmt
{
public:
  explicit DropTableStmt(const string &table_name);
  ~DropTableStmt() override = default;

  StmtType type() const override;

  static RC create(Db *db, DropTableSqlNode &drop_table, Stmt *&stmt);

  const string &table_name() const;
private:
  string table_name_;
};