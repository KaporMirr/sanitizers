// Copyright 2011 Google Inc. All Rights Reserved.
// Author: kcc@google.com (Kostya Serebryany)
/* Copyright 2011 Google Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

// This file is a part of AddressSanitizer, an address sanity checker.

#include "asan_stack.h"
#include "sysinfo.h"
#include "bfd_symbolizer/bfd_symbolizer.h"

#include <string.h>
#include <string>

using std::string;
// ----------------------- ProcSelfMaps ----------------------------- {{{1
class ProcSelfMaps {
 public:
  void Init() {
    ProcMapsIterator it(0, &proc_self_maps_);   // 0 means "current pid"

    uint64 start, end, offset;
    int64 inode;
    char *flags, *filename;
    map_size_ = 0;
    while (it.Next(&start, &end, &flags, &offset, &inode, &filename)) {
      CHECK(map_size_ < kMaxProcSelfMapsSize);
      Mapping &mapping = memory_map[map_size_];
      mapping.beg = start;
      mapping.end = end;
      mapping.name_beg = filename;
      map_size_++;
    }
  }

  void Print() {
    Printf("%s\n", proc_self_maps_);
  }

  void FilterOutAsanRtlFileName(char file_name[]) {
    if (strstr(file_name, "asan_rtl.cc")) {
      strcpy(file_name,   "_asan_rtl_");
    }
  }

  void PrintPc(uintptr_t pc, int idx) {
    const int kLen = 1024;
#ifndef __APPLE__
    char func[kLen+1] = "",
         file[kLen+1] = "",
         module[kLen+1] = "";
    int line = 0;
    int offset = 0;

    if (__asan_flag_symbolize) {
      __asan_need_real_malloc = true;
      int opt = bfds_opt_none;
      if (idx == 0)
        opt |= bfds_opt_update_libs;
      int demangle = __asan_flag_demangle;
      if (demangle == 1) opt |= bfds_opt_demangle;
      if (demangle == 2) opt |= bfds_opt_demangle_params;
      if (demangle == 3) opt |= bfds_opt_demangle_verbose;
      int res = bfds_symbolize((void*)pc,
                               (bfds_opts_e)opt,
                               func, kLen,
                               module, kLen,
                               file, kLen,
                               &line,
                               &offset);
      __asan_need_real_malloc = false;
      if (res == 0) {
        FilterOutAsanRtlFileName(file);
        Printf("    #%d 0x%lx in %s %s:%d\n", idx, pc, func, file, line);
        return;
      }
      // bfd failed
    }
#endif

    for (size_t i = 0; i < map_size_; i++) {
      Mapping &m = memory_map[i];
      if (pc >= m.beg && pc < m.end) {
        char buff[kLen + 1];
        uintptr_t offset = pc - m.beg;
        if (i == 0) offset = pc;
        copy_until_new_line(m.name_beg, buff, kLen);
        Printf("    #%d 0x%lx (%s+0x%lx)\n", idx, pc, buff, offset);
        return;
      }
    }
    Printf("  #%d 0x%lx\n", idx, pc);
  }

 private:
  void copy_until_new_line(const char *str, char *dest, size_t max_size) {
    size_t i = 0;
    for (; str[i] && str[i] != '\n' && i < max_size - 1; i++){
      dest[i] = str[i];
    }
    dest[i] = 0;
  }


  struct Mapping {
    uintptr_t beg, end;
    const char *name_beg;
  };
  static const size_t kMaxNumMapEntries = 4096;
  static const size_t kMaxProcSelfMapsSize = 1 << 20;
  ProcMapsIterator::Buffer proc_self_maps_;
  size_t map_size_;
  Mapping memory_map[kMaxNumMapEntries];
};

static ProcSelfMaps proc_self_maps;

// ----------------------- AsanStackTrace ----------------------------- {{{1

void AsanStackTrace::PrintStack(uintptr_t *addr, size_t size) {
  for (size_t i = 0; i < size && addr[i]; i++) {
    uintptr_t pc = addr[i];
    string img, rtn, file;
    // int line;
    // PcToStrings(pc, true, &img, &rtn, &file, &line);
    proc_self_maps.PrintPc(pc, i);
    // Printf("  #%ld 0x%lx %s\n", i, pc, rtn.c_str());
    if (rtn == "main()") break;
  }
}

void AsanStackTrace::Init() {
  proc_self_maps.Init();
}