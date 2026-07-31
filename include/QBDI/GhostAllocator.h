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
#ifndef QBDI_GHOSTALLOCATOR_H_
#define QBDI_GHOSTALLOCATOR_H_

#include <stddef.h>

#include "QBDI/Platform.h"

#ifdef __cplusplus
namespace QBDI {
extern "C" {
#endif

/*! Redirect QBDI's executable code-cache allocation to a host-provided
 *  allocator. GhostHook fork seam — not part of upstream QBDI.
 *
 *  When set, QBDI routes ExecBlock code-cache alloc/free through these
 *  callbacks instead of the native mmap path, letting the host place JIT code
 *  in ghost memory. Pass NULL/NULL to restore the native allocator.
 *
 *  @param[in] allocFn  Allocator returning a block of numBytes. pFlags is
 *                      ignored — RWX is baked in on the QBDI side. NULL resets.
 *  @param[in] freeFn   Deallocator matching allocFn. NULL resets.
 */
QBDI_EXPORT void qbdi_setGhostAllocator(void *(*allocFn)(size_t numBytes,
                                                         unsigned pFlags),
                                        void (*freeFn)(void *base));

#ifdef __cplusplus
}
}
#endif

#endif // QBDI_GHOSTALLOCATOR_H_
