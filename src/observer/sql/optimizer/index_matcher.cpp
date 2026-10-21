/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of the Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY IMPLIED WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the Mulan PSL v2 for more details. */

#include "sql/optimizer/index_matcher.h"

#include "common/log/log.h"
#include "common/type/data_type.h"
#include "sql/expr/expression.h"
#include "storage/index/index.h"
#include "storage/table/table.h"

namespace {

/// 一个字段上的比较谓词
struct ColumnPredicate
{
  CompOp comp = EQUAL_TO;
  Value    value;
};

/// 范围条件的一侧
struct RangeBound
{
  bool  has_value = false;
  Value value;
  bool  inclusive = true;
};

/// 某个索引的匹配结果
struct IndexMatch
{
  Index        *index        = nullptr;
  int           equal_prefix = 0; ///< 开头连续等值匹配的列数
  int           matched      = 0; ///< 能用上的前缀列数
  bool          has_range    = false;
  vector<Value> equal_values;     ///< 前 equal_prefix 列的等值
  RangeBound    lower;             ///< 范围列的下界
  RangeBound    upper;             ///< 范围列的上界
};

/// 构造边界键时每一列的内容
struct KeyColumn
{
  bool  has_value = false; ///< 是否有具体的值
  Value value;             ///< 具体的值
  bool  use_max = false;   ///< 没有具体值时，取该类型的最大值还是最小值
};

/// 把比较表达式拆成"字段 + 常量"的形式。拆分不出来返回 false
bool split_comparison(ComparisonExpr *comparison_expr, FieldExpr *&field_expr, ValueExpr *&value_expr)
{
  unique_ptr<Expression> &left_expr  = comparison_expr->left();
  unique_ptr<Expression> &right_expr = comparison_expr->right();

  // 左右比较的一边必须是一个值
  if (left_expr->type() != ExprType::VALUE && right_expr->type() != ExprType::VALUE) {
    return false;
  }
  if (left_expr->type() == ExprType::FIELD) {
    if (right_expr->type() != ExprType::VALUE) {
      return false;
    }
    field_expr = static_cast<FieldExpr *>(left_expr.get());
    value_expr = static_cast<ValueExpr *>(right_expr.get());
  } else if (right_expr->type() == ExprType::FIELD) {
    if (left_expr->type() != ExprType::VALUE) {
      return false;
    }
    field_expr = static_cast<FieldExpr *>(right_expr.get());
    value_expr = static_cast<ValueExpr *>(left_expr.get());
  } else {
    return false;
  }

  return field_expr != nullptr && value_expr != nullptr;
}

/// 收集每个字段上的比较谓词
void collect_predicates(vector<unique_ptr<Expression>> &predicates, map<string, vector<ColumnPredicate>> &field_predicates)
{
  for (auto &expr : predicates) {
    if (expr->type() != ExprType::COMPARISON) {
      continue;
    }
    auto comparison_expr = static_cast<ComparisonExpr *>(expr.get());

    FieldExpr *field_expr = nullptr;
    ValueExpr *value_expr = nullptr;
    if (!split_comparison(comparison_expr, field_expr, value_expr)) {
      continue;
    }

    ColumnPredicate predicate;
    predicate.comp = comparison_expr->comp();
    predicate.value = value_expr->get_value();
    field_predicates[field_expr->field().field_name()].push_back(predicate);
  }
}

/// 用一个索引去匹配谓词，遵循最左前缀规则
bool match_index(Index *index, const map<string, vector<ColumnPredicate>> &field_predicates, IndexMatch &match)
{
  const vector<string> &fields = index->index_meta().fields();

  match.index        = index;
  match.equal_prefix = 0;
  match.matched      = 0;
  match.has_range    = false;

  for (size_t i = 0; i < fields.size(); i++) {
    auto it = field_predicates.find(fields[i]);
    if (it == field_predicates.end()) {
      // 这一列没有谓词，左前缀断了
      break;
    }
    const vector<ColumnPredicate> &predicates_of_field = it->second;

    // != 无法用单次范围扫描表达，这一列不能用来构造边界
    bool has_not_equal = false;
    for (const ColumnPredicate &predicate : predicates_of_field) {
      if (predicate.comp == NOT_EQUAL) {
        has_not_equal = true;
        break;
      }
    }
    if (has_not_equal) {
      break;
    }

    // 优先找等值谓词
    const ColumnPredicate *equal_predicate = nullptr;
    for (const ColumnPredicate &predicate : predicates_of_field) {
      if (predicate.comp != EQUAL_TO) {
        continue;
      }
      if (equal_predicate != nullptr && equal_predicate->value != predicate.value) {
        // 同一个字段上有两个不同的等值条件(AND)，是矛盾的，这个索引用不了
        return false;
      }
      equal_predicate = &predicate;
    }

    if (equal_predicate != nullptr) {
      match.equal_values.push_back(equal_predicate->value);
      match.equal_prefix++;
      match.matched = i + 1;
      continue;
    }

    // 没有等值条件，找范围条件
    for (const ColumnPredicate &predicate : predicates_of_field) {
      RangeBound bound;
      bound.has_value = true;
      bound.value     = predicate.value;
      switch (predicate.comp) {
        case GREAT_EQUAL:
        case GREAT_THAN: {
          bound.inclusive = (predicate.comp == GREAT_EQUAL);
          // 下界取最大的那个值
          if (!match.lower.has_value || match.lower.value < bound.value ||
              (match.lower.value == bound.value && !bound.inclusive)) {
            match.lower = bound;
          }
        } break;
        case LESS_EQUAL:
        case LESS_THAN: {
          bound.inclusive = (predicate.comp == LESS_EQUAL);
          // 上界取最小的那个值
          if (!match.upper.has_value || match.upper.value > bound.value ||
              (match.upper.value == bound.value && !bound.inclusive)) {
            match.upper = bound;
          }
        } break;
        default: break;
      }
    }

    if (match.lower.has_value || match.upper.has_value) {
      match.has_range = true;
      match.matched   = i + 1;
    }

    // 范围列之后不能再有约束
    break;
  }

  return match.matched > 0;
}

/// 比较两个匹配结果，优先等值前缀更长的，其次可用前缀更长的，最后列数更少的
bool better_match(const IndexMatch &candidate, const IndexMatch &current)
{
  if (candidate.equal_prefix != current.equal_prefix) {
    return candidate.equal_prefix > current.equal_prefix;
  }
  if (candidate.matched != current.matched) {
    return candidate.matched > current.matched;
  }
  // 列数少的边界更紧，优先选它
  return candidate.index->index_meta().fields().size() < current.index->index_meta().fields().size();
}

/// 取索引各个列在表里的元数据
RC collect_field_metas(Table *table, Index *index, vector<FieldMeta> &field_metas)
{
  const vector<string> &fields = index->index_meta().fields();
  field_metas.clear();
  field_metas.reserve(fields.size());
  for (const string &field : fields) {
    const FieldMeta *field_meta = table->table_meta().field(field.c_str());
    if (nullptr == field_meta) {
      LOG_WARN("index %s refers to a non-exists field %s", index->index_meta().name(), field.c_str());
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
    field_metas.push_back(*field_meta);
  }
  return RC::SUCCESS;
}

/// 把各列的内容拼成完整长度的索引键
RC build_key(const vector<FieldMeta> &field_metas, const vector<KeyColumn> &columns, bool want_greater, vector<char> &key)
{
  int total_length = 0;
  for (const FieldMeta &field_meta : field_metas) {
    total_length += field_meta.len();
  }

  key.assign(total_length, 0);
  int offset = 0;
  for (size_t i = 0; i < field_metas.size(); i++) {
    const FieldMeta &field_meta = field_metas[i];
    if (columns[i].has_value) {
      bool adjusted = false;
      RC   rc = Value::serialize_to_key(
          columns[i].value, field_meta.type(), field_meta.len(), key.data() + offset, want_greater, &adjusted);
      if (OB_FAIL(rc)) {
        return rc;
      }
      if (adjusted) {
        // 等值条件上的值比列还长，截断之后语义就不对了，这个索引用不了
        return RC::VALUE_TOO_LONG;
      }
    } else {
      Value::fill_extreme(field_meta.type(), field_meta.len(), key.data() + offset, columns[i].use_max);
    }
    offset += field_meta.len();
  }

  return RC::SUCCESS;
}


/// 比较两个索引键，语义与 B+树层的 AttrComparator 一致
int compare_keys(const vector<FieldMeta> &field_metas, const vector<char> &left, const vector<char> &right)
{
  int offset = 0;
  for (const FieldMeta &field_meta : field_metas) {
    Value left_value;
    left_value.set_type(field_meta.type());
    left_value.set_data(left.data() + offset, field_meta.len());
    Value right_value;
    right_value.set_type(field_meta.type());
    right_value.set_data(right.data() + offset, field_meta.len());
    int result = DataType::type_instance(field_meta.type())->compare(left_value, right_value);
    if (result != 0) {
      return result;
    }
    offset += field_meta.len();
  }
  return 0;
}

}  // namespace

RC IndexMatcher::match(Table *table, vector<unique_ptr<Expression>> &predicates, IndexScanPlan &plan)
{
  plan = IndexScanPlan();

  // 1. 收集每个字段上的谓词
  map<string, vector<ColumnPredicate>> field_predicates;
  collect_predicates(predicates, field_predicates);
  if (field_predicates.empty()) {
    return RC::SUCCESS;
  }

  // 2. 用最左前缀规则给每个候选索引打分
  vector<Index *> indexes;
  table->find_all_index(indexes);

  IndexMatch best_match;
  bool        found = false;
  for (Index *index : indexes) {
    IndexMatch candidate;
    if (!match_index(index, field_predicates, candidate)) {
      continue;
    }
    if (!found || better_match(candidate, best_match)) {
      best_match = candidate;
      found      = true;
    }
  }

  if (!found) {
    // 没有可用的索引，走全表扫描
    return RC::SUCCESS;
  }

  // 3. 取出索引各个列的元数据
  vector<FieldMeta> field_metas;
  RC                rc = collect_field_metas(table, best_match.index, field_metas);
  if (OB_FAIL(rc)) {
    return rc;
  }

  const size_t column_num = field_metas.size();
  const size_t equal_num  = best_match.equal_prefix;
  // 第一个没有约束的列。等值列和范围列都是有约束的，不能动
  const size_t range_index = best_match.has_range ? (equal_num + 1) : equal_num;

  // 4. 构造左边界
  //    等值列填等值，范围列填下界，后面无界的列填该类型的最小值。
  //    如果下界是开区间(>)，后面无界的列要填最大值，这样扫描才能把
  //    "等于下界"的那些记录排除掉。
  vector<char> left_key;
  bool        left_inclusive = true;
  if (!best_match.has_range || best_match.lower.has_value) {
    vector<KeyColumn> columns(column_num);
    for (size_t i = 0; i < equal_num; i++) {
      columns[i].has_value = true;
      columns[i].value     = best_match.equal_values[i];
    }
    if (best_match.has_range) {
      columns[equal_num].has_value = true;
      columns[equal_num].value     = best_match.lower.value;
      left_inclusive                = best_match.lower.inclusive;
    }
    for (size_t i = range_index; i < column_num; i++) {
      columns[i].has_value = false;
      columns[i].use_max   = !left_inclusive;
    }

    rc = build_key(field_metas, columns, true /*want_greater*/, left_key);
    if (OB_FAIL(rc)) {
      // 边界值构造不出来，退化成全表扫描
      LOG_WARN("failed to build left index key. index=%s, rc=%s", best_match.index->index_meta().name(), strrc(rc));
      plan.index = nullptr;
      return RC::SUCCESS;
    }
  }

  // 5. 构造右边界
  //    规则和左边界对称：等值列填等值，范围列填上界，后面无界的列填该类型的最大值。
  //    如果上界是开区间(<)，后面无界的列要填最小值。
  vector<char> right_key;
  bool        right_inclusive = true;
  if (!best_match.has_range || best_match.upper.has_value) {
    vector<KeyColumn> columns(column_num);
    for (size_t i = 0; i < equal_num; i++) {
      columns[i].has_value = true;
      columns[i].value     = best_match.equal_values[i];
    }
    if (best_match.has_range) {
      columns[equal_num].has_value = true;
      columns[equal_num].value     = best_match.upper.value;
      right_inclusive               = best_match.upper.inclusive;
    }
    for (size_t i = range_index; i < column_num; i++) {
      columns[i].has_value = false;
      columns[i].use_max   = right_inclusive;
    }

    rc = build_key(field_metas, columns, false /*want_greater*/, right_key);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to build right index key. index=%s, rc=%s", best_match.index->index_meta().name(), strrc(rc));
      plan.index = nullptr;
      return RC::SUCCESS;
    }
  }

  // 边界不合法(左边界比右边界还大)说明这个范围是空的，退化成全表扫描。
  // 全表扫描的过滤器会正确地处理这个谓词，不会漏数据。
  if (!left_key.empty() && !right_key.empty() && compare_keys(field_metas, left_key, right_key) > 0) {
    LOG_WARN("index scan range is empty. index=%s", best_match.index->index_meta().name());
    plan.index = nullptr;
    return RC::SUCCESS;
  }

  plan.index           = best_match.index;
  plan.left_key        = std::move(left_key);
  plan.left_inclusive  = left_inclusive;
  plan.right_key       = std::move(right_key);
  plan.right_inclusive = right_inclusive;
  return RC::SUCCESS;
}
