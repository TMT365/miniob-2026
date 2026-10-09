//
// Created by ZhuanZ（无密码） on 2026/10/2.
//

#pragma once

#include "common/sys/rc.h"

class SQLStageEvent;

class DropTableExecutor
{
  public:
  DropTableExecutor() = default;
  virtual ~DropTableExecutor() = default;

  RC execute(SQLStageEvent *sql_event);
};
