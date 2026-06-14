#pragma once

#include <vector>
#include <algorithm>
#include <string>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <condition_variable>

template <typename Key, typename Value>
class HydroDBEngine {
    struct Node {
        Key key;
        Value value;
    };
    
    struct Comp {
        bool operator()(const Node& a, const Node& b) const { return a.key < b.key; }
        bool operator()(const Node& a, const Key& b) const { return a.key < b; }
        bool operator()(const Key& a, const Node& b) const { return a < b.key; }
    };

    std::vector<std::vector<Node>> buckets;
    std::vector<Key> bucket_max;
    std::size_t elements_count = 0;
    
    int C;
    double EPS_HIGH = 0.85;
    double EPS_LOW = 0.50;
    Comp comp;

    mutable std::shared_mutex rw_lock; // Phase 1: Global Reader-Writer Lock
    mutable std::mutex pending_mutex;
    mutable std::condition_variable pending_cv;
    mutable int pending_writers = 0;

    struct WriterTracker {
        const HydroDBEngine* engine;
        WriterTracker(const HydroDBEngine* e) : engine(e) {
            std::lock_guard<std::mutex> lk(engine->pending_mutex);
            engine->pending_writers++;
        }
        ~WriterTracker() {
            {
                std::lock_guard<std::mutex> lk(engine->pending_mutex);
                engine->pending_writers--;
            }
            engine->pending_cv.notify_all();
        }
    };

    inline double pressure(int i) const {
        return static_cast<double>(buckets[i].size()) / C;
    }

    int find_bucket(const Key& k) const {
        if (bucket_max.empty()) return 0;
        int l = 0, r = static_cast<int>(bucket_max.size()) - 1;
        while (l <= r) {
            int m = l + (r - l) / 2;
            if (!(bucket_max[m] < k)) r = m - 1; 
            else l = m + 1;
        }
        if (l >= static_cast<int>(buckets.size())) l = static_cast<int>(buckets.size()) - 1;
        return l;
    }

    inline void update_index(int i) {
        if (!buckets[i].empty()) {
            bucket_max[i] = buckets[i].back().key;
        }
    }

    void flow(int i, int j) {
        int diff = static_cast<int>(buckets[i].size()) - static_cast<int>(buckets[j].size());
        if (diff <= 1) return; // Need at least diff of 2 to move 1 element

        int k = diff / 2; // Perfectly balance both buckets

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

        std::vector<Node> right(std::make_move_iterator(B.begin() + mid), std::make_move_iterator(B.end()));
        B.erase(B.begin() + mid, B.end());

        buckets.insert(buckets.begin() + i + 1, std::move(right));
        bucket_max.insert(bucket_max.begin() + i + 1, buckets[i + 1].back().key);

        update_index(i);
    }

public:
    explicit HydroDBEngine(int capacity = 1000) : C(capacity) {
        if (C < 2) throw std::invalid_argument("Capacity must be >= 2");
    }

    // Multi-Threaded Write
    void set(const Key& k, const Value& v) {
        WriterTracker tracker(this);
        std::unique_lock lock(rw_lock); // Exclusive Write Lock
        
        if (buckets.empty()) {
            buckets.push_back({Node{k, v}});
            bucket_max.push_back(k);
            elements_count++;
            return;
        }

        int i = find_bucket(k);
        auto &B = buckets[i];

        auto it = std::lower_bound(B.begin(), B.end(), k, comp);
        
        // Update existing key
        if (it != B.end() && !(it->key < k) && !(k < it->key)) {
            it->value = v;
            return;
        }

        // Insert new key
        B.insert(it, Node{k, v});
        elements_count++;
        update_index(i);

        if (static_cast<int>(B.size()) > C) split_bucket(i);
        stabilize(i);
    }

    // Multi-Threaded Read
    std::optional<Value> get(const Key& k) const {
        {
            std::unique_lock<std::mutex> plock(pending_mutex);
            pending_cv.wait(plock, [this] { return pending_writers == 0; });
        }
        std::shared_lock lock(rw_lock); // Concurrent Read Lock
        
        if (buckets.empty()) return std::nullopt;
        int i = find_bucket(k);
        const auto &B = buckets[i];
        auto it = std::lower_bound(B.begin(), B.end(), k, comp);
        
        if (it != B.end() && !(it->key < k) && !(k < it->key)) {
            return it->value;
        }
        return std::nullopt;
    }

    // Multi-Threaded Delete
    bool erase(const Key& k) {
        WriterTracker tracker(this);
        std::unique_lock lock(rw_lock); // Exclusive Write Lock
        
        if (buckets.empty()) return false;
        int i = find_bucket(k);
        auto &B = buckets[i];
        
        auto it = std::lower_bound(B.begin(), B.end(), k, comp);
        if (it == B.end() || k < it->key) return false; 

        B.erase(it);
        elements_count--;

        if (B.empty()) {
            buckets.erase(buckets.begin() + i);
            bucket_max.erase(bucket_max.begin() + i);
            return true;
        }

        update_index(i);

        if (i > 0) {
            if (pressure(i - 1) - pressure(i) > EPS_HIGH) flow(i - 1, i);
        }
        if (i + 1 < static_cast<int>(buckets.size())) {
            if (pressure(i + 1) - pressure(i) > EPS_HIGH) flow(i + 1, i);
        }
        return true;
    }

    // Multi-Threaded Range Query
    std::vector<std::pair<Key, Value>> range(const Key& L, const Key& R, int offset = 0, int count = -1) const {
        {
            std::unique_lock<std::mutex> plock(pending_mutex);
            pending_cv.wait(plock, [this] { return pending_writers == 0; });
        }
        std::shared_lock lock(rw_lock); // Concurrent Read Lock
        std::vector<std::pair<Key, Value>> result;
        
        if (R < L || buckets.empty()) return result;

        int skipped = 0;
        int i = find_bucket(L);
        for (; i < static_cast<int>(buckets.size()); ++i) {
            const auto &B = buckets[i];
            if (!B.empty() && R < B.front().key) break; 

            auto it_start = std::lower_bound(B.begin(), B.end(), L, comp);
            auto it_end = std::upper_bound(B.begin(), B.end(), R, comp);
            
            for (auto it = it_start; it != it_end; ++it) {
                if (skipped < offset) {
                    skipped++;
                    continue;
                }
                result.push_back({it->key, it->value});
                if (count != -1 && static_cast<int>(result.size()) >= count) return result;
            }
        }
        return result;
    }
};
