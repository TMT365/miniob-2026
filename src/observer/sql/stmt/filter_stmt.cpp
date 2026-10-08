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
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/filter_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "common/sys/rc.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

FilterStmt::~FilterStmt()
{
  for (FilterUnit *unit : filter_units_) {
    delete unit;
  }
  filter_units_.clear();
}

RC FilterStmt::create(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
    const ConditionSqlNode *conditions, int condition_num, FilterStmt *&stmt)
{
  RC rc = RC::SUCCESS;
  stmt  = nullptr;

  FilterStmt *tmp_stmt = new FilterStmt();
  for (int i = 0; i < condition_num; i++) {
    FilterUnit *filter_unit = new FilterUnit();

    rc = create_filter_unit(db, default_table, tables, conditions[i], filter_unit);
    if (rc != RC::SUCCESS) {
      delete tmp_stmt;
      LOG_WARN("failed to create filter unit. condition index=%d", i);
      return rc;
    }
    tmp_stmt->filter_units_.push_back(filter_unit);
  }

  stmt = tmp_stmt;
  return rc;
}

RC FilterStmt::get_table_and_field(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
    const string &table_name, const string &field_name, Table *&table, const FieldMeta *&field)
{
  if (common::is_blank(table_name.c_str())) {
    table = default_table;
  } else if (nullptr != tables) {
    auto iter = tables->find(table_name);
    if (iter != tables->end()) {
      table = iter->second;
    }
  } else {
    table = db->find_table(table_name.c_str());
  }
  if (nullptr == table) {
    LOG_WARN("No such table: attr.relation_name: %s", table_name.c_str());
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  field = table->table_meta().field(field_name.c_str());
  if (nullptr == field) {
    LOG_WARN("no such field in table: table %s, field %s", table->name(), field_name.c_str());
    table = nullptr;
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }
  return RC::SUCCESS;
}

RC FilterStmt::create_filter_unit(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
    const ConditionSqlNode &condition, FilterUnit *&filter_unit)
{
  /*
   * NOTE 这里要重新递归产生FilterUnit, FilterObj
   * FilterObj 可以支持 字段、常量、复杂表达式
   */
  RC rc = RC::SUCCESS;

  CompOp comp = condition.comp;
  if (comp < EQUAL_TO || comp >= NO_OP) {
    LOG_WARN("invalid compare operator : %d", comp);
    return RC::INVALID_ARGUMENT;
  }
  // // NOTE 为filter重新分配堆内存
  Expression *left_expr = condition.left_expr.get();
  auto        left_obj  = std::make_unique<FilterObj>();
  rc                    = create_filter_unit_recursion(db, default_table, tables, left_expr, *left_obj);
  if (rc != RC::SUCCESS) {
    delete filter_unit;
    // NOTE std::make_unique<FilterObj>() 是在堆上创建对象，返回给栈上的指针值，所有left_obj会自动析构
    left_obj.reset();
    LOG_WARN("failed to recursively create left filter unit");
    return rc;
  }
  filter_unit->set_left(std::move(left_obj));
  Expression *right_expr = condition.right_expr.get();
  auto        right_obj  = std::make_unique<FilterObj>();
  rc                     = create_filter_unit_recursion(db, default_table, tables, right_expr, *right_obj);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to recursively create right filter unit");
    delete filter_unit;
    right_obj.reset();
    return rc;
  }
  filter_unit->set_right(std::move(right_obj));
  filter_unit->set_comp(comp);

  // 检查两个类型是否能够比较
  // NOTE 注意现在的支持数据类型有: int, float, char, date, bool, vector. [int, float, char, date] 是可以互相做比较的
  // char 类型作比较是是使用 strcmp, char 类型与 int, float比较时转变成对应的类型，默认是0.0
  return rc;
}

RC FilterStmt::create_filter_unit_recursion(
    Db *db, Table *default_table, unordered_map<string, Table *> *tables, Expression *expr, FilterObj &filter_obj)
{
  RC rc = RC::SUCCESS;

  if (expr == nullptr) {
    LOG_WARN("null expression");
    return RC::INVALID_ARGUMENT;
  }
  // NOTE 这是一个关于字段的expression，例如： col1 + 3 >= -(col3 + 4) + (2-1)*2
  /*                      condition[i]
   *                      /             \
   *    [+,ARITHMETIC]left_expr          right_expr[+, ARITHMETIC]
   *                   /   \                 /    \
   *                col1   3[ValueExpr]     ....       ...
   */
  if (expr->type()==ExprType::UNBOUND_FIELD) {
    Table           *table      = nullptr;
    const FieldMeta  *field      = nullptr;
    UnboundFieldExpr *tmp_expr   = static_cast<UnboundFieldExpr *>(expr);
    string            table_name = tmp_expr->table_name();
    string            field_name = tmp_expr->field_name();
    rc                           = get_table_and_field(db, default_table, tables, table_name, field_name, table, field);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot find attr");
      return rc;
    }
    filter_obj.init_attr(Field(table, field));
  } else if (expr->type() == ExprType::ARITHMETIC) {
    /* NOTE 这个争对的是 这样的节点：         (-,+,*,/,//)[Arithmetic]
     *  这样的常量运算节点可以对其提前求值       /       \
     *  如果有无法直接求值的可以递归到下一步   5           4
     */
    auto  arith_expr = static_cast<ArithmeticExpr *>(expr);
    Value v;                            // TODO 递归尝试求值应该是出现NULL，直接returnSUCCESS
    rc = arith_expr->try_get_value(v);  // 可以直接获取 ValueExpr 的value 并进行运算得出结果
    if (rc == RC::SUCCESS) {            // OPTIMIZE 优化了原来0做除数也返回SUCCESS的bug
      filter_obj.init_value(v);  // 能归并成常量，如 3+5
    } else {
      // 归并失败（包含字段），递归处理左右子树
      unique_ptr<Expression> bound_left;
      unique_ptr<Expression> bound_right;

      rc = bind_expression(db, default_table, tables, arith_expr->left().get(), bound_left);
      if (rc != RC::SUCCESS)
        return rc;
      if (arith_expr->right()) {
        rc = bind_expression(db, default_table, tables, arith_expr->right().get(), bound_right);
        if (rc != RC::SUCCESS)
          return rc;
      }
      // 构建新的 ArithmeticExpr，左右子树已经是 Bound 的
      ArithmeticExpr *bound_arith =
          new ArithmeticExpr(arith_expr->arithmetic_type(), bound_left.release(), bound_right.release());
      filter_obj.init_expr(bound_arith);
    }
  } else if (expr->type()==ExprType::VALUE) {
    Value v;
    rc = static_cast<ValueExpr *>(expr)->try_get_value(v);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    filter_obj.init_value(v);
  } else {
    LOG_WARN("invalid expression");
    return RC::INVALID_ARGUMENT;
  }

  return rc;
}
RC FilterStmt::bind_expression(Db *db, Table *default_table, unordered_map<string, Table *> *tables, Expression *expr,
    unique_ptr<Expression> &bound_expr)
{
  if (expr->type() == ExprType::UNBOUND_FIELD) {
    auto            *unbound    = static_cast<UnboundFieldExpr *>(expr);
    Table           *table      = nullptr;
    const FieldMeta *field_meta = nullptr;
    RC               rc =
        get_table_and_field(db, default_table, tables, unbound->table_name(), unbound->field_name(), table, field_meta);
    if (rc != RC::SUCCESS)
      return rc;
    bound_expr.reset(new FieldExpr(Field(table, field_meta)));

  } else if (expr->type() == ExprType::ARITHMETIC) {
    auto                  *arith = static_cast<ArithmeticExpr *>(expr);
    unique_ptr<Expression> left, right;
    RC                     rc = bind_expression(db, default_table, tables, arith->left().get(), left);
    if (rc != RC::SUCCESS)
      return rc;
    if (arith->right()) {
      rc = bind_expression(db, default_table, tables, arith->right().get(), right);
      if (rc != RC::SUCCESS)
        return rc;
    }
    bound_expr.reset(new ArithmeticExpr(arith->arithmetic_type(), left.release(), right.release()));
  } else if (expr->type() == ExprType::VALUE) {
    // 常量值、FieldExpr 等已经是 bound 的，直接拷贝
    // 编译器会对临时对象做优化
    bound_expr=(static_cast<ValueExpr *>(expr)->copy());
  }
  return RC::SUCCESS;
}
