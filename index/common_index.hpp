//
// Created by bysoulwarden on 2021/10/25.
//

#ifndef VIPER_COMMON_INDEX_H
#define VIPER_COMMON_INDEX_H

#include <hdr_histogram.h>
#include <set>

namespace viper::index {
    using offset_size_t = uint64_t;
    using block_size_t = uint64_t;
    using page_size_t = uint8_t;
    using data_offset_size_t = uint16_t;

    struct KeyValueOffset {
        static constexpr offset_size_t INVALID = 0xFFFFFFFFFFFFFFFF;

        union {
            offset_size_t offset;
            struct {
                data_offset_size_t data_offset: 16;
                page_size_t page_number: 3;
                block_size_t block_number: 45;
            };
        };

        KeyValueOffset() : offset{INVALID} {}

        static KeyValueOffset NONE() { return KeyValueOffset{INVALID}; }

        explicit KeyValueOffset(const offset_size_t offset) : offset(offset) {}

        KeyValueOffset(const block_size_t block_number, const page_size_t page_number, const data_offset_size_t slot)
                : block_number{block_number}, page_number{page_number}, data_offset{slot} {}

        static KeyValueOffset Tombstone() {
            return KeyValueOffset{};
        }

        inline std::tuple<block_size_t, page_size_t, data_offset_size_t> get_offsets() const {
            return {block_number, page_number, data_offset};
        }

        inline offset_size_t get_offset() const {
            return offset;
        }

        inline bool is_tombstone() const {
            return offset == INVALID;
        }

        inline bool operator==(const KeyValueOffset &rhs) const { return offset == rhs.offset; }

        inline bool operator!=(const KeyValueOffset &rhs) const { return offset != rhs.offset; }
    };
    struct SkipListKey {
    public:
        uint64_t u;
        KeyValueOffset o;
        SkipListKey(uint64_t u,KeyValueOffset o):u(u),o(o){
        }
    };
    template<typename T1, typename T2>
    struct BulkComparator
    {
        bool operator() (const std::pair<T1, T2>& lhs,
                         const std::pair<T1, T2>& rhs) const
        {
            return lhs.first < rhs.first;
        }
    };

    template<typename KeyType>
    class BaseIndex {
    public:
        hdr_histogram *op_hdr;
        hdr_histogram *retrain_hdr;
        hdr_histogram *cus_hdr1;
        hdr_histogram *cus_hdr2;
        hdr_histogram *cus_hdr3;
        std::chrono::high_resolution_clock::time_point start;
        std::chrono::high_resolution_clock::time_point start_cus1;
        std::chrono::high_resolution_clock::time_point start_cus2;
        std::chrono::high_resolution_clock::time_point start_cus3;

        virtual BaseIndex* bulk_load(std::vector<std::pair<uint64_t, KeyValueOffset>> * vector,hdr_histogram * bulk_hdr,int threads){
            return nullptr;
        }


        virtual hdr_histogram *GetOpHdr() {
            if (op_hdr == nullptr) {
                hdr_init(1, 1000000000, 4, &op_hdr);
            }
            return op_hdr;
        }

        hdr_histogram *GetRetrainHdr() {
            if (retrain_hdr == nullptr) {
                hdr_init(1, 1000000000, 4, &retrain_hdr);
            }
            return retrain_hdr;
        }

        void LogRetrainStart() {
            if (retrain_hdr == nullptr) {
                return;
            }
            start = std::chrono::high_resolution_clock::now();
        }
        void LogRetrainEnd() {
            if (retrain_hdr == nullptr) {
                return;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(retrain_hdr, duration.count());
        }

        virtual hdr_histogram *GetCusHdr1() {
            if (cus_hdr1 == nullptr) {
                hdr_init(1, 1000000000, 4, &cus_hdr1);
            }
            return cus_hdr1;
        }
        void LogHdr1Start() {
            if (cus_hdr1 == nullptr) {
                return;
            }
            start_cus1 = std::chrono::high_resolution_clock::now();
        }
        void LogHdr1End() {
            if (cus_hdr1 == nullptr) {
                return;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_cus1);
            hdr_record_value_atomic(cus_hdr1, duration.count());
        }

        virtual hdr_histogram *GetCusHdr2() {
            if (cus_hdr2 == nullptr) {
                hdr_init(1, 1000000000, 4, &cus_hdr2);
            }
            return cus_hdr2;
        }
        void LogHdr2Start() {
            if (cus_hdr2 == nullptr) {
                return;
            }
            start_cus2 = std::chrono::high_resolution_clock::now();
        }
        void LogHdr2End() {
            if (cus_hdr2 == nullptr) {
                return;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_cus2);
            hdr_record_value_atomic(cus_hdr2, duration.count());
        }

        virtual hdr_histogram *GetCusHdr3() {
            if (cus_hdr3 == nullptr) {
                hdr_init(1, 1000000000, 4, &cus_hdr3);
            }
            return cus_hdr3;
        }
        void LogHdr3Start() {
            if (cus_hdr3 == nullptr) {
                return;
            }
            start_cus3 = std::chrono::high_resolution_clock::now();
        }
        void LogHdr3End() {
            if (cus_hdr3 == nullptr) {
                return;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_cus3);
            hdr_record_value_atomic(cus_hdr3, duration.count());
        }

        BaseIndex() {
            op_hdr = nullptr;
            retrain_hdr = nullptr;
            cus_hdr1= nullptr;
            cus_hdr2= nullptr;
            cus_hdr3= nullptr;
        }

        virtual ~BaseIndex() {};

        virtual bool SupportBulk(int threads){
            return false;
        }

        //index+data size
        virtual uint64_t GetIndexSize() { return 0;}
        //index size
        virtual uint64_t GetIndexSizeWithoutData() {return 0; }

        virtual KeyValueOffset Insert(const KeyType &k, KeyValueOffset o) {
            std::chrono::high_resolution_clock::time_point start;
            if (op_hdr != nullptr) {
                start = std::chrono::high_resolution_clock::now();
            }
            KeyValueOffset ret = CoreInsert(k, o);
            if (op_hdr == nullptr) {
                return ret;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(op_hdr, duration.count());
            return ret;
        }

        virtual KeyValueOffset Insert(const KeyType &k, KeyValueOffset o,uint32_t thread_id) {
            std::chrono::high_resolution_clock::time_point start;
            if (op_hdr != nullptr) {
                start = std::chrono::high_resolution_clock::now();
            }
            KeyValueOffset ret = CoreInsert(k, o,thread_id);
            if (op_hdr == nullptr) {
                return ret;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(op_hdr, duration.count());
            return ret;
        }

        virtual KeyValueOffset
        Insert(const KeyType &k, KeyValueOffset o, std::function<bool(KeyType, KeyValueOffset)> f) {
            std::chrono::high_resolution_clock::time_point start;
            if (op_hdr != nullptr) {
                start = std::chrono::high_resolution_clock::now();
            }
            KeyValueOffset ret = CoreInsert(k, o, f);
            if (op_hdr == nullptr) {
                return ret;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(op_hdr, duration.count());
            return ret;
        }

        virtual KeyValueOffset CoreInsert(const KeyType &, KeyValueOffset) {
            throw std::runtime_error("Insert  not implemented");
        }

        virtual KeyValueOffset CoreInsert(const KeyType &, KeyValueOffset,uint32_t thread_id) {
            throw std::runtime_error("Insert thread_id not implemented");
        }

        virtual KeyValueOffset
        CoreInsert(const KeyType & k, KeyValueOffset o, std::function<bool(KeyType, KeyValueOffset)> f) {
            return CoreInsert(k,o);
        }

        virtual KeyValueOffset Get(const KeyType &k) {
            std::chrono::high_resolution_clock::time_point start;
            if (op_hdr != nullptr) {
                start = std::chrono::high_resolution_clock::now();
            }
            KeyValueOffset ret = CoreGet(k);
            if (op_hdr == nullptr) {
                return ret;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(op_hdr, duration.count());
            return ret;
        }

        virtual KeyValueOffset Get(const KeyType &k,uint32_t thread_id) {
            std::chrono::high_resolution_clock::time_point start;
            if (op_hdr != nullptr) {
                start = std::chrono::high_resolution_clock::now();
            }
            KeyValueOffset ret = CoreGet(k,thread_id);
            if (op_hdr == nullptr) {
                return ret;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(op_hdr, duration.count());
            return ret;
        }

        virtual KeyValueOffset Get(const KeyType &k, std::function<bool(KeyType, KeyValueOffset)> f) {
            std::chrono::high_resolution_clock::time_point start;
            if (op_hdr != nullptr) {
                start = std::chrono::high_resolution_clock::now();
            }
            KeyValueOffset ret = CoreGet(k, f);
            if (op_hdr == nullptr) {
                return ret;
            }
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            hdr_record_value_atomic(op_hdr, duration.count());
            return ret;
        }

        virtual KeyValueOffset CoreGet(const KeyType &) {
            throw std::runtime_error("Get not implemented");
        }

        virtual KeyValueOffset CoreGet(const KeyType &,uint32_t thread_id) {
            throw std::runtime_error("Get not implemented");
        }

        virtual KeyValueOffset CoreGet(const KeyType & k, std::function<bool(KeyType, KeyValueOffset)> f) {
            return CoreGet(k);
        }
    };

    class KeyForXindex {
        typedef std::array<double, 1> model_key_t;

    public:
        static constexpr size_t model_key_size() { return 1; }
        static KeyForXindex max() {
            static KeyForXindex max_key(std::numeric_limits<uint64_t>::max());
            return max_key;
        }
        static KeyForXindex min() {
            static KeyForXindex min_key(std::numeric_limits<uint64_t>::min());
            return min_key;
        }

        KeyForXindex() : key(0) {}
        KeyForXindex(uint64_t key) : key(key) {}
        KeyForXindex(const KeyForXindex &other) { key = other.key; }
        KeyForXindex &operator=(const KeyForXindex &other) {
            key = other.key;
            return *this;
        }

        model_key_t to_model_key() const {
            model_key_t model_key;
            model_key[0] = key;
            return model_key;
        }

        friend bool operator<(const KeyForXindex &l, const KeyForXindex &r) { return l.key < r.key; }
        friend bool operator>(const KeyForXindex &l, const KeyForXindex &r) { return l.key > r.key; }
        friend bool operator>=(const KeyForXindex &l, const KeyForXindex &r) { return l.key >= r.key; }
        friend bool operator<=(const KeyForXindex &l, const KeyForXindex &r) { return l.key <= r.key; }
        friend bool operator==(const KeyForXindex &l, const KeyForXindex &r) { return l.key == r.key; }
        friend bool operator!=(const KeyForXindex &l, const KeyForXindex &r) { return l.key != r.key; }

        uint64_t key;
    } ;
}
#endif //VIPER_COMMON_INDEX_H