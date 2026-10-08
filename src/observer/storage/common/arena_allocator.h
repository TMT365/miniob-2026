/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
* 核心思想：块式内存池（Arena 内存池）
* 一次性向操作系统申请比较大的内存块 (block)，内部切分、分段分配，不单独释放小块。
* Arena 的核心特性：不支持单独 free 某一次 Allocate 出来的内存；只有 Arena 对象析构的时候，一次性释放全部 blocks_里所有内存。
* 非常适合向量化执行：Chunk/Column 生命周期内大量临时字符串，Chunk 销毁时一次性回收全部内存。
 */
class Arena
{
public:
  Arena();

  Arena(const Arena &)            = delete;
  Arena &operator=(const Arena &) = delete;

  ~Arena();

  // 传递一个大小字节数，返回分配出来的地址
  char *Allocate(size_t bytes);

  // 带内存对齐的分配，满足malloc对齐要求
  char *AllocateAligned(size_t bytes);

  // 统计已分配总内存大小（原子变量，线程安全读取）
  size_t MemoryUsage() const { return memory_usage_.load(std::memory_order_relaxed); }

private:
  /**
   * 内部还是调用char *AllocateNewBlock(size_t block_bytes)但是处理了属性的更新
   * @param bytes 要分配的字节大小
   * @return 分配完后的首地址
   */
  char *AllocateFallback(size_t bytes);
  char *AllocateNewBlock(size_t block_bytes);

  // Allocation state
  // NOTE 当前正在使用的block的分配指针 & 剩余字节，这个alloc_ptr是指向下一个可以使用的地址，不是刚刚分配的
  char  *alloc_ptr_;
  size_t alloc_bytes_remaining_;  ///< 因为一次分配的空间不可能都使用完，所以要加一个剩余可利用空间

  // 保存所有向new[]申请的大块内存块
  std::vector<char *> blocks_;

  // Total memory usage of the arena.
  //
  // TODO(costan): This member is accessed via atomics, but the others are
  //               accessed without any locking. Is this OK?
  std::atomic<size_t> memory_usage_;
};

inline char *Arena::Allocate(size_t bytes)
{
  assert(bytes > 0);
  // 还能够继续分配就持续利用，提高利用率
  if (bytes <= alloc_bytes_remaining_) {
    char *result = alloc_ptr_;
    alloc_ptr_ += bytes;
    alloc_bytes_remaining_ -= bytes;
    return result;
  }
  return AllocateFallback(bytes);  /// 如果现有块的空间不够了，就要去重新去扩张块空间
}
