/*
 * Copyright 2015 Rockchip Electronics Co. LTD
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

#ifndef __MPP_TRIE_H__
#define __MPP_TRIE_H__

#include "rk_type.h"
#include "mpp_err.h"

typedef void* MppTrie;

/* spatial optimized tire tree */
typedef struct MppAcNode_t {
    RK_S16          next[16];
    /* idx - tire node index in ascending order */
    RK_S32          idx;
    /* id  - tire node carried payload data */
    RK_S32          id;
} MppTrieNode;

#ifdef __cplusplus
extern "C" {
#endif

MPP_RET mpp_trie_init(MppTrie *trie, RK_S32 node_count, RK_S32 info_count);
MPP_RET mpp_trie_deinit(MppTrie trie);

MPP_RET mpp_trie_add_info(MppTrie trie, const char **info);
RK_S32 mpp_trie_get_node_count(MppTrie trie);
RK_S32 mpp_trie_get_info_count(MppTrie trie);

MppTrieNode *mpp_trie_get_node(MppTrieNode *root, const char *name);
const char **mpp_trie_get_info(MppTrie trie, const char *name);
MppTrieNode *mpp_trie_node_root(MppTrie trie);

#ifdef __cplusplus
}
#endif

#endif /*__MPP_TRIE_H__*/
