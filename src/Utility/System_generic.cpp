/*
 * This file is part of QBDI.
 *
 * Copyright 2017 - 2025 Quarkslab
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <algorithm>
#include <stddef.h>
#include <stdlib.h>
#include <string>
#include <system_error>

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Memory.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"

#include "QBDI/Config.h"
#include "QBDI/Platform.h"
#include "Utility/LogSys.h"
#include "Utility/System.h"

namespace QBDI {

bool isRWXSupported() { return false; }
bool isRWRXSupported() { return true; }

// GhostHook seam pointers (default null => native llvm mmap path).
void *(*ghostCodeAlloc)(size_t numBytes, unsigned pFlags) = nullptr;
void (*ghostCodeFree)(void *base) = nullptr;

llvm::sys::MemoryBlock allocateMappedMemory(size_t numBytes, const llvm::sys::MemoryBlock *const nearBlock,
                     unsigned pFlags, std::error_code &ec) {
  // GhostHook: route the JIT code-cache block into host memory (ghost memory,
  // VMA-less). The allocator returns RWX regardless of pFlags (W^X flips are
  // short-circuited in ExecBlock::makeRX/makeRW).
  if (ghostCodeAlloc != nullptr) {
    void *base = ghostCodeAlloc(numBytes, pFlags);
    if (base == nullptr) {
      ec = std::make_error_code(std::errc::not_enough_memory);
      return {};
    }
    ec = std::error_code();
    return llvm::sys::MemoryBlock(base, numBytes);
  }
  // forward to llvm function
  return llvm::sys::Memory::allocateMappedMemory(numBytes, nearBlock, pFlags,
                                                 ec);
}

void releaseMappedMemory(llvm::sys::MemoryBlock &block) {
  if (ghostCodeFree != nullptr) {
    ghostCodeFree(block.base());
    return;
  }
  llvm::sys::Memory::releaseMappedMemory(block);
}

// GhostHook seam setter (exported): host injects the code-cache allocator before
// the first VM is constructed. Passing null,null restores the native path.
extern "C" QBDI_EXPORT
void qbdi_setGhostAllocator(void *(*allocFn)(size_t numBytes, unsigned pFlags), void (*freeFn)(void *base)) {
  ghostCodeAlloc = allocFn;
  ghostCodeFree = freeFn;
}

const std::string getHostCPUName() {
  const std::string cpuname = llvm::sys::getHostCPUName().str();
  // set default ARM CPU
  if constexpr (is_arm)
    if (cpuname.empty() || cpuname == "generic")
      return "cortex-a8";
  return cpuname;
}

const std::vector<std::string> getHostCPUFeatures() {
  std::vector<std::string> mattrs = {};
  llvm::StringMap<bool> features = llvm::sys::getHostCPUFeatures();

  bool ret = !features.empty();

  if (!ret) {
    QBDI_WARN("Fail to detect CPUHostFeatures");
    features.clear();
  }

  const char *fixupFeatures = getenv("QBDI_FIXUP_FEATURES");
  if (fixupFeatures != nullptr) {
    llvm::SubtargetFeatures addFeatures(fixupFeatures);
    for (const auto &f : addFeatures.getFeatures()) {
      if (llvm::SubtargetFeatures::hasFlag(f)) {
        features[llvm::SubtargetFeatures::StripFlag(f)] =
            llvm::SubtargetFeatures::isEnabled(f);
      } else {
        features[f] = true;
      }
    }
  }

  if (ret || fixupFeatures != nullptr) {
#if defined(_QBDI_FORCE_DISABLE_AVX)
    const char *disable_avx = "1";
#else
    const char *disable_avx = getenv("QBDI_FORCE_DISABLE_AVX");
#endif

#if defined(_QBDI_ASAN_ENABLED_)
    const char *asan_blacklist_feature[] = {
#if defined(QBDI_ARCH_X86_64) || defined(QBDI_ARCH_X86)
        "x87", "fxsr",  "xsave",  "xsaveopt", "xsavec",     "xsaves",
        "sse", "sse2",  "sse3",   "ssse3",    "sse4.1",     "sse4.2",
        "mmx", "3dnow", "3dnowa", "sse4a",    "avx",        "avx2",
        "fma", "f16c",  "pclmul", "gfni",     "vpclmulqdq", "fma4",
        "xop", "aes",   "vaes",   "sha"
#endif
    };
    const size_t asan_blacklist_feature_size =
        sizeof(asan_blacklist_feature) / sizeof(char *);
#endif

    for (const auto &feat : features) {
      if (!feat.getValue()) {
        continue;
      }
      // XXX: #19 Bad AVX support detection in VM environments
      // fix buggy dynamic detection
      if (disable_avx != NULL && feat.getKey() == llvm::StringRef("avx")) {
        continue;
      }
#if defined(_QBDI_ASAN_ENABLED_)
      bool blacklist_feature = false;
      for (int i = 0; i < asan_blacklist_feature_size; i++) {
        if (feat.getKey() == llvm::StringRef(asan_blacklist_feature[i])) {
          blacklist_feature = true;
          break;
        }
      }
      if (blacklist_feature) {
        continue;
      }
#endif
      mattrs.push_back(feat.getKey().str());
    }
  }

  if constexpr (is_arm) {
    // set default ARM CPU
    if (features.size() == 0) {
      features.insert({"fp16", true});
      features.insert({"d16", true});
    }
  }
  // Fixing awfull LLVM API
  if (features.count("fp16") && features["fp16"]) {
    mattrs.emplace_back("vfp2");
  }
  if (features.count("d16") && features["d16"]) {
    mattrs.emplace_back("vfp3");
  }
  return mattrs;
}

bool isHostCPUFeaturePresent(const char *query) {
  std::vector<std::string> features = getHostCPUFeatures();
  for (const std::string &feature : features) {
    if (feature == query) {
      return true;
    }
  }
  return false;
}

} // namespace QBDI
