#!/bin/bash
# 生成 SQL 解析器文件：lex_sql.cpp 和 yacc_sql.cpp/yacc_sql.hpp

set -e

cd "$(dirname "$0")"

echo "Generating parser files..."

# 使用 flex 生成 lex_sql.cpp
echo "Running flex on lex_sql.l..."
flex -i -o lex_sql.cpp lex_sql.l

# 使用 bison 生成 yacc_sql.cpp 和 yacc_sql.hpp
echo "Running bison on yacc_sql.y..."
bison -d -y -o yacc_sql.cpp yacc_sql.y

echo "Done. Generated files:"
echo "  - lex_sql.cpp"
echo "  - yacc_sql.cpp"
echo "  - yacc_sql.hpp"
