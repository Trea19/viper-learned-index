#include "common_fixture.hpp"
#include "ycsb_common.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <thread>
#include <execution>

namespace viper::kv_bm {

std::string random_file(const std::filesystem::path& base_dir) {
    if (!std::filesystem::exists(base_dir)) {
        if (!std::filesystem::create_directories(base_dir)) {
            throw std::runtime_error{"Could not create dir: " + base_dir.string() + "\n"};
        }
    }
    std::string str("abcdefghijklmnopqrstuvwxyz");
    std::random_device rd;
    std::mt19937 generator(rd());
    std::shuffle(str.begin(), str.end(), generator);
    std::string file_name = str.substr(0, 15) + ".file";
    std::filesystem::path file{file_name};
    return base_dir / file;
}

void BaseFixture::log_find_count(benchmark::State& state, uint64_t num_found, uint64_t num_expected) {
    state.counters["found"] = num_found;
    if (num_found != num_expected) {
        std::cerr << " DID NOT FIND ALL ENTRIES (" + std::to_string(num_found)
            + "/" + std::to_string(num_expected) + ")\n";
    }
}

template <typename PrefillFn>
void BaseFixture::prefill_internal(const size_t num_prefills, PrefillFn prefill_fn) {
#ifndef NDEBUG
    std::cout << "START PREFILL." << std::endl;
#endif
    const auto start = std::chrono::high_resolution_clock::now();
    cpu_set_t cpuset_before;
    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset_before);
    set_cpu_affinity();

    std::vector<std::thread> prefill_threads{};
    const size_t num_prefills_per_thread = (num_prefills / 1) + 1;
    for (size_t thread_num = 0; thread_num < 1; ++thread_num) {
        const size_t start_key = thread_num * num_prefills_per_thread;
        const size_t end_key = std::min(start_key + num_prefills_per_thread, num_prefills);
        prefill_threads.emplace_back([=]() {
            set_cpu_affinity(thread_num);
            prefill_fn(start_key, end_key);
        });
    }

    for (std::thread& thread : prefill_threads) {
        thread.join();
    }

    set_cpu_affinity(CPU_ISSET(0, &cpuset_before) ? 0 : 1);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "PREFILL DURATION: " << duration << " s." << std::endl;
}

void BaseFixture::prefill(const size_t num_prefills) {
    if (num_prefills == 0) {
        return;
    }

    auto prefill_fn = [this](const size_t start, const size_t end) {
        this->insert(start, end);
    };

    prefill_internal(num_prefills, prefill_fn);
}

void BaseFixture::prefill_ycsb(const std::vector<ycsb::Record>& data) {
    const size_t num_prefills = data.size();
    auto prefill_fn = [&](const size_t start, const size_t end) {
        this->run_ycsb(start, end, data, nullptr);
    };

    prefill_internal(num_prefills, prefill_fn);
}

void BaseFixture::generate_strings(size_t num_strings, size_t key_size, size_t value_size) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    cpu_set_t cpuset_before;
    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset_before);
    set_cpu_affinity();

    const auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Generating records..." << std::endl;

    auto record_gen_fn = [](std::vector<std::string>& records, size_t start, size_t end, size_t record_size) {
        std::random_device rd;
        std::default_random_engine rng(rd());
        std::normal_distribution<> record_length_dist(record_size, 0.2 * record_size);
        std::uniform_int_distribution<> dist(0, sizeof(alphabet) / sizeof(*alphabet) - 2);

        for (auto i = start; i < end; ++i) {
            size_t record_len = record_length_dist(rng);
            record_len = std::min(record_len, 2 * record_size);
            record_len = std::max(1ul, record_len);
            std::string str;
            str.reserve(record_len);
            std::generate_n(std::back_inserter(str), record_len, [&]() { return alphabet[dist(rng)]; });
            records[i] = std::move(str);
        }
    };

    std::vector<std::string>& keys = std::get<0>(var_size_kvs_);
    keys.resize(num_strings);
    std::vector<std::string>& values = std::get<1>(var_size_kvs_);
    values.resize(num_strings);

    std::vector<std::thread> generator_threads{};
    const size_t num_strings_per_thread = (num_strings / NUM_UTIL_THREADS) + 1;
    for (size_t thread_num = 0; thread_num < NUM_UTIL_THREADS; ++thread_num) {
        const size_t start_i = thread_num * num_strings_per_thread;
        const size_t end_i = std::min(start_i + num_strings_per_thread, num_strings);
        generator_threads.emplace_back([=, &keys, &values]() {
            set_cpu_affinity(thread_num);
            record_gen_fn(keys, start_i, end_i, key_size);
            record_gen_fn(values, start_i, end_i, value_size);
        });
    }

    for (std::thread& thread : generator_threads) {
        thread.join();
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "GENERATION DURATION: " << duration << " s." << std::endl;

    set_cpu_affinity(CPU_ISSET(0, &cpuset_before) ? 0 : 1);
}

bool is_init_thread(const benchmark::State& state) {
    // Use idx = 1 because 0 starts all threads first before continuing.
    return state.threads == 1 || state.thread_index == 1;
}

void set_cpu_affinity(const uint16_t from, const uint16_t to) {
    if (from >= CPUS.size() || to > CPUS.size() || from < 0 || to < 0 || to <= from) {
        throw std::runtime_error("Thread range invalid! " +
                                 std::to_string(from) + " -> " + std::to_string(to));
    }

    const auto native_thread_handle = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int cpu = from; cpu < to; ++cpu) {
        CPU_SET(CPUS[cpu], &cpuset);
    }
    int rc = pthread_setaffinity_np(native_thread_handle, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
}

void set_cpu_affinity() {
    return set_cpu_affinity(0, CPUS.size());
}

void set_cpu_affinity(uint16_t thread_idx) {
    return set_cpu_affinity(thread_idx, thread_idx + 1);
}

void zero_block_device(const std::string& block_dev, size_t length) {
    int fd = open(block_dev.c_str(), O_RDWR);
    if (fd < 0) {
        throw std::runtime_error("Cannot open dax device: " + block_dev + " | " + std::strerror(errno));
    }

    void* addr = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == nullptr || addr == reinterpret_cast<void*>(0xffffffffffffffff)) {
        throw std::runtime_error("Cannot mmap pool file: " + block_dev + " | " + std::strerror(errno));
    }

    constexpr size_t buffer_size = 4096;
    std::array<char, buffer_size> buffer;
    buffer.fill(0);

    const size_t num_chunks = length / buffer_size;
    const size_t num_chunks_per_thread = (num_chunks / NUM_UTIL_THREADS) + 1;

    std::vector<std::thread> zero_threads;
    zero_threads.reserve(NUM_UTIL_THREADS);
    for (size_t thread_num = 0; thread_num < NUM_UTIL_THREADS; ++thread_num) {
        char* start_addr = reinterpret_cast<char*>(addr) + (thread_num * num_chunks_per_thread);
        zero_threads.emplace_back([&](char* start_addr) {
            for (size_t i = 0; i < num_chunks_per_thread; ++i) {
                void* chunk_start_addr = start_addr + (i * buffer_size);
                memcpy(chunk_start_addr, &buffer, buffer_size);
            }
        }, start_addr);
    }

    for (std::thread& thread : zero_threads) {
        thread.join();
    }

    munmap(addr, length);
}

}  // namespace viper::kv_bm
