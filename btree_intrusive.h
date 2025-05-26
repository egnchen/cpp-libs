#pragma once

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <numeric>
#include <optional>
#include <cassert>
#include <stdexcept>

#define BTREE_TPL 

#ifdef BTREE_DEBUG
static auto &dbg = std::cout;
#else
struct NullStream {
    template<typename T>
    NullStream& operator<<(const T&) { return *this; }
    NullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
    NullStream& operator<<(std::ios_base& (*)(std::ios_base&)) { return *this; }
};
static NullStream dbg;
#endif

template <typename K, typename V, unsigned ORDER = 12, typename Cmp = decltype(std::less<K>{})>
class IntrusiveBTree {
    static_assert(ORDER >= 3, "ORDER must be at least 3");

    struct BTreeNode;

    struct BTreeLeaf {
        bool isLeaf;
        static_assert(std::numeric_limits<uint8_t>::max() >= ORDER - 1, "ORDER is too large for uint8_t");
        uint8_t len{0};
        unsigned size{0}; // size of current and all its children
        BTreeNode *parent;
        K keys[ORDER - 1];
        V vals[ORDER - 1];
        BTreeLeaf(bool isLeaf = true, BTreeNode *parent = nullptr): isLeaf(isLeaf), parent(parent) {}
        unsigned locate(const K &key) const {
            // in our scenario, a linear search is a tad faster than binary search
            // return std::lower_bound(keys, keys + len, key, Cmp()) - keys;
            return std::find_if(keys, keys + len, [&key](const K &k) {
                return !Cmp()(k, key);
            }) - keys;
        }
    };

    struct BTreeNodePtr {
        BTreeLeaf *ptr;
        BTreeNodePtr(BTreeLeaf *ptr = nullptr): ptr(ptr) {}
        BTreeNodePtr &operator=(const BTreeNodePtr &other) { ptr = other.ptr; return *this; }
        operator bool() const { return ptr != nullptr; }
        bool operator==(const BTreeNodePtr &other) const { return ptr == other.ptr; }
        BTreeLeaf *operator->(void) const { return static_cast<BTreeLeaf *>(ptr); }
        bool isLeaf() const { return ptr->isLeaf; }
        BTreeLeaf &leaf() {
            assert(isLeaf());
            return *static_cast<BTreeLeaf *>(ptr); 
        }
        BTreeNode &node() {
            assert(!isLeaf());
            return *static_cast<BTreeNode *>(ptr); 
        }
        BTreeNodePtr *children() {
            assert(!isLeaf());
            return static_cast<BTreeNode *>(ptr)->children; 
        }
        void destruct() {
            if (!ptr) {
                return;
            }
            if (isLeaf()) {
                delete static_cast<BTreeLeaf *>(ptr);
            } else {
                delete static_cast<BTreeNode *>(ptr);
            }
            ptr = nullptr;
        }
    };

    struct BTreeNode: public BTreeLeaf {
        using BTreeLeaf::len;
        BTreeNodePtr children[ORDER];
        ~BTreeNode() {
            if (len == 0) {
                return;
            }
            for(int i = 0; i < len + 1; i++) {
                children[i].destruct();
            }
        }
        BTreeNode(BTreeNode *parent = nullptr): BTreeLeaf(false, parent) {}
    };

    struct BTreeCursor {
        BTreeNodePtr node;
        uint8_t idx{0};
        bool valid() const { return node; }
        const K &key() { assert(node); return node->keys[idx]; }
        V &val() { assert(node); return node->vals[idx]; }
    };

    BTreeCursor getPredecessor(BTreeNode *node, unsigned idx) const {
        auto cur = node->children[idx];
        while(!cur.isLeaf()) {
            cur = cur.children()[cur->len];
        }
        assert(cur->len > 0);
        return {cur, static_cast<uint8_t>(cur->len - 1)};
    }

    BTreeCursor getSuccessor(BTreeNode *node, unsigned idx) const {
        auto cur = node->children[idx + 1];
        while(!cur.isLeaf()) {
            dbg << "cur: " << std::hex << (uintptr_t(cur) & 0xffff) << std::dec << std::endl;
            dbg << "cur front: " << cur->keys[0] << std::endl;
            cur = cur.children()[0];
        }
        assert(cur->len > 0);
        printNode(cur);
        return {cur, 0};
    }

    void printNode(const BTreeLeaf *leaf) const {
        dbg << "l" << std::hex << (uintptr_t(leaf) & 0xffff) << std::dec << "(" << int(leaf->len) << "): ";
        for(int i = 0; i < leaf->len; i++) {
            dbg << leaf->keys[i] << ',' << leaf->vals[i] << " ";
        }
        dbg << std::endl;
    }

    void printNode(const BTreeNode *node) const {
        dbg << "n" << std::hex << (uintptr_t(node) & 0xffff) << std::dec << "(" << int(node->len) << "): ";
        for(int i = 0; i < node->len; i++) {
            dbg << node->keys[i] << ',' << node->vals[i] << " ";
        }
        for(int i = 0; i < node->len + 1; i++) {
            dbg << std::hex << (uintptr_t(node->children[i].ptr) & 0xffff) << std::dec << '(' << int(node->children[i]->len) << ") ";
        }
        dbg << std::endl;
    }

    void printNode(BTreeNodePtr node) const {
        if (node.isLeaf()) {
            printNode(&node.leaf());
        } else {
            printNode(&node.node());
        }
    }

    void merge(BTreeNodePtr node, int idx) {
        auto child = node.children()[idx];
        auto sibling = node.children()[idx + 1];

        dbg << "merging ";
        printNode(sibling);
        dbg << "into ";
        printNode(child);

        assert(child->len <= ORDER / 2 - 1 && sibling->len <= ORDER / 2 - 1);
        
        // Move key and value from parent to child
        child->keys[child->len] = node->keys[idx];
        child->vals[child->len] = node->vals[idx];
        // Move keys and values from sibling to child
        std::move(sibling->keys, sibling->keys + sibling->len, child->keys + child->len + 1);
        std::move(sibling->vals, sibling->vals + sibling->len, child->vals + child->len + 1);
        // Move children if not leaf
        if (!child.isLeaf()) {
            for(int i = 0; i < sibling->len + 1; i++) {
                sibling.children()[i]->parent = &child.node();
                child.children()[child->len + 1 + i] = sibling.children()[i];
            }
        }
        // Remove sibling from parent
        std::move(node->keys + idx + 1, node->keys + node->len, node->keys + idx);
        std::move(node->vals + idx + 1, node->vals + node->len, node->vals + idx);
        std::move(node.children() + idx + 2, node.children() + node->len + 1, node.children() + idx + 1);
        // Update lengths
        child->len += sibling->len + 1;
        child->size += sibling->size + 1;
        node->len--;
        // node->size stays put
        sibling->len = 0;
        // sibling->size does not matter
        if (!sibling.isLeaf()) {
            sibling.children()[0] = nullptr;
        }
        // Destruct sibling
        sibling.destruct();

        dbg << "merged content: ";
        printNode(child);
    }

    void removeFromLeaf(BTreeLeaf *node, unsigned idx) {
        dbg << "remove #" << idx << "(" << node->keys[idx] << ") from ";
        printNode(node);
        assert(idx < node->len);
        std::move(node->keys + idx + 1, node->keys + node->len, node->keys + idx);
        std::move(node->vals + idx + 1, node->vals + node->len, node->vals + idx);
        node->len--;
        node->size--;

        // size-=1 recursively up to the root
        BTreeNode *n = node->parent;
        while(n) {
            dbg << "decr size from " << std::hex << (uintptr_t(n) & 0xffff) << std::dec << "(" << int(node->len) << ")" << std::endl;
            n->size--;
            n = n->parent;
        }

        if (node->len < ORDER / 2 && node->parent) {
            auto nIdx = node->parent->locate(node->keys[0]);
            assert(node->parent->children[nIdx].ptr == node);
            fill(node->parent, nIdx);
        }

    }

    void removeFromNonLeaf(BTreeNode *node, unsigned idx) {
        dbg << "remove #" << idx << "(" << node->keys[idx] << ") from ";
        printNode(node);
        if (node->children[idx]->len >= ORDER / 2) {
            auto pred = getPredecessor(node, idx);
            assert(pred.valid());
            node->keys[idx] = pred.key();
            node->vals[idx] = pred.val();
            if (pred.node.isLeaf()) {
                removeFromLeaf(&pred.node.leaf(), pred.idx);
            } else {
                removeFromNonLeaf(&pred.node.node(), pred.idx);
            }
        } else if (node->children[idx + 1]->len >= ORDER / 2) {
            auto succ = getSuccessor(node, idx);
            assert(succ.valid());
            node->keys[idx] = succ.key();
            node->vals[idx] = succ.val();
            dbg << "succ key: " << succ.key() << std::endl;
            if (succ.node.isLeaf()) {
                removeFromLeaf(&succ.node.leaf(), succ.idx);
            } else {
                removeFromNonLeaf(&succ.node.node(), succ.idx);
            }
        } else {
            auto key = node->keys[idx];
            merge(node, idx);
            doRemove(node->children[idx], key);
        }
    }

    void insertNonFull(BTreeNodePtr node, const K &key, const V &val) {
        if (node.isLeaf()) {
            auto idx = node->locate(key);
            std::move_backward(node->keys + idx, node->keys + node->len, node->keys + node->len + 1);
            std::move_backward(node->vals + idx, node->vals + node->len, node->vals + node->len + 1);
            node->keys[idx] = key;
            node->vals[idx] = val;
            node->len++;
            node->size++;
        } else {
            auto idx = node->locate(key);
            if (node.children()[idx]->len == ORDER - 1) {
                splitChild(&node.node(), idx);
                if (key > node->keys[idx]) {
                    idx++;
                }
            }
            insertNonFull(node.children()[idx], key, val);
            node->size++;
        }
    } 

    void splitChild(BTreeNode *parent, unsigned idx) {
        auto child = parent->children[idx];
        assert(child->len == ORDER - 1);
        // Create new child
        BTreeNodePtr newChild(child->isLeaf ? new BTreeLeaf : new BTreeNode);
        newChild->parent = parent;

        // Move upper half of keys and values to newChild
        std::move(child->keys + ORDER / 2, child->keys + ORDER - 1, newChild->keys);
        std::move(child->vals + ORDER / 2, child->vals + ORDER - 1, newChild->vals);
        unsigned agg = 0;
        if (!child->isLeaf) {
            for(int i = ORDER / 2; i < ORDER; i++) {
                agg += child.children()[i]->size;
                child.children()[i]->parent = &newChild.node();
                newChild.children()[i - ORDER / 2] = child.children()[i];
            }
        }
        newChild->len = ORDER - 1 - ORDER / 2;
        newChild->size = ORDER - 1 - ORDER / 2 + agg;

        // Insert middle key and value into parent
        std::move_backward(parent->keys + idx, parent->keys + parent->len, parent->keys + parent->len + 1);
        std::move_backward(parent->vals + idx, parent->vals + parent->len, parent->vals + parent->len + 1);
        parent->keys[idx] = child->keys[ORDER / 2 - 1];
        parent->vals[idx] = child->vals[ORDER / 2 - 1];
        std::move_backward(parent->children + idx + 1, parent->children + parent->len + 1, parent->children + parent->len + 2);
        parent->children[idx + 1] = newChild;
        
        child->len = ORDER / 2 - 1;
        child->size -= newChild->size + 1;
        parent->len++;
        // parent->size stays put
    }

    void borrowFromPrev(BTreeNode *node, unsigned idx) {
        auto child = node->children[idx];
        auto sibling = node->children[idx - 1];

        // Shift child's keys and values to make space
        std::move_backward(child->keys, child->keys + child->len, child->keys + child->len + 1);
        std::move_backward(child->vals, child->vals + child->len, child->vals + child->len + 1);
        // Take one from sibling
        child->keys[0] = node->keys[idx - 1];
        child->vals[0] = node->vals[idx - 1];
        node->keys[idx - 1] = sibling->keys[sibling->len - 1];
        node->vals[idx - 1] = sibling->vals[sibling->len - 1];
        // Move children if not leaf
        if (!child.isLeaf()) {
            assert(!sibling.isLeaf());
            std::move_backward(child.children(), child.children() + child->len + 1, child.children() + child->len + 2);
            child.children()[0] = sibling.children()[sibling->len];
            child.children()[0]->parent = &child.node();
            child->size += child.children()[0]->size;
            sibling->size -= child.children()[0]->size;
        }
        // Update lengths
        child->len++;
        child->size++;
        sibling->len--;
        sibling->size--;
    }

    void borrowFromNext(BTreeNode *node, unsigned idx) {
        auto child = node->children[idx];
        auto sibling = node->children[idx + 1];

        // Take one from sibling
        child->keys[child->len] = node->keys[idx];
        child->vals[child->len] = node->vals[idx];
        node->keys[idx] = sibling->keys[0];
        node->vals[idx] = sibling->vals[0];
        // Shift sibling's keys and values
        std::move(sibling->keys + 1, sibling->keys + sibling->len, sibling->keys);
        std::move(sibling->vals + 1, sibling->vals + sibling->len, sibling->vals);
        // Move children if not leaf
        if (!child.isLeaf()) {
            assert(!sibling.isLeaf());
            child.children()[child->len + 1] = sibling.children()[0];
            child.children()[child->len + 1]->parent = &child.node();
            std::move(sibling.children() + 1, sibling.children() + sibling->len + 1, sibling.children());
            child->size += child.children()[child->len + 1]->size;
            sibling->size -= child.children()[child->len + 1]->size;
        }
        // Update lengths
        child->len++;
        child->size++;
        sibling->len--;
        sibling->size--;
    }

    void fill(BTreeNode *node, unsigned idx) {
        dbg << "---------------Filling at idx " << idx << std::endl;
        dbg << "parent content ";
        printNode(node);
        dbg << "children content: ";
        printNode(node->children[idx]);
        if (idx > 0) {
            dbg << "prev content: ";
            printNode(node->children[idx - 1]);
        }
        if (idx < node->len) {
            dbg << "next content: ";
            printNode(node->children[idx + 1]);
        }

        if (idx != 0 && node->children[idx - 1]->len >= ORDER / 2) {
            borrowFromPrev(node, idx);
            dbg << "after borrow from prev ";
            printNode(node);
        } else if (idx != node->len && node->children[idx + 1]->len >= ORDER / 2) {
            borrowFromNext(node, idx);
            dbg << "after borrow from next ";
            printNode(node);
        } else {
            auto targetIdx = idx != node->len ? idx : idx - 1;
            if (idx != node->len) {
                merge(node, idx);
            } else {
                merge(node, idx - 1);
            }
            dbg << "after merge ";
            printNode(node);
        }
    }

    BTreeCursor doFind(BTreeNodePtr node, const K &key) const {
        BTreeCursor cursor;
        auto idx = node->locate(key);
        if (idx < node->len && !Cmp()(key, node->keys[idx])) {
            cursor = {node, idx};
        } else if (!node.isLeaf()) {
            cursor = doFind(node.children()[idx], key);
        }
        return cursor;
    }

    bool doRemove(BTreeNodePtr node, const K &key) {
        auto idx = node->locate(key);
        if (idx < node->len && !Cmp()(key, node->keys[idx])) {
            if (node.isLeaf()) {
                removeFromLeaf(&node.leaf(), idx);
            } else {
                removeFromNonLeaf(&node.node(), idx);
            }
            return true;
        } else {
            if (node.isLeaf()) {
                return false; // key not found
            }
            bool flag = (idx == node->len);
            if (node.children()[idx]->len < ORDER / 2) {
                fill(&node.node(), idx);
            }
            if (flag && idx > node->len) {
                return doRemove(node.children()[idx - 1], key);
            } else {
                return doRemove(node.children()[idx], key);
            }
        }
    }

    void doTraverse(BTreeNodePtr node, int depth, int &last, int &counter, bool print) {
        if (node->parent) {
            if (node->len < ORDER / 2 - 1) {
                throw std::runtime_error("node length is less than ORDER / 2 - 1");
            }
        } else if (node != root) {
            throw std::runtime_error("node parent is invalid, but not root");
        }

        if (node.isLeaf()) {
            for (unsigned i = 0; i < node->len; i++) {
                if (print) {
                    std::cout << node->keys[i] << ',' << node->vals[i] << "(d" << depth << "l) ";
                    std::cout.flush();
                }
                if(node->keys[i] < last) {
                    throw std::runtime_error("order violation");
                }
                last = node->keys[i];
                counter++;
            }
        } else {
            unsigned agg = 0;
            unsigned counterStart = counter;
            for (unsigned i = 0; i < node->len; i++) {
                doTraverse(node.children()[i], depth + 1, last, counter, print);
                if (print) {
                    std::cout << node->keys[i] << ',' << node->vals[i] << "(d" << depth << "n) ";
                }
                if(node->keys[i] < last) {
                    throw std::runtime_error("order violation");
                }
                last = node->keys[i];
                counter++;
                agg += node.children()[i]->size;
            }
            doTraverse(node.children()[node->len], depth + 1, last, counter, print);
            agg += node.children()[node->len]->size;
            if (agg + node->len != node->size) {
                std::cout << std::endl;
                printNode(&node.node());
                std::cout.flush();
                throw std::runtime_error("size mismatch: " + std::to_string(agg + node->len) + " != " + std::to_string(node->size));
            }
        }
    }

    BTreeNodePtr root;

public:
    IntrusiveBTree() : root(new BTreeLeaf()) {}

    ~IntrusiveBTree() {
        root.destruct();
    }

    void insert(const K &key, const V &val = {}) {
        if (root->len == ORDER - 1) {
            auto newRoot = new BTreeNode;
            newRoot->children[0] = root;
            root->parent = newRoot;
            newRoot->size = root->size;
            splitChild(newRoot, 0);
            insertNonFull(newRoot, key, val);
            root = newRoot;
        } else {
            insertNonFull(root, key, val);
        }
    }

    BTreeCursor find(const K &key) const {
        return doFind(root, key);
    }

    bool remove(const K &key) {
        auto ret = doRemove(root, key);

        if (!root.isLeaf() && root->len == 0) {
            auto tmp = root;
            root = root.children()[0];
            root->parent = nullptr;
            tmp.children()[0] = nullptr;
            tmp.destruct();
        }

        return ret;
    }

    void traverse(bool print = false) {
        int last = -1;
        int counter = 0;
        doTraverse(root, 0, last, counter, print);
        if (print) {
            std::cout << counter << " nodes traversed" << std::endl;
        }
    }

    unsigned getRank(const K &key) const {
        unsigned rank = 0;
        BTreeNodePtr node = root;
        while (node) {
            printNode(node);
            auto idx = node->locate(key);
            rank += idx;
            if (!node.isLeaf()) {
                for(int i = 0; i < idx; i++) {
                    rank += node.children()[i]->size;
                }
            }
            if (idx < node->len && !Cmp()(key, node->keys[idx])) {
                // found the exact one
                if (!node.isLeaf()) {
                    rank += node.children()[idx]->size;
                }
                break;
            }
            if (node.isLeaf()) {
                break;
            }
            node = node.children()[idx];
        }
        return rank;
    }

    unsigned size() const {
        return root.ptr ? root->size : 0;
    }
};

template <typename K, typename V, unsigned ORDER = 12, typename Cmp = decltype(std::less<K>{})>
using BTreeMap = IntrusiveBTree<K, V, ORDER, Cmp>;

template <typename K, unsigned ORDER = 12, typename Cmp = decltype(std::less<K>())>
using BTreeSet = IntrusiveBTree<K, std::tuple<>, ORDER, Cmp>;