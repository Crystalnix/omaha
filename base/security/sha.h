// Copyright 2005-2009 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ========================================================================

#ifndef OMAHA_COMMON_SECURITY_SHA1_H__
#define OMAHA_COMMON_SECURITY_SHA1_H__

#include <inttypes.h>
#include "hash-internal.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef HASH_CTX SHA_CTX;

void SHA_init(SHA_CTX* ctx);
void SHA_update(SHA_CTX* ctx, const void* data, int len);
const uint8_t* SHA_final(SHA_CTX* ctx);

// Convenience method. Returns digest address.
const uint8_t* SHA(const void* data, int len, uint8_t* digest);

#define SHA_DIGEST_SIZE 20

#ifdef __cplusplus
}
#endif // __cplusplus

#endif  // OMAHA_COMMON_SECURITY_SHA1_H__
