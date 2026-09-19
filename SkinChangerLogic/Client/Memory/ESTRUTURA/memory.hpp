#ifndef MEMORY_HPP
#define MEMORY_HPP


#include <memory>
#include <type_traits>
#include <mutex>

#include <ESTRUTURA/memory_engine.hpp>
#include <ESTRUTURA/types.hpp>

class c_memory {
public:
    c_memory() = default;
    ~c_memory() = default;

public:
    bool setup(bool verbose = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return memory_engine.initialize(verbose);
    }

    void destroy()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        memory_engine.shutdown();
        guest_cr3 = 0;
    }

    void set_cr3(u64 value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        guest_cr3 = value;
    }

    void clear_cr3()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        guest_cr3 = 0;
    }

    bool is_valid() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return memory_engine.is_initialized();
    }

    HANDLE get_process_handle() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return memory_engine.process();
    }

    template <typename t_type>
    bool read(u64 address, t_type& out_value, bool verbose = false)
    {
        static_assert(std::is_trivially_copyable_v<t_type>);
        return read(address, &out_value, sizeof(t_type), verbose);
    }

    template <typename t_type>
    t_type read(u64 address, bool verbose = false)
    {
        static_assert(std::is_trivially_copyable_v<t_type>);

        t_type out_value { };
        read(address, out_value, verbose);
        return out_value;
    }

    bool read(u64 address, void* buffer, usize size, bool verbose = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (guest_cr3 != 0 && !utility::is_likely_guest_kernel_pointer(address)) {
            return memory_engine.read_guest_user(guest_cr3, address, buffer, size, verbose);
        }

        if (utility::is_likely_guest_kernel_pointer(address)) {
            return memory_engine.read_guest_kernel(address, buffer, size, verbose);
        }

        return memory_engine.read_guest_physical(address, buffer, size, verbose);
    }

    template <typename t_type>
    bool read_kernel(u64 address, t_type& out_value, bool verbose = false)
    {
        static_assert(std::is_trivially_copyable_v<t_type>);
        return read_kernel(address, &out_value, sizeof(t_type), verbose);
    }

    bool read_kernel(u64 address, void* buffer, usize size, bool verbose = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return memory_engine.read_guest_kernel(address, buffer, size, verbose);
    }

    bool read_user(u64 cr3, u64 address, void* buffer, usize size, bool verbose = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return memory_engine.read_guest_user(cr3, address, buffer, size, verbose);
    }

    template <typename t_type>
    bool write(u64 address, const t_type& value, bool verbose = false)
    {
        static_assert(std::is_trivially_copyable_v<t_type>);
        return write(address, &value, sizeof(t_type), verbose);
    }

    bool write(u64 address, const void* buffer, usize size, bool verbose = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (guest_cr3 != 0 && !utility::is_likely_guest_kernel_pointer(address)) {
            return memory_engine.write_guest_user(guest_cr3, address, buffer, size, verbose);
        }

        if (utility::is_likely_guest_kernel_pointer(address)) {
            return memory_engine.write_guest_kernel(address, buffer, size, verbose);
        }

        return memory_engine.write_guest_physical(address, buffer, size, verbose);
    }

    u64 get_cr3(u64 mm, bool verbose = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto result = memory_engine.read_mm_cr3(mm, verbose);
        if (!result.has_value())
            return 0;

        return *result;
    }

private:
    mem::c_memory_engine memory_engine { };
    u64                  guest_cr3     { };
    mutable std::mutex   m_mutex       { };
};

inline auto memory = std::make_shared<c_memory>();

#endif
