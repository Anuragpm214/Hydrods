#pragma once

#include <vector>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <stdexcept>
#include <functional>

/**
 * @class HydroDS
 * @brief A cache-friendly, dynamic bucket-based balanced data structure.
 * 
 * HydroDS acts as an unrolled linked list / B-tree hybrid. It maintains 
 * a dynamic collection of sorted std::vectors (buckets). To avoid expensive 
 * array splitting/merging, it uses a "fluid pressure" stabilization algorithm 
 * to flow elements to adjacent buckets when capacity thresholds are reached.
 */
template <
    typename T, 
    typename Compare = std::less<T>
>
class HydroDS {
public:
    // Standard STL Container Type Aliases
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    
private:
    std::vector<std::vector<T>> buckets;
    std::vector<T> bucket_max;
    size_type elements_count = 0;
    
    int C;
    double EPS_HIGH;
    double EPS_LOW;
    Compare comp;

    inline double pressure(int i) const {
        return static_cast<double>(buckets[i].size()) / C;
    }

    int find_bucket(const T& x) const {
        if (bucket_max.empty()) return 0;
        int l = 0, r = static_cast<int>(bucket_max.size()) - 1;
        while (l <= r) {
            int m = l + (r - l) / 2;
            // bucket_max[m] >= x is equivalent to !(bucket_max[m] < x)
            if (!comp(bucket_max[m], x)) r = m - 1; 
            else l = m + 1;
        }
        if (l >= static_cast<int>(buckets.size())) l = static_cast<int>(buckets.size()) - 1;
        return l;
    }

    inline void update_index(int i) {
        if (!buckets[i].empty()) {
            bucket_max[i] = buckets[i].back();
        }
    }

    void flow(int i, int j) {
        double dp = pressure(i) - pressure(j);
        if (dp <= EPS_HIGH) return;

        int k = static_cast<int>(C * (dp - EPS_LOW) / 2.0);
        k = std::max(1, std::min(k, static_cast<int>(buckets[i].size())));

        auto &A = buckets[i];
        auto &B = buckets[j];

        if (i < j) {
            B.insert(B.begin(), std::make_move_iterator(A.end() - k), std::make_move_iterator(A.end()));
            A.erase(A.end() - k, A.end());
        } else {
            B.insert(B.end(), std::make_move_iterator(A.begin()), std::make_move_iterator(A.begin() + k));
            A.erase(A.begin(), A.begin() + k);
        }

        update_index(i);
        update_index(j);
    }

    void stabilize(int i) {
        bool active = false;
        for (int step = 0; step < 2; ++step) {
            bool moved = false;

            if (i > 0) {
                double dp = pressure(i) - pressure(i - 1);
                if ((!active && dp > EPS_HIGH) || (active && dp >= EPS_LOW)) {
                    active = true;
                    flow(i, i - 1);
                    moved = true;
                }
            }

            if (i + 1 < static_cast<int>(buckets.size())) {
                double dp = pressure(i) - pressure(i + 1);
                if ((!active && dp > EPS_HIGH) || (active && dp >= EPS_LOW)) {
                    active = true;
                    flow(i, i + 1);
                    moved = true;
                }
            }

            if (!moved) break;
        }
    }

    void split_bucket(int i) {
        if (buckets.size() == buckets.capacity()) buckets.reserve(buckets.size() + 1);
        if (bucket_max.size() == bucket_max.capacity()) bucket_max.reserve(bucket_max.size() + 1);

        auto &B = buckets[i];
        int mid = B.size() / 2;

        std::vector<T> right(std::make_move_iterator(B.begin() + mid), std::make_move_iterator(B.end()));
        B.erase(B.begin() + mid, B.end());

        buckets.insert(buckets.begin() + i + 1, std::move(right));
        bucket_max.insert(bucket_max.begin() + i + 1, buckets[i + 1].back());

        update_index(i);
    }

public:
    // ==========================================
    // Standard Bidirectional Const Iterator
    // ==========================================
    class const_iterator {
        const HydroDS* ds;
        int bucket_idx;
        int elem_idx;
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator(const HydroDS* d, int b_idx, int e_idx) : ds(d), bucket_idx(b_idx), elem_idx(e_idx) {
            advance_if_needed();
        }

        const_iterator() : ds(nullptr), bucket_idx(0), elem_idx(0) {}

        reference operator*() const {
            return ds->buckets[bucket_idx][elem_idx];
        }

        pointer operator->() const {
            return &ds->buckets[bucket_idx][elem_idx];
        }

        const_iterator& operator++() {
            elem_idx++;
            advance_if_needed();
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        const_iterator& operator--() {
            if (bucket_idx == static_cast<int>(ds->buckets.size())) {
                if (!ds->buckets.empty()) {
                    bucket_idx = ds->buckets.size() - 1;
                    elem_idx = ds->buckets[bucket_idx].size() - 1;
                }
            } else {
                if (elem_idx > 0) {
                    elem_idx--;
                } else {
                    int original_bucket = bucket_idx;
                    do {
                        bucket_idx--;
                    } while (bucket_idx >= 0 && ds->buckets[bucket_idx].empty());
                    if (bucket_idx >= 0) {
                        elem_idx = ds->buckets[bucket_idx].size() - 1;
                    } else {
                        bucket_idx = original_bucket;
                        elem_idx = 0;
                        throw std::out_of_range("Iterator decremented past begin");
                    }
                }
            }
            return *this;
        }

        const_iterator operator--(int) {
            const_iterator tmp = *this;
            --(*this);
            return tmp;
        }

        friend bool operator==(const const_iterator& a, const const_iterator& b) {
            return a.ds == b.ds && a.bucket_idx == b.bucket_idx && a.elem_idx == b.elem_idx;
        }

        friend bool operator!=(const const_iterator& a, const const_iterator& b) {
            return !(a == b);
        }

    private:
        void advance_if_needed() {
            if (!ds) return;
            while (bucket_idx < static_cast<int>(ds->buckets.size()) && 
                   elem_idx >= static_cast<int>(ds->buckets[bucket_idx].size())) {
                bucket_idx++;
                elem_idx = 0;
            }
        }
    };

    using iterator = const_iterator; // Set structure: all elements are const to prevent sorting invalidation

    const_iterator begin() const { return const_iterator(this, 0, 0); }
    const_iterator end() const { return const_iterator(this, buckets.size(), 0); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    // ==========================================
    // Constructors & Rule of 5
    // ==========================================
    explicit HydroDS(int capacity = 1000, double eps_low = 0.50, double eps_high = 0.85, Compare c = Compare()) 
        : C(capacity), EPS_HIGH(eps_high), EPS_LOW(eps_low), comp(c) {
        if (C < 2) throw std::invalid_argument("Capacity must be >= 2");
        if (EPS_LOW >= EPS_HIGH || EPS_LOW <= 0.0 || EPS_HIGH >= 1.0) {
            throw std::invalid_argument("Invalid EPS values. Require 0 < EPS_LOW < EPS_HIGH < 1.0");
        }
    }

    template <typename InputIt>
    HydroDS(InputIt first, InputIt last, int capacity = 1000, double eps_low = 0.50, double eps_high = 0.85, Compare c = Compare())
        : HydroDS(capacity, eps_low, eps_high, c) {
        for (auto it = first; it != last; ++it) {
            insert(*it);
        }
    }

    HydroDS(const HydroDS& other) = default;
    HydroDS(HydroDS&& other) noexcept 
        : buckets(std::move(other.buckets)),
          bucket_max(std::move(other.bucket_max)),
          elements_count(other.elements_count),
          C(other.C), EPS_HIGH(other.EPS_HIGH), EPS_LOW(other.EPS_LOW), comp(std::move(other.comp)) {
        other.elements_count = 0;
    }
    HydroDS& operator=(const HydroDS& other) = default;
    HydroDS& operator=(HydroDS&& other) noexcept {
        if (this != &other) {
            buckets = std::move(other.buckets);
            bucket_max = std::move(other.bucket_max);
            elements_count = other.elements_count;
            C = other.C;
            EPS_HIGH = other.EPS_HIGH;
            EPS_LOW = other.EPS_LOW;
            comp = std::move(other.comp);
            other.elements_count = 0;
        }
        return *this;
    }
    ~HydroDS() = default;

    // ==========================================
    // Capacity & Modification
    // ==========================================
    bool empty() const {
        return elements_count == 0;
    }

    size_type size() const {
        return elements_count;
    }

    void clear() {
        buckets.clear();
        bucket_max.clear();
        elements_count = 0;
    }

    void insert(const T& x) {
        if (buckets.empty()) {
            buckets.push_back({x});
            bucket_max.push_back(x);
            elements_count++;
            return;
        }

        int i = find_bucket(x);
        auto &B = buckets[i];

        // Edge-biased fast insertion
        if (!comp(x, B.back())) { // x >= B.back()
            B.push_back(x);
        } else if (!comp(B.front(), x)) { // x <= B.front()
            B.insert(B.begin(), x);
        } else {
            B.insert(std::lower_bound(B.begin(), B.end(), x, comp), x);
        }
        
        elements_count++;
        update_index(i);

        if (static_cast<int>(B.size()) > C)
            split_bucket(i);

        stabilize(i);
    }

    const_iterator find(const T& x) const {
        if (buckets.empty()) return end();
        int i = find_bucket(x);
        const auto &B = buckets[i];
        auto it = std::lower_bound(B.begin(), B.end(), x, comp);
        
        // Check if elements are equivalent: !(a < b) && !(b < a)
        if (it != B.end() && !comp(x, *it) && !comp(*it, x)) { 
            int elem_idx = std::distance(B.begin(), it);
            return const_iterator(this, i, elem_idx);
        }
        return end();
    }

    bool search(const T& x) const {
        return find(x) != end();
    }

    bool erase(const T& x) {
        if (buckets.empty()) return false;

        int i = find_bucket(x);
        if (i < 0 || i >= static_cast<int>(buckets.size())) return false;

        auto &B = buckets[i];
        auto it = std::lower_bound(B.begin(), B.end(), x, comp);
        
        if (it == B.end() || comp(x, *it) || comp(*it, x)) return false; 

        B.erase(it);
        elements_count--;

        if (B.empty()) {
            buckets.erase(buckets.begin() + i);
            bucket_max.erase(bucket_max.begin() + i);
            return true;
        }

        update_index(i);

        // Reverse flow stabilization
        if (i > 0) {
            double dp = pressure(i - 1) - pressure(i);
            if (dp > EPS_HIGH) flow(i - 1, i);
        }

        if (i + 1 < static_cast<int>(buckets.size())) {
            double dp = pressure(i + 1) - pressure(i);
            if (dp > EPS_HIGH) flow(i + 1, i);
        }
        
        return true;
    }

    size_type range_query(const T& L, const T& R) const {
        if (comp(R, L)) return 0; // L > R
        if (buckets.empty()) return 0;

        size_type cnt = 0;
        int i = find_bucket(L);

        for (; i < static_cast<int>(buckets.size()); ++i) {
            const auto &B = buckets[i];

            if (!B.empty() && comp(R, B.front())) break; // B.front() > R

            auto it_start = std::lower_bound(B.begin(), B.end(), L, comp);
            auto it_end = std::upper_bound(B.begin(), B.end(), R, comp);
            cnt += std::distance(it_start, it_end);
        }
        return cnt;
    }
};
