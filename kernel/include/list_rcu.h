/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <list.h>
#include <rcu.h>

/**
 * 遍历链表
 *
 * @param pos 当前节点指针
 * @param head 链表头
 *
 * 必须在 rcu_read_lock() 内使用，遍历期间不可睡眠。
 */
#define list_for_each_rcu(pos, head)                  \
    for (pos = rcu_dereference((head)->next);         \
         pos != (head);                               \
         pos = rcu_dereference(pos->next))

/**
 * 遍历链表并获取包含结构体
 *
 * @param pos 包含结构体指针
 * @param head 链表头
 * @param member 链表节点在结构体中的成员名
 *
 * 必须在 rcu_read_lock() 内使用，遍历期间不可睡眠。
 */
#define list_for_each_entry_rcu(pos, head, member)                 \
    for (pos = list_entry(rcu_dereference((head)->next),           \
                          typeof(*pos), member);                   \
         &pos->member != (head);                                   \
         pos = list_entry(rcu_dereference(pos->member.next),       \
                          typeof(*pos), member))

/**
 * 在头部添加节点
 *
 * @param n 新节点
 * @param head 链表头
 */
static inline void list_add_rcu(
    struct list_head *n,
    struct list_head *head
) {
    struct list_head *next = rcu_dereference(head->next);
    n->next = next;
    n->prev = head;
    rcu_assign_pointer(head->next, n);
    next->prev = n;
}

/**
 * 在尾部添加节点
 *
 * @param n 新节点
 * @param head 链表头
 */
static inline void list_add_tail_rcu(
    struct list_head *n,
    struct list_head *head
) {
    struct list_head *prev = rcu_dereference(head->prev);
    n->next = head;
    n->prev = prev;
    rcu_assign_pointer(prev->next, n);
    head->prev = n;
}

/**
 * 删除节点
 *
 * @param n 要删除的节点
 *
 * 删除后节点内存不可立即释放，需等待 RCU 宽限期结束。
 */
static inline void list_del_rcu(struct list_head *n) {
    struct list_head *prev = rcu_dereference(n->prev);
    struct list_head *next = rcu_dereference(n->next);
    rcu_assign_pointer(prev->next, next);
    next->prev = prev;
}

/**
 * 替换节点
 *
 * @param old 被替换的节点
 * @param new 新节点
 *
 * 替换后 old 节点不可立即释放，需等待 RCU 宽限期结束。
 */
static inline void list_replace_rcu(
    struct list_head *old,
    struct list_head *new
) {
    struct list_head *prev = rcu_dereference(old->prev);
    struct list_head *next = rcu_dereference(old->next);
    new->next = next;
    new->prev = prev;
    rcu_assign_pointer(prev->next, new);
    next->prev = new;
}

/**
 * 遍历哈希链表
 *
 * @param pos 当前节点指针（struct hlist_node *）
 * @param head 哈希链表头
 *
 * 必须在 rcu_read_lock() 内使用，遍历期间不可睡眠。
 */
#define hlist_for_each_rcu(pos, head)                    \
    for (pos = rcu_dereference((head)->first);           \
         pos;                                            \
         pos = rcu_dereference(pos->next))

/**
 * 遍历哈希链表并获取包含结构体
 *
 * @param pos 包含结构体指针
 * @param head 哈希链表头
 * @param member hlist_node 在结构体中的成员名
 *
 * 必须在 rcu_read_lock() 内使用，遍历期间不可睡眠。
 */
#define hlist_for_each_entry_rcu(pos, head, member)                \
    for (pos = hlist_entry_safe(rcu_dereference((head)->first),    \
                                typeof(*pos), member);             \
         pos;                                                      \
         pos = hlist_entry_safe(rcu_dereference(pos->member.next), \
                                typeof(*pos), member))

/**
 * 在头部添加节点
 *
 * @param n 新节点
 * @param h 哈希链表头
 */
static inline void hlist_add_head_rcu(
    struct hlist_node *n,
    struct hlist_head *h
) {
    struct hlist_node *first = rcu_dereference(h->first);
    n->next = first;
    n->pprev = &h->first;
    if (first) {
        first->pprev = &n->next;
    }
    rcu_assign_pointer(h->first, n);
}

/**
 * 在指定节点之前添加节点
 *
 * @param n 新节点
 * @param next 参考节点
 */
static inline void hlist_add_before_rcu(
    struct hlist_node *n,
    struct hlist_node *next
) {
    n->pprev = next->pprev;
    n->next = next;
    rcu_assign_pointer(*(next->pprev), n);
    next->pprev = &n->next;
}

/**
 * 在指定节点之后添加节点
 *
 * @param n 新节点
 * @param prev 参考节点
 */
static inline void hlist_add_behind_rcu(
    struct hlist_node *n,
    struct hlist_node *prev
) {
    struct hlist_node *next = rcu_dereference(prev->next);
    n->next = next;
    n->pprev = &prev->next;
    rcu_assign_pointer(prev->next, n);
    if (next) {
        next->pprev = &n->next;
    }
}

/**
 * 删除节点
 *
 * @param n 要删除的节点
 *
 * 删除后节点内存不可立即释放，需等待 RCU 宽限期结束。
 */
static inline void hlist_del_rcu(struct hlist_node *n) {
    struct hlist_node *next = rcu_dereference(n->next);
    struct hlist_node **pprev = n->pprev;
    rcu_assign_pointer(*pprev, next);
    if (next) {
        next->pprev = pprev;
    }
}