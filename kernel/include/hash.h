/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <list.h>

// 哈希函数类型：输入键指针，返回 32 位哈希值
typedef uint32_t (*hash_fn)(const void *key);

// 键比较函数类型：相等返回 true，否则返回 false
typedef bool (*key_eq_fn)(const void *key1, const void *key2);

// 哈希表控制结构
struct hash_table {
    struct hlist_head *buckets;
    size_t bucket_count;    // 桶数量，必须为 2 的幂
    hash_fn hash;
    key_eq_fn eq;
};

/**
 * 初始化哈希表
 *
 * @param ht 哈希表指针
 * @param buckets 预先分配好的桶数组
 * @param bucket_count 桶数量，必须为 2 的幂
 * @param hash 哈希计算函数
 * @param eq 键比较函数
 */
static inline void hash_init(
    struct hash_table *ht,
    struct hlist_head *buckets,
    size_t bucket_count,
    hash_fn hash,
    key_eq_fn eq
) {
    ht->buckets = buckets;
    ht->bucket_count = bucket_count;
    ht->hash = hash;
    ht->eq = eq;

    for (size_t i = 0; i < bucket_count; i++) {
        INIT_HLIST_HEAD(&ht->buckets[i]);
    }
}

// 计算键对应的桶索引
static inline size_t hash_bucket_index(
    const struct hash_table *ht,
    const void *key
) {
    uint32_t h = ht->hash(key);
    return h & (ht->bucket_count - 1);
}

/**
 * 将节点插入哈希表
 *
 * @param ht 哈希表指针
 * @param node 待插入的 hlist_node
 * @param key 节点的键
 *
 * 调用者需确保键在表中尚不存在
 */
static inline void hash_add(
    struct hash_table *ht,
    struct hlist_node *node,
    const void *key
) {
    size_t idx = hash_bucket_index(ht, key);
    hlist_add_head(node, &ht->buckets[idx]);
}

/**
 * 在哈希表中查找节点
 *
 * @param ht 哈希表指针
 * @param key 待查找的键
 * @param get_key 回调：从 hlist_node 提取键指针
 *
 * @return 失败：NULL
 * @return 成功：hlist_node 指针
 */
static inline struct hlist_node *hash_lookup(
    struct hash_table *ht,
    const void *key,
    const void *(*get_key)(const struct hlist_node *node)
) {
    size_t idx = hash_bucket_index(ht, key);
    struct hlist_node *pos;

    hlist_for_each(pos, &ht->buckets[idx]) {
        const void *node_key = get_key(pos);
        if (ht->eq(key, node_key)) {
            return pos;
        }
    }

    return NULL;
}

/**
 * 从哈希表中删除节点
 *
 * @param node 要删除的 hlist_node
 */
static inline void hash_del(struct hlist_node *node) {
    hlist_del(node);
}

// 遍历哈希表中所有节点
#define hash_for_each(pos, ht) \
    for (size_t _bkt = 0; _bkt < (ht)->bucket_count; _bkt++) \
        hlist_for_each(pos, &(ht)->buckets[_bkt])

// 遍历哈希表中所有节点（安全版本，可删除当前节点）
#define hash_for_each_safe(pos, n, ht) \
    for (size_t _bkt = 0; _bkt < (ht)->bucket_count; _bkt++) \
        hlist_for_each_safe(pos, n, &(ht)->buckets[_bkt])