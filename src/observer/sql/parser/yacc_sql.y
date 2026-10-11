
%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/log/log.h"
#include "common/lang/string.h"
#include "sql/parser/parse_defs.h"
#include "sql/parser/yacc_sql.hpp"
#include "sql/parser/lex_sql.h"
#include "sql/expr/expression.h"
#include "common/type/index_type.h"

using namespace std;

string token_name(const char *sql_string, YYLTYPE *llocp)
{
  return string(sql_string + llocp->first_column, llocp->last_column - llocp->first_column + 1);
}

int yyerror(YYLTYPE *llocp, const char *sql_string, ParsedSqlResult *sql_result, yyscan_t scanner, const char *msg)
{
  unique_ptr<ParsedSqlNode> error_sql_node = make_unique<ParsedSqlNode>(SCF_ERROR);
  error_sql_node->error.error_msg = msg;
  error_sql_node->error.line = llocp->first_line;
  error_sql_node->error.column = llocp->first_column;
  sql_result->add_sql_node(std::move(error_sql_node));
  return 0;
}

ArithmeticExpr *create_arithmetic_expression(ArithmeticExpr::Type type,
                                             Expression *left,
                                             Expression *right,
                                             const char *sql_string,
                                             YYLTYPE *llocp)
{
  ArithmeticExpr *expr = new ArithmeticExpr(type, left, right);
  expr->set_name(token_name(sql_string, llocp));
  return expr;
}

UnboundFunctionExpr *create_function_expression(const char *function_name,
                                                vector<unique_ptr<Expression>> *args,
                                                const char *sql_string,
                                                YYLTYPE *llocp)
{
  UnboundFunctionExpr *expr = new UnboundFunctionExpr(function_name, *args);
  expr->set_name(token_name(sql_string, llocp));
  delete args;
  return expr;
}

%}

%define api.pure full
%define parse.error verbose
/** 启用位置标识 **/
%locations
%lex-param { yyscan_t scanner }
/** 这些定义了在yyparse函数中的参数 **/
%parse-param { const char * sql_string }
%parse-param { ParsedSqlResult * sql_result }
%parse-param { void * scanner }

//标识tokens
%token  SEMICOLON
        BY
        CREATE
        DROP
        GROUP
        TABLE
        TABLES
        INDEX
        CALC
        SELECT
        DESC
        SHOW
        SYNC
        INSERT
        DELETE
        UPDATE
        LBRACE
        RBRACE
        COMMA
        TRX_BEGIN
        TRX_COMMIT
        TRX_ROLLBACK
        INT_T
        STRING_T
        FLOAT_T
        DATE_T
        VECTOR_T
        HELP
        EXIT
        DOT //QUOTE
        INTO
        VALUES
        FROM
        WHERE
        AND
        SET
        ON
        LOAD
        DATA
        INFILE
        EXPLAIN
        STORAGE
        FORMAT
        UNIQUE
        NOT
        NULL_VAL
        PRIMARY
        KEY
        ANALYZE
        FIELDS
        TERMINATED
        ENCLOSED
        EQ
        LT
        GT
        LE
        GE
        NE
        INNER
        JOIN
        LEFT
        RIGHT

/** union 中定义各种数据类型，真实生成的代码也是union类型，所以不能有非POD类型的数据 **/
%union {
  ParsedSqlNode *                            sql_node;
  ConditionSqlNode *                         condition;
  Value *                                    value;
  enum CompOp                                comp;
  RelAttrSqlNode *                           rel_attr;
  vector<AttrInfoSqlNode> *                  attr_infos;
  AttrInfoSqlNode *                          attr_info;
  JoinSqlNode*                               join;
  Expression *                               expression;
  vector<unique_ptr<Expression>> *           expression_list;
  vector<unique_ptr<UpdatePairNode>> *       update_pair_list;
  unique_ptr<UpdatePairNode>*                update_pair;
  vector<Value> *                            value_list;
  vector<ConditionSqlNode> *                 condition_list;
  vector<RelAttrSqlNode> *                   rel_attr_list;
  vector<string> *                           relation_list;
  vector<string> *                           key_list;
  char *                                     cstring;
  int                                        number;
  float                                      floats;
}

%destructor { delete $$; } <condition>
%destructor { delete $$; } <value>
%destructor { delete $$; } <rel_attr>
%destructor { delete $$; } <attr_infos>
%destructor { delete $$; } <expression>
%destructor { delete $$; } <expression_list>
%destructor { delete $$; } <value_list>
%destructor { delete $$; } <condition_list>
// %destructor { delete $$; } <rel_attr_list>
%destructor { delete $$; } <relation_list>
%destructor { delete $$; } <key_list>
%destructor { delete $$; } <update_pair_list>
%destructor { delete $$; } <update_pair>
%destructor { delete $$; } <join>

//终结符，就是不可再拆分的token
%token <number> NUMBER
%token <floats> FLOAT
%token <cstring> ID
%token <cstring> SSS

//非终结符
/** type 定义了各种解析后的结果输出的是什么类型。类型对应了 union 中的定义的成员变量名称 **/
%type <number>              type
%type <condition>           condition
%type <value>               value
%type <number>              number
%type <cstring>             relation
%type <comp>                comp_op
%type <rel_attr>            rel_attr
%type <join>                join
%type <attr_infos>          attr_def_list
%type <attr_info>           attr_def
%type <value_list>          value_list
%type <condition_list>      where
%type <condition_list>      condition_list
%type <condition_list> join_condition_list
%type <condition_list> comma_join_condition
%type <cstring>          storage_format
%type <key_list>            primary_key
%type <key_list>            attr_list
%type <relation_list>       rel_list
%type <expression>          expression
%type <expression>          function_call
%type <expression>          select_item
%type <expression_list>     expression_list
%type <expression_list>     select_expression_list
%type <expression_list>     group_by
%type <update_pair>         update_pair
%type <update_pair_list>    update_pair_list
%type <cstring>             fields_terminated_by
%type <cstring>             enclosed_by
%type <sql_node>            calc_stmt
%type <sql_node>            select_stmt
%type <sql_node>            insert_stmt
%type <sql_node>            update_stmt
%type <sql_node>            delete_stmt
%type <sql_node>            create_table_stmt
%type <sql_node>            drop_table_stmt
%type <sql_node>            analyze_table_stmt
%type <sql_node>            show_tables_stmt
%type <sql_node>            desc_table_stmt
%type <sql_node>            create_index_stmt
%type <sql_node>            drop_index_stmt
%type <sql_node>            sync_stmt
%type <sql_node>            begin_stmt
%type <sql_node>            commit_stmt
%type <sql_node>            rollback_stmt
%type <sql_node>            load_data_stmt
%type <sql_node>            explain_stmt
%type <sql_node>            set_variable_stmt
%type <sql_node>            help_stmt
%type <sql_node>            exit_stmt
%type <sql_node>            command_wrapper
// commands should be a list but I use a single command instead
%type <sql_node>            commands

%left '+' '-'
%left '*' '/'
%right UMINUS /** 这里定义了一个优先级，表示负号的优先级高于加减乘除 **/
%%

//所有语法解析的入口，解析结果放在sql_result中
commands: command_wrapper opt_semicolon  //commands or sqls. parser starts here.
  {
    unique_ptr<ParsedSqlNode> sql_node = unique_ptr<ParsedSqlNode>($1);
    sql_result->add_sql_node(std::move(sql_node));
  }
  ;

command_wrapper:
    calc_stmt
  | select_stmt
  | insert_stmt
  | update_stmt
  | delete_stmt
  | create_table_stmt
  | drop_table_stmt
  | analyze_table_stmt
  | show_tables_stmt
  | desc_table_stmt
  | create_index_stmt
  | drop_index_stmt
  | sync_stmt
  | begin_stmt
  | commit_stmt
  | rollback_stmt
  | load_data_stmt
  | explain_stmt
  | set_variable_stmt
  | help_stmt
  | exit_stmt
   ;

exit_stmt:      
    EXIT {
      (void)yynerrs;  // 这么写为了消除yynerrs未使用的告警。如果你有更好的方法欢迎提PR
      $$ = new ParsedSqlNode(SCF_EXIT);

    };

help_stmt:
    HELP {
      $$ = new ParsedSqlNode(SCF_HELP);

    };

sync_stmt:
    SYNC {
      $$ = new ParsedSqlNode(SCF_SYNC);

    }
    ;

begin_stmt:
    TRX_BEGIN  {
      $$ = new ParsedSqlNode(SCF_BEGIN);

    }
    ;

commit_stmt:
    TRX_COMMIT {
      $$ = new ParsedSqlNode(SCF_COMMIT);

    }
    ;

rollback_stmt:
    TRX_ROLLBACK  {
      $$ = new ParsedSqlNode(SCF_ROLLBACK);

    }
    ;

drop_table_stmt:    /*drop table 语句的语法解析树*/
    DROP TABLE ID {
      $$ = new ParsedSqlNode(SCF_DROP_TABLE);
      $$->drop_table.relation_name = $3;

    };

analyze_table_stmt:  /* analyze table 语法的语法解析树*/
    ANALYZE TABLE ID {
      $$ = new ParsedSqlNode(SCF_ANALYZE_TABLE);
      $$->analyze_table.relation_name = $3;

    }
    ;

show_tables_stmt:
    SHOW TABLES {
      $$ = new ParsedSqlNode(SCF_SHOW_TABLES);

    }
    ;

desc_table_stmt:
    DESC ID  {
      $$ = new ParsedSqlNode(SCF_DESC_TABLE);
      $$->desc_table.relation_name = $2;

    }
    ;

create_index_stmt:    /*create index 语句的语法解析树*/
    CREATE INDEX ID ON ID LBRACE ID RBRACE  /* 普通索引 */
    {
      $$ = new ParsedSqlNode(SCF_CREATE_INDEX);
      CreateIndexSqlNode &create_index = $$->create_index;
      create_index.index_type = IndexType::COMMON_INDEX;
      create_index.index_name = $3;
      create_index.relation_name = $5;
      create_index.attribute_name = $7;

    }
    | CREATE UNIQUE INDEX ID ON ID LBRACE ID RBRACE  /* 索引 */
    {
      $$ = new ParsedSqlNode(SCF_CREATE_INDEX);
      CreateIndexSqlNode &create_index = $$->create_index;
      create_index.index_type = IndexType::UNIQUE_INDEX;
      create_index.index_name = $4;
      create_index.relation_name = $6;
      create_index.attribute_name = $8;
    }
    | CREATE PRIMARY KEY ID ON ID LBRACE ID RBRACE  /* 创建主键暂时页算在这里面 */
    {
      $$ = new ParsedSqlNode(SCF_CREATE_INDEX);
      CreateIndexSqlNode &create_index = $$->create_index;
      create_index.index_type = IndexType::PRIMARY_KEY;
      create_index.index_name = $4;
      create_index.relation_name = $6;
      create_index.attribute_name = $8;
    }
    ;

drop_index_stmt:      /*drop index 语句的语法解析树*/
    DROP INDEX ID ON ID
    {
      $$ = new ParsedSqlNode(SCF_DROP_INDEX);
      $$->drop_index.index_name = $3;
      $$->drop_index.relation_name = $5;

    }
    ;
create_table_stmt:    /*create table 语句的语法解析树*/
    CREATE TABLE ID LBRACE attr_def_list primary_key RBRACE storage_format
    {
      $$ = new ParsedSqlNode(SCF_CREATE_TABLE);
      CreateTableSqlNode &create_table = $$->create_table;
      create_table.relation_name = $3;
      //free($3);

      create_table.attr_infos.swap(*$5);
      delete $5;

      if ($6 != nullptr) {
        create_table.primary_keys.swap(*$6);
        delete $6;
      }
      if ($8 != nullptr) {
        create_table.storage_format = $8;
      }

    }
    ;
    
attr_def_list:
    attr_def
    {
      $$ = new vector<AttrInfoSqlNode>;

      $$->emplace_back(*$1);
      delete $1;
    }
    | attr_def_list COMMA attr_def
    {
      $$ = $1;
      $$->emplace_back(*$3);
      delete $3;
    }
    ;
    
attr_def:
    ID type LBRACE number RBRACE 
    {
      $$ = new AttrInfoSqlNode;
      $$->type = (AttrType)$2;
      $$->name = $1;
      $$->length = $4;

    }
    | ID type
    {
      $$ = new AttrInfoSqlNode;
      $$->type = (AttrType)$2;
      $$->name = $1;
      $$->length = 4;

    }
    ;
number:
    NUMBER {$$ = $1;}
    ;
type:
    INT_T      { $$ = static_cast<int>(AttrType::INTS); }
    | STRING_T { $$ = static_cast<int>(AttrType::CHARS); }
    | FLOAT_T  { $$ = static_cast<int>(AttrType::FLOATS); }
    | DATE_T   { $$ = static_cast<int>(AttrType::DATES); }
    | VECTOR_T { $$ = static_cast<int>(AttrType::VECTORS); }
    ;
primary_key:
    /* empty */
    {
      $$ = nullptr;
    }
    | COMMA PRIMARY KEY LBRACE attr_list RBRACE
    {
      $$ = $5;
    }
    ;

attr_list:
    ID {
      $$ = new vector<string>();
      $$->push_back($1);

    }
    | ID COMMA attr_list {
      if ($3 != nullptr) {
        $$ = $3;
      } else {
        $$ = new vector<string>;
      }

      $$->insert($$->begin(), $1);
    }
    ;

insert_stmt:        /*insert   语句的语法解析树*/
    INSERT INTO ID VALUES LBRACE value_list RBRACE 
    {
      $$ = new ParsedSqlNode(SCF_INSERT);
      $$->insertion.relation_name = $3;
      $$->insertion.values.swap(*$6);
      delete $6;
    }
    ;

value_list:
    value
    {
      $$ = new vector<Value>;
      $$->emplace_back(*$1);

      delete $1;
    }
    | value_list COMMA value { 
      $$ = $1;
      $$->emplace_back(*$3);
      delete $3;
    }
    ;
value:
    | NUMBER {
      $$ = new Value((int)$1);

      @$ = @1;
    }
    | '-' NUMBER {
      $$ = new Value(-(int)$2);

      @$ = @2;
    }
    | FLOAT {
      $$ = new Value((float)$1);

      @$ = @1;
    }
    | '-' FLOAT {
      $$ = new Value(-(float)$2);
      @$ = @2;
    }
    | SSS {
      char *tmp = common::substr($1,1,strlen($1)-2);
      $$ = new Value(tmp);
      free(tmp);
    }
    | NULL_VAL {
      $$ = new Value();
      $$->set_null();
    }
    ;
storage_format:
    /* empty */
    {
      $$ = nullptr;
    }
    | STORAGE FORMAT EQ ID
    {
      $$ = $4;
    }
    ;
    
delete_stmt:    /*  delete 语句的语法解析树*/
    DELETE FROM ID where
    {
      $$ = new ParsedSqlNode(SCF_DELETE);
      $$->deletion.relation_name = $3;
      if ($4 != nullptr) {
        $$->deletion.conditions.swap(*$4);
        delete $4;
      }

    }
    ;
update_stmt:      /*  update 语句的语法解析树，现在支持多字段更新 */
    UPDATE ID SET update_pair_list where
    {
      $$ = new ParsedSqlNode(SCF_UPDATE);
      $$->update.relation_name = $2;
      if ($4 != nullptr) {
        $$->update.update_pairs.swap(*$4);  /** 定义的 update_pairs 的返回值是 vector<unique_ptr<UpdatePairNode>> *，所以这里使用swap是为了避免内存泄漏 **/
        delete $4;
      }
      if ($5 != nullptr) {
        $$->update.conditions.swap(*$5);
        delete $5;
      }

    }
    ;
update_pair_list:
    update_pair {
        $$ = new vector<unique_ptr<UpdatePairNode>>;
        $$->emplace_back(std::move(*$1));
        delete $1; // ！！非常关键：释放 $1 这个unique_ptr包装器本身，节点的内存由vector接管，不会释放节点
    }
    | update_pair COMMA update_pair_list {
        $$ = $3;
        $$->insert($$->begin(), std::move(*$1));
        delete $1; // 释放临时的unique_ptr*包装
    }
;

update_pair:
    rel_attr EQ expression { /** 这里使用 rel_attr EQ expression 是为了支持 update t set a=1 + 2, b=2 + a 的语法 **/
      UpdatePairNode *node = new UpdatePairNode();
      node->attr.attribute_name = $1->attribute_name;
      node->attr.relation_name = $1->relation_name;
      node->value.reset($3);
      $$ = new std::unique_ptr<UpdatePairNode>(node);
      delete $1;
    }
    ;
select_stmt:        /*  select 语句的语法解析树*/
    SELECT select_expression_list
    {
      $$ = new ParsedSqlNode(SCF_SELECT);

      if ($2 != nullptr) {
        $$->selection.expressions.swap(*$2);
        delete $2;
      }
    }
    | SELECT select_expression_list FROM join where group_by /* join -> JoinSqlNode* */
    {
      $$ = new ParsedSqlNode(SCF_SELECT);

      if ($2 != nullptr) {
        $$->selection.expressions.swap(*$2);
        delete $2;
      }

      if ($4 != nullptr) {
        $$->selection.join.reset($4);
      }

      if ($5 != nullptr) {
        $$->selection.conditions.swap(*$5);
        delete $5;
      }

      if ($6 != nullptr) {
        $$->selection.group_by.swap(*$6);
        delete $6;
      }
    }
    ;
calc_stmt:
    CALC expression_list
    {
      $$ = new ParsedSqlNode(SCF_CALC);

      $$->calc.expressions.swap(*$2);
      delete $2;
    }
    ;

expression_list: /* 现在开始支持使用() */
    expression
    {
      $$ = new vector<unique_ptr<Expression>>;
      $$->emplace_back($1);

    }
    | expression COMMA expression_list
    {
      if ($3 != nullptr) {
        $$ = $3;
      } else {
        $$ = new vector<unique_ptr<Expression>>;

      }
      $$->emplace($$->begin(), $1);
    }
    ;
select_expression_list:
    select_item
    {
      $$ = new vector<unique_ptr<Expression>>;
      $$->emplace_back($1);
    }
    | select_item COMMA select_expression_list
    {
      $$ = $3;
      $$->emplace($$->begin(), $1);
    }
    ;
select_item:
    expression
    {
      $$ = $1;
    }
    | expression ID
    {
      $1->set_name($2);
      free($2);
      $$ = $1;
    }
    ;
expression:  /* 括号能算在表达式里面 */
    expression '+' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::ADD, $1, $3, sql_string, &@$);
    }
    | expression '-' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::SUB, $1, $3, sql_string, &@$);
    }
    | expression '*' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::MUL, $1, $3, sql_string, &@$);
    }
    | expression '/' expression { /* NOTE miniob 支持 / 是整除吗？ */
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::DIV, $1, $3, sql_string, &@$);
    }
    | '-' expression %prec UMINUS {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::NEGATIVE, $2, nullptr, sql_string, &@$);
    }
    | LBRACE expression RBRACE {    /* 使用这一条就可以匹配所有情况的括号 */
      $$ = $2;
      $$->set_name(token_name(sql_string, &@$));
    }
    | '*' {
      $$ = new StarExpr();
    }
    | value {
      $$ = new ValueExpr(*$1);
      $$->set_name(token_name(sql_string, &@$));

      delete $1;
    }
    | rel_attr {
      RelAttrSqlNode *node = $1;
      $$ = new UnboundFieldExpr(node->relation_name, node->attribute_name);
      $$->set_name(token_name(sql_string, &@$));
      delete $1;
    }
    | function_call {
      $$ = $1;
      $$->set_name(token_name(sql_string, &@$));
    }
    ;
function_call:
    ID LBRACE expression_list RBRACE {
      $$ = create_function_expression($1, $3, sql_string, &@$);
    }
    ;
rel_attr:
    ID {
      $$ = new RelAttrSqlNode;
      $$->attribute_name = $1;

    }
    | ID DOT ID {
      $$ = new RelAttrSqlNode;
      $$->relation_name  = $1;
      $$->attribute_name = $3;
    }
    ;
join:  /* JoinSqlNode* */
     ID {
        JoinSqlNode *node = new JoinSqlNode();
        node->left = nullptr;
        node->right = nullptr;
        node->type = TableRefType::BASE_TABLE;
        node->table_name = $1;
        node->table_alias = $1;
        node->on_conditions.clear();
        node->on_filter_stmt=nullptr;
        $$ = (node);
     }
     | join JOIN join ON join_condition_list {
        JoinSqlNode *node = new JoinSqlNode();
        node->type = TableRefType::JOIN_NODE;
        node->join_type = JoinType::INNER_JOIN;
        node->on_filter_stmt=nullptr;
        node->left.reset($1);
        node->right.reset($3);
        if ($5 != nullptr) {
          node->on_conditions.swap(*$5);
          delete $5;
        }
        $$ = node;
     }
     | join INNER JOIN join ON join_condition_list {
        JoinSqlNode *node = new JoinSqlNode();
        node->type = TableRefType::JOIN_NODE;
        node->join_type = JoinType::INNER_JOIN;
        node->on_filter_stmt=nullptr;
        node->left.reset($1);
        node->right.reset($4);
        if ($6 != nullptr) {
          node->on_conditions.swap(*$6);
          delete $6;
        }
        $$ = node;
     }
     | join COMMA join comma_join_condition {
        JoinSqlNode *node = new JoinSqlNode();
        node->left.reset($1);
        node->right.reset($3);
        node->on_filter_stmt=nullptr;
        node->type = TableRefType::JOIN_NODE;
        if ($4 == nullptr) {
          node->join_type = JoinType::COMMA_CARTESIAN;
        } else {
          // 兼容“逗号连接后带 ON”的写法：有 ON 条件时按受限内连接处理。
          node->join_type = JoinType::INNER_JOIN;
          node->on_conditions.swap(*$4);
          delete $4;
        }
        $$ = (node);
     }
     ;
comma_join_condition:
    /* empty */ {
      $$ = nullptr;
    }
    | ON join_condition_list {
      $$ = $2;
    }
    ;
join_condition_list:
    expression comp_op expression {
      $$ = new vector<ConditionSqlNode>;
      ConditionSqlNode condition;
      condition.comp = $2;
      condition.left_expr.reset($1);
      condition.right_expr.reset($3);
      $$->emplace_back(std::move(condition));
    }
    | expression comp_op expression AND join_condition_list {
      $$ = $5;
      ConditionSqlNode condition;
      condition.comp = $2;
      condition.left_expr.reset($1);
      condition.right_expr.reset($3);
      $$->insert($$->begin(), std::move(condition));
    }
    ;
relation:
    ID {
      $$ = $1;
    }
    ;
rel_list:
    relation {
      $$ = new vector<string>();
      $$->push_back($1);

    }
    | relation COMMA rel_list {  /* 多表查询 */
      if ($3 != nullptr) {
        $$ = $3;
      } else {
        $$ = new vector<string>;
      }
      $$->insert($$->begin(), $1);
    }
    ;
where:
    /* empty */
    {
      $$ = nullptr;
    }
    | WHERE condition_list {
      $$ = $2;
    }
    ;
condition_list:
    /* empty */
    {
      $$ = nullptr;
    }
    | condition {
      $$ = new vector<ConditionSqlNode>; /* NOTE 使用vector是因为有AND情况 */
      $$->emplace_back(std::move(*$1));

      delete $1;
    }
    | condition AND condition_list {
      $$ = $3;
      $$->emplace_back(std::move(*$1));
      delete $1;
    }
    ;
condition:
    /* NOTE 为了能处理where里面有算数表达式的情况，这里要加入 expression_list, 同时也解决了 table.attribution comp_op 5+3-1
     * NOTE expression_list 里面包括了value, Expression 的返回类型是基类 Expression的指针
     * 单独这一个就可以支持所有情况了，返回值依旧是 ConditionSqlNode*
    */
    expression_list comp_op expression_list
    {
      $$ = new ConditionSqlNode;
      $$->comp = $2;
      // 建议增加校验
      if ((*$1).size() != 1) {
        // 语法错误：WHERE后面只能是单个表达式
        yyerror(&@$, sql_string, nullptr, scanner, "condition expression must be single expression");
        delete $1;
        YYERROR;
      }
      $$->left_expr=std::move((*$1)[0]);
      if ((*$3).size() != 1) {
        yyerror(&@$, sql_string, nullptr, scanner, "condition expression must be single expression");
        delete $3;
        YYERROR;
      }
      $$->right_expr=std::move((*$3)[0]);
      delete $1; // 移动当个节点的所有权不会释放vector
      delete $3;
    }
    /* 新增：支持 WHERE expression 语法，隐式当作 expression != 0 */
    | expression_list
    {
      $$ = new ConditionSqlNode;
      $$->comp = CompOp::NOT_EQUAL;
      if ((*$1).size()==1) {
        $$->left_expr = std::move((*$1)[0]);
      }
      // 创建一个值为 0 的常量表达式作为右操作数
      ValueExpr *zero_expr = new ValueExpr();
      Value zero_val;
      zero_val.set_int(0);
      $$->right_expr = unique_ptr<Expression>(zero_expr);
      delete $1;
    }
    ;

comp_op:
      EQ { $$ = EQUAL_TO; }
    | LT { $$ = LESS_THAN; }
    | GT { $$ = GREAT_THAN; }
    | LE { $$ = LESS_EQUAL; }
    | GE { $$ = GREAT_EQUAL; }
    | NE { $$ = NOT_EQUAL; }
    ;

// your code here
group_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | GROUP BY expression_list
    {
      // group by 的表达式范围与select查询值的表达式范围是不同的，比如group by不支持 *
      // 但是这里没有处理。
      $$ = $3;
    }
    ;
load_data_stmt:
    LOAD DATA INFILE SSS INTO TABLE ID fields_terminated_by enclosed_by
    {
      char *tmp_file_name = common::substr($4, 1, strlen($4) - 2);
      
      $$ = new ParsedSqlNode(SCF_LOAD_DATA);
      $$->load_data.relation_name = $7;
      $$->load_data.file_name = tmp_file_name;
      if ($8 != nullptr) {
        char *tmp = common::substr($8,1,strlen($8)-2);
        $$->load_data.terminated = $8;
        free(tmp);
      }
      if ($9 != nullptr) {
        char *tmp = common::substr($9,1,strlen($9)-2);
        $$->load_data.enclosed = $9;
        free(tmp);
      }
      free(tmp_file_name);

    }
    ;

fields_terminated_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | FIELDS TERMINATED BY SSS
    {
      $$ = $4;
    };

enclosed_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | ENCLOSED BY SSS
    {
      $$ = $3;
    };

explain_stmt:
    EXPLAIN command_wrapper
    {
      $$ = new ParsedSqlNode(SCF_EXPLAIN);
      $$->explain.sql_node = unique_ptr<ParsedSqlNode>($2);

    }
    ;

set_variable_stmt:
    SET ID EQ value
    {
      $$ = new ParsedSqlNode(SCF_SET_VARIABLE);
      $$->set_variable.name  = $2;
      $$->set_variable.value = *$4;

      delete $4;
    }
    ;

opt_semicolon: /*empty*/
    | SEMICOLON
    ;
%%
//_____________________________________________________________________
extern void scan_string(const char *str, yyscan_t scanner);

int sql_parse(const char *s, ParsedSqlResult *sql_result) {
  yyscan_t scanner;
  std::vector<char *> allocated_strings;
  yylex_init_extra(static_cast<void*>(&allocated_strings),&scanner);
  scan_string(s, scanner);
  int result = yyparse(s, sql_result, scanner);

  for (char *ptr : allocated_strings) {
    free(ptr);
  }
  allocated_strings.clear();

  yylex_destroy(scanner);
  return result;
}