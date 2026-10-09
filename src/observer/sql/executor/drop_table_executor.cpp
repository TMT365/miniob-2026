//
// Created by ZhuanZ（无密码） on 2026/10/2.
//

#include "drop_table_executor.h"
#include "event/sql_event.h"
#include "sql/stmt/drop_table_stmt.h"
#include "sql/stmt/stmt.h"
#include "session/session.h"
#include "event/session_event.h"
#include "storage/db/db.h"

RC DropTableExecutor::execute(SQLStageEvent*sql_event )
{
  RC rc = RC::SUCCESS;
  Stmt *stmt = dynamic_cast<DropTableStmt *>(sql_event->stmt());
  SessionEvent *session_event = sql_event->session_event();
  Session *session = session_event->session();

  // 断言
  ASSERT(stmt->type() == StmtType::DROP_TABLE, "drop table executor can not run this command: %d",
      static_cast<int>(stmt->type()));

  // 获取当前会话选择的数据库
  Db *db = session->get_current_db();
  DropTableStmt *drop_table_stmt = static_cast<DropTableStmt *>(stmt);
  const char *table_name = drop_table_stmt->table_name().c_str();
  rc = db->drop_table(table_name);
  return rc;
}