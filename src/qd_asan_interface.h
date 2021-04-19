#ifndef __qd_asan_interface_h__
#define __qd_asan_interface_h__ 1
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/**
 * Defines the ASAN interface file if available. If not, defines stub macros.
 *
 * See #include <sanitizer/asan_interface.h> in Clang for the source.
 */

#if defined(__clang__)
# define QD_HAS_ADDRESS_SANITIZER __has_feature(address_sanitizer)
#elif defined (__GNUC__) && defined(__SANITIZE_ADDRESS__)
# define QD_HAS_ADDRESS_SANITIZER __SANITIZE_ADDRESS__
#else
# define QD_HAS_ADDRESS_SANITIZER 0
#endif

#if QD_HAS_ADDRESS_SANITIZER

void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);

/// Marks a memory region as unaddressable.
///
/// \note Macro provided for convenience; defined as a no-op if ASan is not
/// enabled.
///
/// \param addr Start of memory region.
/// \param size Size of memory region.
#define ASAN_POISON_MEMORY_REGION(addr, size) \
  __asan_poison_memory_region((addr), (size))

/// Marks a memory region as addressable.
///
/// \note Macro provided for convenience; defined as a no-op if ASan is not
/// enabled.
///
/// \param addr Start of memory region.
/// \param size Size of memory region.
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
  __asan_unpoison_memory_region((addr), (size))

#else  // QD_HAS_ADDRESS_SANITIZER

#define ASAN_POISON_MEMORY_REGION(addr, size) \
  ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
  ((void)(addr), (void)(size))

#endif  // QD_HAS_ADDRESS_SANITIZER

// https://github.com/google/sanitizers/wiki/AddressSanitizer#turning-off-instrumentation

#if QD_HAS_ADDRESS_SANITIZER
# define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#else
# define ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif  // QD_HAS_ADDRESS_SANITIZER

#endif  // __qd_asan_interface_h__
