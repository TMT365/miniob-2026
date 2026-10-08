#pragma once

#include "storage/common/arena_allocator.h"
#include "common/type/string_t.h"
#include "common/log/log.h"

/**
 * VectorBuffer 是一个用来存储变长字符串的对象，使用Arena对象管理内存
 */
class VectorBuffer
{
public:
  VectorBuffer() = default;

  /**
   *
   * @param data 从记录的列里面取出的变长记录首地址
   * @param len 字符串的长度
   * @return 新的字符对象
   */
  string_t add_string(const char *data, int len)
  {
    // string_t里面会处理这个大小问题，这里其实没有必要了
    if (len <= string_t::INLINE_LENGTH) {
      return string_t(data, len);
    }
    auto insert_string = empty_string(len);
    auto insert_pos    = insert_string.get_data_writeable();
    memcpy(insert_pos, data, len);
    return insert_string;
  }
  // 把一个string_t的对象增加到现有的内存空间里面，如果空间不够就像Arena申请一块空的
  string_t add_string(string_t data) { return add_string(data.data(), data.size()); }

  // 申请一块空的新内存
  string_t empty_string(int len)
  {
    ASSERT(len > string_t::INLINE_LENGTH, "len > string_t::INLINE_LENGTH");
    auto insert_pos = heap.Allocate(len);
    return string_t(insert_pos, len);
  }

private:
  // 使用Arena管理这个可变长容器的内存空间，便于管理。
  Arena heap;
};