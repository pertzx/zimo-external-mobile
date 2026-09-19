#ifndef MEMORY_ENGINE_CPP
#define MEMORY_ENGINE_CPP

#include <IMPLEMENTACAO/includes.hpp>
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <iostream>
#include <vector>
#include <sstream>
#include <array>
#include <memory>

#include <ESTRUTURA/memory_engine.hpp>
#include <ESTRUTURA/app_config.hpp>
#include "../../XorStr.hpp"

namespace offsets {
    constexpr u64 peb_ldr                  = 0x18;
    constexpr u64 range_start              = 0x0;
    constexpr u64 range_size               = 0x8;
    constexpr u64 range_page_desc_stride   = 0x10;
    constexpr u64 page_desc_state          = 0x0;
    constexpr u64 page_desc_info           = 0x8;
    constexpr u64 page_map_cache_stride    = 0x20;
    constexpr u64 page_map_cached_tag      = 0x0;
    constexpr u64 page_map_page_desc       = 0x8;
    constexpr u64 page_map_host_base       = 0x18;
    constexpr u64 remote_stub_pvm          = 6;
    constexpr u64 remote_stub_chunk_id     = 15;
    constexpr u64 remote_stub_result_ptr_0 = 21;
    constexpr u64 remote_stub_target_fn    = 31;
    constexpr u64 remote_stub_result_ptr_1 = 43;
}

namespace utility {

    constexpr u64 k_long_mode_page_frame_mask = 0x000FFFFFFFFFF000ULL;
    constexpr u64 k_page_size                 = 0x1000ULL;
    constexpr u64 k_large_page_1gb_mask       = 0x000FFFFFFFE00000ULL;
    constexpr u64 k_large_page_2mb_mask       = 0x000FFFFFFFE00000ULL;
    constexpr u64 k_user_pointer_min          = 0x10000ULL;
    constexpr u64 k_user_pointer_max          = 0x00007FFFFFFFFFFFULL;
    constexpr u64 k_read_counter_limit        = 1000000000000ULL;
    constexpr u64 k_direct_state_mask_high    = 0x7000000000000ULL;
    constexpr u64 k_direct_state_mask_low     = 0x0FFFFFFFFFF000ULL;
    constexpr u64 k_present_bit               = 1ULL << 0;
    constexpr u64 k_page_size_bit             = 1ULL << 7;
    constexpr u64 k_page_mask                 = ~0xFFFULL;
    constexpr u64 k_page_offset_mask          = 0xFFFULL;
    constexpr u32 k_chunk_cache_mask          = 0x3F;
    constexpr u64 k_range_cache_mask          = 0x7ULL;
    constexpr u64 k_page_map_cache_mask       = 0xFFULL;
    constexpr usize k_remote_stub_size        = 128;
    constexpr usize k_remote_stub_stack       = 0x1000;

    enum class e_system_information_class : ULONG {
        system_process_information = 5
    };

    enum class e_process_info_class : ULONG {
        process_basic_information = 0
    };

    struct c_unicode_string;
    struct c_object_attributes;
    struct c_client_id;

    using t_nt_query_system_information = LONG (NTAPI*)(e_system_information_class, PVOID, ULONG, PULONG);
    using t_nt_open_process = LONG (NTAPI*)(PHANDLE, ACCESS_MASK, c_object_attributes*, c_client_id*);
    using t_nt_query_information_process = LONG (NTAPI*)(HANDLE, e_process_info_class, PVOID, ULONG, PULONG);
    using t_nt_queue_apc_thread_ex = LONG (NTAPI*)(
        HANDLE,
        HANDLE,
        ULONG,
        PVOID,
        PVOID,
        PVOID,
        PVOID
    );
    using t_nt_alert_thread = LONG (NTAPI*)( HANDLE );
    using t_nt_create_thread_ex = LONG (NTAPI*)(
        PHANDLE,
        ACCESS_MASK,
        c_object_attributes*,
        HANDLE,
        PVOID,
        PVOID,
        ULONG,
        SIZE_T,
        SIZE_T,
        SIZE_T,
        PVOID
    );

    struct c_unicode_string {
        USHORT length         { };
        USHORT maximum_length { };
        PWSTR  buffer         { };
    };

    struct c_object_attributes {
        ULONG             length                   { };
        HANDLE            root_directory           { };
        c_unicode_string* object_name              { };
        ULONG             attributes               { };
        PVOID             security_descriptor      { };
        PVOID             security_quality_service { };
    };

    struct c_client_id {
        HANDLE unique_process { };
        HANDLE unique_thread  { };
    };

    struct c_system_process_information {
        ULONG            next_entry_offset           { };
        ULONG            number_of_threads           { };
        BYTE             reserved_0[48]             { };
        c_unicode_string image_name                 { };
        LONG             base_priority              { };
        HANDLE           unique_process_id          { };
        PVOID             reserved_1                 { };
        ULONG            handle_count               { };
        ULONG            session_id                 { };
        PVOID            reserved_2                 { };
        SIZE_T           peak_virtual_size          { };
        SIZE_T           virtual_size               { };
        ULONG            reserved_3                 { };
        SIZE_T           peak_working_set_size      { };
        SIZE_T           working_set_size           { };
        PVOID            reserved_4                 { };
        SIZE_T           quota_paged_pool_usage     { };
        PVOID             reserved_5                 { };
        SIZE_T           quota_non_paged_pool_usage { };
        SIZE_T           pagefile_usage             { };
        SIZE_T           peak_pagefile_usage        { };
        SIZE_T           private_page_count         { };
        LARGE_INTEGER    read_operation_count       { };
        LARGE_INTEGER    write_operation_count      { };
        LARGE_INTEGER    other_operation_count      { };
        LARGE_INTEGER    read_transfer_count        { };
        LARGE_INTEGER    write_transfer_count       { };
        LARGE_INTEGER    other_transfer_count       { };
    };

    struct c_process_basic_information {
        LONG      exit_status          { };
        PVOID     peb_base_address     { };
        ULONG_PTR affinity_mask        { };
        LONG      base_priority        { };
        ULONG_PTR unique_process_id    { };
        ULONG_PTR inherited_process_id { };
    };

    struct c_peb_ldr_data {
        ULONG      length                        { };
        BOOLEAN    initialized                   { };
        HANDLE     ss_handle                     { };
        LIST_ENTRY in_load_order_module_list     { };
        LIST_ENTRY in_memory_order_module_list   { };
        LIST_ENTRY in_initialization_module_list { };
    };

    struct c_ldr_data_table_entry {
        LIST_ENTRY       in_load_order_links           { };
        LIST_ENTRY       in_memory_order_links         { };
        LIST_ENTRY       in_initialization_order_links { };
        PVOID            dll_base                      { };
        PVOID            entry_point                   { };
        ULONG            size_of_image                 { };
        c_unicode_string full_dll_name                 { };
        c_unicode_string base_dll_name                 { };
    };

    inline bool nt_success(LONG status)
    {
        return status >= 0;
    }

    inline bool is_likely_user_pointer(u64 value)
    {
        return value >= k_user_pointer_min && value < k_user_pointer_max;
    }

    void verbose_log(bool verbose, const std::string& message)
    {
        if (verbose) {
            std::cout << message << '\n';
        }
    }

    std::wstring to_lower_wide(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
            return static_cast<wchar_t>(::towlower(character));
        });
        return value;
    }

    template <typename t_type>
    t_type get_nt_function(const char* name)
    {
        static const auto ntdll_module = GetModuleHandleW(XorStr(L"ntdll"));
        if (ntdll_module == nullptr || name == nullptr)
            return nullptr;

        char name_copy[128]{};
        ::strncpy_s(name_copy, name, _TRUNCATE);
        return reinterpret_cast<t_type>(GetProcAddress(ntdll_module, name_copy));
    }

    HANDLE open_process_nt(u32 process_id, ACCESS_MASK access_mask)
    {
        static const auto nt_open_process = get_nt_function<t_nt_open_process>(XorStr("NtOpenProcess"));
        if (nt_open_process == nullptr)
            return nullptr;

        HANDLE process_handle { };
        c_object_attributes object_attributes { };
        object_attributes.length = sizeof(object_attributes);

        c_client_id client_id { };
        client_id.unique_process = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(process_id));

        if (!nt_success(nt_open_process(&process_handle, access_mask, &object_attributes, &client_id)))
            return nullptr;

        return process_handle;
    }

    std::vector<u32> enumerate_process_threads(u32 process_id)
    {
        std::vector<u32> thread_ids;
        const HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
        if ( snapshot == INVALID_HANDLE_VALUE )
            return thread_ids;

        THREADENTRY32 entry { };
        entry.dwSize = sizeof( entry );
        if ( Thread32First( snapshot, &entry ) ) {
            do {
                if ( entry.th32OwnerProcessID == process_id )
                    thread_ids.push_back( entry.th32ThreadID );
            } while ( Thread32Next( snapshot, &entry ) );
        }

        CloseHandle( snapshot );
        return thread_ids;
    }

    bool is_user_mode_address( u64 address )
    {
        return address >= k_user_pointer_min && address <= k_user_pointer_max;
    }

    void resume_thread_fully( HANDLE thread )
    {
        DWORD suspend_count = 0;
        do {
            suspend_count = ResumeThread( thread );
        } while ( suspend_count > 1 );
    }

    struct c_remote_stub_view {
        u64 chunk_ptr { };
        s64 status    { };
        u32 done      { };
        u32 pad       { };
    };

    bool poll_remote_stub_done( HANDLE remote_process, LPCVOID remote_result, DWORD timeout_ms, bool& out_done )
    {
        out_done = false;
        const u64 deadline = GetTickCount64( ) + timeout_ms;

        while ( GetTickCount64( ) < deadline ) {
            c_remote_stub_view snapshot { };
            SIZE_T bytes = 0;
            if ( !ReadProcessMemory( remote_process, remote_result, &snapshot, sizeof( snapshot ), &bytes ) || bytes != sizeof( snapshot ) )
                return false;

            if ( snapshot.done != 0 || snapshot.chunk_ptr != 0 ) {
                out_done = true;
                return true;
            }

            Sleep( 1 );
        }

        return true;
    }

    bool patch_remote_stub_spin_wait( HANDLE remote_process, PVOID remote_code, usize stub_size )
    {
        if ( stub_size < 1 )
            return false;

        static const u8 spin_tail[] = { 0xF3, 0x90, 0xEB, 0xFC };
        SIZE_T written = 0;
        return WriteProcessMemory(
            remote_process,
            static_cast< u8* >( remote_code ) + stub_size - 1,
            spin_tail,
            sizeof( spin_tail ),
            &written
        ) && written == sizeof( spin_tail );
    }

    bool patch_remote_stub_return( HANDLE remote_process, PVOID remote_code, usize stub_size )
    {
        if ( stub_size < 1 )
            return false;

        static const u8 ret_opcode = 0xC3;
        SIZE_T written = 0;
        return WriteProcessMemory(
            remote_process,
            static_cast< u8* >( remote_code ) + stub_size - 1,
            &ret_opcode,
            sizeof( ret_opcode ),
            &written
        ) && written == sizeof( ret_opcode );
    }

    bool reset_remote_stub_state( HANDLE remote_process, LPCVOID remote_result )
    {
        const c_remote_stub_view initial { };
        SIZE_T written = 0;
        return WriteProcessMemory(
            remote_process,
            const_cast< LPVOID >( remote_result ),
            &initial,
            sizeof( initial ),
            &written
        ) && written == sizeof( initial );
    }

    bool execute_remote_stub_via_hijack(
        HANDLE remote_process,
        u32 process_id,
        PVOID remote_code,
        LPCVOID remote_result,
        u64 remote_stack_top,
        usize stub_size,
        DWORD timeout_ms
    )
    {
        constexpr ACCESS_MASK k_thread_access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION;
        constexpr usize k_max_hijack_attempts = 16;
        const u64 deadline = GetTickCount64( ) + timeout_ms;
        usize attempts = 0;

        for ( const u32 thread_id : enumerate_process_threads( process_id ) ) {
            if ( GetTickCount64( ) >= deadline || attempts >= k_max_hijack_attempts )
                break;

            const DWORD remaining_ms = static_cast< DWORD >( deadline - GetTickCount64( ) );
            if ( remaining_ms == 0 )
                break;

            if ( !reset_remote_stub_state( remote_process, remote_result ) || !patch_remote_stub_spin_wait( remote_process, remote_code, stub_size ) )
                break;

            HANDLE thread = OpenThread( k_thread_access, FALSE, thread_id );
            if ( thread == nullptr )
                continue;

            ++attempts;

            if ( SuspendThread( thread ) == static_cast< DWORD >( -1 ) ) {
                CloseHandle( thread );
                continue;
            }

            CONTEXT context { };
            context.ContextFlags = CONTEXT_FULL;
            if ( !GetThreadContext( thread, &context ) || !is_user_mode_address( context.Rip ) ) {
                resume_thread_fully( thread );
                CloseHandle( thread );
                continue;
            }

            const CONTEXT saved_context = context;
            context.Rip = reinterpret_cast< DWORD64 >( remote_code );
            context.Rsp = remote_stack_top & ~0xFULL;

            if ( !SetThreadContext( thread, &context ) ) {
                resume_thread_fully( thread );
                CloseHandle( thread );
                continue;
            }

            resume_thread_fully( thread );

            bool done = false;
            const bool poll_ok = poll_remote_stub_done( remote_process, remote_result, remaining_ms, done );

            if ( SuspendThread( thread ) != static_cast< DWORD >( -1 ) ) {
                SetThreadContext( thread, &saved_context );
                resume_thread_fully( thread );
            } else {
                resume_thread_fully( thread );
            }

            CloseHandle( thread );

            if ( poll_ok && done )
                return true;

            Sleep( 2 );
        }

        return false;
    }

    bool execute_remote_stub_via_apc(
        HANDLE remote_process,
        u32 process_id,
        PVOID remote_code,
        LPCVOID remote_result,
        DWORD timeout_ms
    )
    {
        static const auto nt_queue_apc_thread_ex = get_nt_function<t_nt_queue_apc_thread_ex>( "NtQueueApcThreadEx" );
        static const auto nt_alert_thread = get_nt_function<t_nt_alert_thread>( "NtAlertThread" );
        if ( nt_queue_apc_thread_ex == nullptr )
            return false;

        if ( !reset_remote_stub_state( remote_process, remote_result ) )
            return false;

        constexpr ULONG k_special_user_apc = 1;
        constexpr ACCESS_MASK k_thread_access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION;

        std::vector<HANDLE> alerted_threads;
        alerted_threads.reserve( 32 );

        for ( const u32 thread_id : enumerate_process_threads( process_id ) ) {
            HANDLE thread = OpenThread( k_thread_access, FALSE, thread_id );
            if ( thread == nullptr )
                continue;

            const LONG apc_status = nt_queue_apc_thread_ex(
                thread,
                nullptr,
                k_special_user_apc,
                remote_code,
                nullptr,
                nullptr,
                nullptr
            );

            if ( !nt_success( apc_status ) ) {
                CloseHandle( thread );
                continue;
            }

            if ( nt_alert_thread != nullptr )
                nt_alert_thread( thread );

            alerted_threads.push_back( thread );
        }

        bool done = false;
        const bool poll_ok = !alerted_threads.empty()
            && poll_remote_stub_done( remote_process, remote_result, timeout_ms, done );

        for ( HANDLE thread : alerted_threads )
            CloseHandle( thread );

        return poll_ok && done;
    }

    bool execute_remote_stub_stealth(
        HANDLE remote_process,
        u32 process_id,
        PVOID remote_code,
        LPCVOID remote_result,
        u64 remote_stack_top,
        usize stub_size,
        DWORD timeout_ms
    )
    {
        if ( remote_process == nullptr || remote_code == nullptr || remote_result == nullptr || remote_stack_top == 0 )
            return false;

        if ( !reset_remote_stub_state( remote_process, remote_result ) || !patch_remote_stub_return( remote_process, remote_code, stub_size ) )
            return false;

        // Sem NtCreateThreadEx / CreateRemoteThread (Sysmon Event 8): APC primeiro, hijack como fallback.
        if ( execute_remote_stub_via_apc( remote_process, process_id, remote_code, remote_result, timeout_ms ) )
            return true;

        if ( !reset_remote_stub_state( remote_process, remote_result ) || !patch_remote_stub_spin_wait( remote_process, remote_code, stub_size ) )
            return false;

        return execute_remote_stub_via_hijack( remote_process, process_id, remote_code, remote_result, remote_stack_top, stub_size, timeout_ms );
    }

    HANDLE duplicate_process_handle( HANDLE source_handle, ACCESS_MASK desired_access = 0 )
    {
        if ( source_handle == nullptr )
            return nullptr;

        HANDLE duplicated = nullptr;
        const DWORD options = desired_access != 0 ? 0 : DUPLICATE_SAME_ACCESS;
        const ACCESS_MASK access = desired_access != 0 ? desired_access : 0;
        if ( !DuplicateHandle( GetCurrentProcess( ), source_handle, GetCurrentProcess( ), &duplicated, access, FALSE, options ) )
            return nullptr;

        return duplicated;
    }

    HANDLE open_process_stealth(u32 target_pid, ACCESS_MASK desired_access)
    {
        HANDLE h_token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &h_token)) {
            TOKEN_PRIVILEGES tp { };
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            LookupPrivilegeValueA(nullptr, XorStr("SeDebugPrivilege"), &tp.Privileges[0].Luid);
            AdjustTokenPrivileges(h_token, FALSE, &tp, 0, nullptr, nullptr);
            CloseHandle(h_token);
        }

        using t_nt_qsi = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
        static const auto NtQSI = get_nt_function<t_nt_qsi>(XorStr("NtQuerySystemInformation"));
        if (!NtQSI) return nullptr;

        struct HE {
            PVOID      Object;
            ULONG_PTR  PID;
            ULONG_PTR  Handle;
            ULONG      Access;
            USHORT     BackTrace;
            USHORT     TypeIndex;
            ULONG      Attributes;
            ULONG      Reserved;
        };
        struct HI {
            ULONG_PTR Count;
            ULONG_PTR Reserved;
            HE Entries[1];
        };

        ULONG buf_size = 64 * 1024 * 1024;
        std::unique_ptr<BYTE[]> buffer;
        LONG status = -1;

        for (int i = 0; i < 10; i++) {
            buffer = std::make_unique<BYTE[]>(buf_size);
            if (!buffer) return nullptr;
            ULONG needed = 0;
            status = NtQSI(64, buffer.get(), buf_size, &needed);
            if (status == 0) break;
            if (status != static_cast<LONG>(0xC0000004L)) break;
            buf_size = (needed > buf_size) ? (needed + 4 * 1024 * 1024) : (buf_size * 2);
        }

        if (status != 0) return nullptr;

        auto* info = reinterpret_cast<HI*>(buffer.get());
        DWORD my_pid = GetCurrentProcessId();

        std::unordered_map<DWORD, HANDLE> src_cache;
        HANDLE result = nullptr;

        for (ULONG_PTR i = 0; i < info->Count && !result; i++) {
            auto& e = info->Entries[i];

            if (static_cast<DWORD>(e.PID) == my_pid || e.PID <= 4)
                continue;

            if ((e.Access & desired_access) != desired_access)
                continue;

            DWORD src_pid = static_cast<DWORD>(e.PID);

            HANDLE src_proc = nullptr;
            auto it = src_cache.find(src_pid);
            if (it != src_cache.end()) {
                src_proc = it->second;
                if (!src_proc) continue;
            } else {
                src_proc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, src_pid);
                src_cache[src_pid] = src_proc;
                if (!src_proc) continue;
            }

            HANDLE dup = nullptr;
            BOOL ok = DuplicateHandle(
                src_proc,
                reinterpret_cast<HANDLE>(e.Handle),
                GetCurrentProcess(),
                &dup,
                desired_access,
                FALSE,
                0
            );

            if (ok && dup != nullptr) {
                DWORD target_pid_real = 0;
                static const auto nt_qip = get_nt_function<t_nt_query_information_process>(XorStr("NtQueryInformationProcess"));
                if (nt_qip != nullptr) {
                    c_process_basic_information pbi{ };
                    if (nt_success(nt_qip(dup, e_process_info_class::process_basic_information, &pbi, sizeof(pbi), nullptr))) {
                        target_pid_real = static_cast<DWORD>(pbi.unique_process_id);
                    }
                }
                if (target_pid_real == target_pid) {
                    result = dup;
                } else {
                    CloseHandle(dup);
                }
            }
        }

        for (auto& pair : src_cache) {
            if (pair.second != nullptr) {
                CloseHandle(pair.second);
            }
        }

        return result;
    }

    HANDLE open_remote_exec_process( u32 process_id, HANDLE existing_handle )
    {
        constexpr ACCESS_MASK k_exec_access =
            PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_CREATE_THREAD;

        HANDLE remote_process = duplicate_process_handle( existing_handle, k_exec_access );
        if ( remote_process != nullptr )
            return remote_process;

        remote_process = open_process_stealth( process_id, k_exec_access );
        if ( remote_process != nullptr )
            return remote_process;

        return open_process_nt( process_id, k_exec_access );
    }

    // Note: utility::hex, utility::sanitize_guest_string, and utility::is_likely_guest_kernel_pointer 
    // are already defined in kernel_structs.cpp and kernel_structs.hpp, so we do not define them here.
}

namespace mem {

    constexpr c_dynamic_offsets k_profile_bstk_vmm = {
        e_vm_build_variant::bstkvmm,
        "a",
        false,        // cpu_array_is_inline
        0x120000,     // min_region_size
        0x30,         // range_host_base
        0x3C,         // vm_cpu_count
        0x10AFF0,     // vm_read_counter
        0x104000,     // vm_direct_ram_window
        0x106000,     // vm_direct_ram_tag
        0x106019,     // vm_ram_range_mode
        0x106050,     // vm_range_cache
        0x106098,     // vm_range_tree_root
        0x1060B0,     // vm_mmio2_table
        0x106860,     // vm_chunk_cache
        0x106C60,     // vm_chunk_tree_root
        0x106C80,     // vm_page_map_cache
        0x11FE00,     // vm_cpu_array
        0x120000,     // expected_pvcpu_delta
        0x50,         // range_left
        0x58,         // range_right
        0x70,         // range_page_desc_table
        0x0,          // chunk_node_left
        0x8,          // chunk_node_right
        0x10,         // chunk_node_key
        0x28,         // chunk_host_base
        0x70,         // mmio_range_host_base
        0xDA070,      // bstk_map_chunk_rva
        0x106C68      // chunk_count_offset
    };

    constexpr c_dynamic_offsets k_profile_bstk = {
        e_vm_build_variant::bstk,
        "b",
        true,         // cpu_array_is_inline
        0x30000,      // min_region_size
        0x30,         // range_host_base
        0x38,         // vm_cpu_count
        0x0,          // vm_read_counter (no temporal counter)
        0x0,          // vm_direct_ram_window
        0x0,          // vm_direct_ram_tag
        0x0,          // vm_ram_range_mode
        0xFD0,        // vm_range_cache
        0x1018,       // vm_range_tree_root
        0x1058,       // vm_mmio2_table
        0x1348,       // vm_chunk_cache
        0x1340,       // vm_chunk_tree_root
        0x0,          // vm_page_map_cache
        0x24000,      // vm_cpu_array
        0x0,          // expected_pvcpu_delta (unused when inline)
        0x58,         // range_left
        0x60,         // range_right
        0x80,         // range_page_desc_table
        0x8,          // chunk_node_left
        0x10,         // chunk_node_right
        0x0,          // chunk_node_key
        0x30,         // chunk_host_base
        0x60,         // mmio_range_host_base
        0xDA070,      // bstk_map_chunk_rva
        0x1748        // chunk_count_offset
    };

    c_memory_engine::~c_memory_engine()
    {
        shutdown();
    }

    bool c_memory_engine::initialize(bool verbose)
    {
        reset();

        const auto process_id_result = find_process_id_by_name(config::default_process_name());
        if (!process_id_result.has_value()) {
            utility::verbose_log(verbose, XorStrS("[mem] process not found"));
            return false;
        }

        utility::verbose_log(verbose, XorStrS("[mem] process id=") + std::to_string(*process_id_result));

        const auto desired_access = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
        process_handle = utility::open_process_stealth(*process_id_result, desired_access);
        if (process_handle == nullptr) {
            utility::verbose_log(verbose, XorStrS("[mem] stealth handle failed, falling back to direct open"));
            process_handle = utility::open_process_nt(*process_id_result, desired_access);
        }
        if (process_handle == nullptr) {
            utility::verbose_log(verbose, XorStrS("[mem] open process failed"));
            reset();
            return false;
        }

        process_id_value = *process_id_result;

        const auto module_result = find_module_in_process(process_handle, config::default_module_name());
        if (!module_result.has_value()) {
            utility::verbose_log(verbose, XorStrS("[mem] module not found"));
            reset();
            return false;
        }

        module_info_value = *module_result;
        bstk_module_base  = module_info_value.base;
        utility::verbose_log(verbose, XorStrS("[mem] module base=") + utility::hex(module_info_value.base));

        // Pattern scan for the physical-chunk-map function in bstkvmm.dll.
        // Primary pattern (v6.1.36+): function saves rbx/rsi/rdi, pushes r14, sets up frame,
        // then atomically increments the VM chunk-tree counter [rcx+0x106C70] and saves r8 (output
        // ptr) into r14.  Matches exactly 1 function in the DLL.
        // Legacy pattern (pre-v6.1.36): saves rbx/rbp/rsi, push rdi, sub rsp 30, mov rbx r8.
        std::string pattern_to_find =
            XorStrS("48 89 5c 24 ? 48 89 74 24 ? 48 89 7c 24 ? 41 56 48 83 ec ? 83 81 70 6c 10 00 01 4d 8b f0");
        std::string pattern_legacy  =
            XorStrS("48 89 5c 24 ? 48 89 6c 24 ? 48 89 74 24 ? 57 48 83 ec 30 49 8b d8");

        auto scan_pattern_local = [this](u64 base, u64 size, const std::string& pat) -> u64 {
            std::vector<std::pair<BYTE, bool>> parsed;
            std::stringstream ss(pat);
            std::string tok;
            auto parse_hex_byte = [](const std::string& value, BYTE& out) -> bool {
                if (value.empty() || value.size() > 2) return false;
                unsigned parsed_value = 0;
                for (char ch : value) {
                    parsed_value <<= 4;
                    if (ch >= '0' && ch <= '9') parsed_value |= static_cast<unsigned>(ch - '0');
                    else if (ch >= 'a' && ch <= 'f') parsed_value |= static_cast<unsigned>(ch - 'a' + 10);
                    else if (ch >= 'A' && ch <= 'F') parsed_value |= static_cast<unsigned>(ch - 'A' + 10);
                    else return false;
                }
                out = static_cast<BYTE>(parsed_value & 0xFF);
                return true;
            };
            while (ss >> tok) {
                if (tok == XorStrS("?") || tok == XorStrS("??")) {
                    parsed.push_back({0, true});
                } else {
                    BYTE parsed_byte = 0;
                    if (parse_hex_byte(tok, parsed_byte)) parsed.push_back({parsed_byte, false});
                }
            }
            if (parsed.empty()) return 0;
            
            std::vector<BYTE> buf(size, 0);
            std::vector<bool> page_readable(size / 0x1000 + 1, false);

            for (size_t offset = 0; offset < size; offset += 0x1000) {
                size_t read_size = std::min<size_t>(0x1000, size - offset);
                SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(base + offset), buf.data() + offset, read_size, &bytes_read) && bytes_read > 0) {
                    page_readable[offset / 0x1000] = true;
                }
            }

            for (size_t i = 0; i < size - parsed.size(); ++i) {
                size_t start_page = i / 0x1000;
                size_t end_page = (i + parsed.size() - 1) / 0x1000;
                
                bool pages_ok = true;
                for (size_t p = start_page; p <= end_page; ++p) {
                    if (p < page_readable.size() && !page_readable[p]) {
                        pages_ok = false;
                        break;
                    }
                }
                if (!pages_ok) continue;

                bool ok = true;
                for (size_t j = 0; j < parsed.size(); ++j) {
                    if (!parsed[j].second && buf[i + j] != parsed[j].first) {
                        ok = false;
                        break;
                    }
                }
                if (ok) return base + i;
            }
            return 0;
        };

        u64 found_fn = scan_pattern_local(bstk_module_base, module_info_value.size, pattern_to_find);
        if (found_fn == 0)
            found_fn = scan_pattern_local(bstk_module_base, module_info_value.size, pattern_legacy);
        if (found_fn != 0) {
            bstk_map_chunk_rva_value = found_fn - bstk_module_base;
            utility::verbose_log(verbose, XorStrS("[mem] bstk_map_chunk_rva encontrado dinamicamente: ") + utility::hex(bstk_map_chunk_rva_value));
        } else {
            // Fallback for BstkVMM.dll 6.1.36.156792 (BlueStacks 5.22.210):
            // pgmPhysChunkMap is at RVA 0xDA0C0 in this version.
            // The old fallback 0xDA070 pointed to the MIDDLE of a larger function — wrong!
            bstk_map_chunk_rva_value = 0xDA0C0;
            utility::verbose_log(verbose, XorStrS("[mem] Pattern scan failed. Using hardcoded fallback RVA=0xDA0C0."));
        }

        const auto vm_result = locate_vm_info(verbose);
        if (!vm_result.has_value()) {
            utility::verbose_log(verbose, XorStrS("[mem] vm info not found"));
            reset();
            return false;
        }

        vm_info_value = *vm_result;

        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex);
            gva_to_physical_page_cache.reserve(65536);
            gva_to_host_page_cache.reserve(65536);
            phys_to_host_page_cache.reserve(65536);
        }

        utility::verbose_log(verbose, XorStrS("[mem] memory engine initialized successfully"));
        return true;
    }

    void c_memory_engine::shutdown()
    {
        reset();
    }

    bool c_memory_engine::is_initialized() const
    {
        return process_handle != nullptr && process_id_value != 0 && vm_info_value.pvm != 0 && vm_info_value.pvcpu_0 != 0;
    }

    HANDLE c_memory_engine::process() const
    {
        return process_handle;
    }

    u32 c_memory_engine::process_id() const
    {
        return process_id_value;
    }

    const c_module_info& c_memory_engine::module() const
    {
        return module_info_value;
    }

    const c_vm_info& c_memory_engine::vm_info() const
    {
        return vm_info_value;
    }

    u64 c_memory_engine::pvm() const
    {
        return vm_info_value.pvm;
    }

    u64 c_memory_engine::pvcpu() const
    {
        return vm_info_value.pvcpu_0;
    }

    void c_memory_engine::clear_runtime_caches()
    {
        clear_translation_caches();
    }

    void c_memory_engine::clear_translation_caches()
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        gva_to_physical_page_cache.clear();
        gva_to_host_page_cache.clear();
        phys_to_host_page_cache.clear();
    }

    void c_memory_engine::reset()
    {
        clear_translation_caches();

        if (process_handle != nullptr) {
            CloseHandle(process_handle);
            process_handle = nullptr;
        }

        process_id_value  = 0;
        module_info_value = { };
        vm_info_value     = { };
        bstk_module_base  = 0;
        bstk_map_chunk_rva_value = 0;
        dyn_offsets = { };
    }

    bool c_memory_engine::read_host_bytes(u64 address, void* buffer, usize size) const
    {
        if (process_handle == nullptr || buffer == nullptr || size == 0)
            return false;

        SIZE_T bytes_read { };
        if (!ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read))
            return false;

        return bytes_read == size;
    }

    bool c_memory_engine::write_host_bytes(u64 address, const void* buffer, usize size) const
    {
        if (process_handle == nullptr || buffer == nullptr || size == 0)
            return false;

        SIZE_T bytes_written { };
        const bool success = WriteProcessMemory(process_handle, reinterpret_cast<LPVOID>(address), buffer, size, &bytes_written);

        return success && bytes_written == size;
    }

    bool c_memory_engine::validate_vm_candidate(u64 candidate, u64 region_size, const c_dynamic_offsets& p, c_vm_info& out_info, bool verbose) const
    {
        // Diagnostic: only log "near-misses" (regions that pass the cheap cpu_count
        // filter) so we can see WHICH check rejects the real VM, without flooding the
        // console with the thousands of regions that fail trivially. Capped to avoid spam.
        static int s_near_miss_logged = 0;
        auto near_miss = [&](const char* stage, u64 a = 0, u64 b = 0) {
            if (verbose && s_near_miss_logged < 40) {
                s_near_miss_logged++;
                std::cout << XorStr("[mem][vm-miss] profile=") << p.name << XorStr(" cand=0x") << std::hex << candidate
                          << XorStr(" stage=") << stage << XorStr(" v1=0x") << a << XorStr(" v2=0x") << b << std::dec << "\n" << std::flush;
            }
        };

        if (region_size < p.min_region_size)
            return false;

        u32 cpu_count = 0;
        if (!read_host_value(candidate + p.vm_cpu_count, cpu_count))
            return false;
        if (cpu_count < 1 || cpu_count > 8)
            return false;

        // From here on, the region looked plausible (valid cpu_count) — log rejections.
        u64 cpu0 = 0;
        if (p.cpu_array_is_inline) {
            cpu0 = candidate + p.vm_cpu_array;
        } else {
            if (p.vm_cpu_array + sizeof(u64) > region_size)
                { near_miss(XorStr("cpu_array_oob"), p.vm_cpu_array, region_size); return false; }
            if (!read_host_value(candidate + p.vm_cpu_array, cpu0))
                { near_miss(XorStr("cpu0_read")); return false; }
            if (!utility::is_likely_user_pointer(cpu0))
                { near_miss(XorStr("cpu0_not_ptr"), cpu0); return false; }
            if (p.expected_pvcpu_delta != 0) {
                s64 diff = static_cast<s64>(cpu0 - candidate);
                if (diff < static_cast<s64>(p.expected_pvcpu_delta) - 0x10000 || diff > static_cast<s64>(p.expected_pvcpu_delta) + 0x10000)
                    { near_miss(XorStr("pvcpu_delta"), (u64)diff, p.expected_pvcpu_delta); return false; }
            }
        }

        u64 read_count = 0;
        if (p.vm_read_counter != 0) {
            if (!read_host_value(candidate + p.vm_read_counter, read_count))
                { near_miss(XorStr("readcnt_read")); return false; }
            if (read_count == 0 || read_count > utility::k_read_counter_limit)
                { near_miss(XorStr("readcnt_range"), read_count); return false; }
        }

        u64 range_tree_root = 0;
        if (!read_host_value(candidate + p.vm_range_tree_root, range_tree_root))
            { near_miss(XorStr("rangeroot_read")); return false; }
        if (!utility::is_likely_user_pointer(range_tree_root))
            { near_miss(XorStr("rangeroot_not_ptr"), range_tree_root); return false; }

        u64 range_gcphys = 0;
        u64 range_cb = 0;
        if (!read_host_value(range_tree_root + 0x0, range_gcphys) || !read_host_value(range_tree_root + 0x8, range_cb))
            { near_miss(XorStr("range_fields_read")); return false; }

        if ((range_gcphys & 0xFFFULL) != 0 || (range_cb & 0xFFFULL) != 0 || range_cb == 0 || range_cb > 0x1000000000ULL)
            { near_miss(XorStr("range_sanity"), range_gcphys, range_cb); return false; }

        if (p.chunk_count_offset != 0) {
            u32 chunk_count = 0;
            if (!read_host_value(candidate + p.chunk_count_offset, chunk_count))
                { near_miss(XorStr("chunkcnt_read")); return false; }
            if (chunk_count > 0x10000)
                { near_miss(XorStr("chunkcnt_range"), chunk_count); return false; }
        }

        if (verbose)
            std::cout << XorStr("[mem][vm-hit] profile=") << p.name << XorStr(" cand=0x") << std::hex << candidate
                      << XorStr(" cpu0=0x") << cpu0 << std::dec << XorStr(" cpus=") << cpu_count << "\n" << std::flush;

        out_info.pvm          = candidate;
        out_info.pvcpu_0      = cpu0;
        out_info.cpu_count    = cpu_count;
        out_info.read_counter = read_count;
        out_info.region_size  = region_size;

        return true;
    }

    std::optional<c_vm_info> c_memory_engine::locate_vm_info(bool verbose) const
    {
        MEMORY_BASIC_INFORMATION memory_info { };
        auto* current_address = static_cast<u8*>(nullptr);

        const c_dynamic_offsets* profiles[] = { &k_profile_bstk_vmm, &k_profile_bstk };

        u64 regions_scanned = 0;        // total RW commit regions >= 128KB we tested
        u64 largest_region  = 0;        // helps confirm the guest RAM is even mapped

        while (VirtualQueryEx(process_handle, current_address, &memory_info, sizeof(memory_info)) == sizeof(memory_info)) {
            const auto protection = memory_info.Protect & 0xFF;
            const bool readable_writable = protection == PAGE_READWRITE || protection == PAGE_EXECUTE_READWRITE;

            if (memory_info.State == MEM_COMMIT && readable_writable && memory_info.RegionSize >= (128 * 1024)) {
                const auto candidate = static_cast<u64>(reinterpret_cast<uptr>(memory_info.BaseAddress));
                regions_scanned++;
                if (memory_info.RegionSize > largest_region) largest_region = memory_info.RegionSize;

                for (const auto* p : profiles) {
                    c_vm_info info { };
                    if (validate_vm_candidate(candidate, memory_info.RegionSize, *p, info, verbose)) {
                        this->dyn_offsets = *p;
                        this->dyn_offsets.bstk_map_chunk_rva = bstk_map_chunk_rva_value;

                        if (p->variant == e_vm_build_variant::bstkvmm) {
                            s64 shift = static_cast<s64>(info.pvcpu_0 - candidate) - p->expected_pvcpu_delta;
                            if (shift != 0) {
                                this->dyn_offsets.vm_chunk_cache += shift;
                                this->dyn_offsets.vm_chunk_tree_root += shift;
                                this->dyn_offsets.vm_page_map_cache += shift;
                                this->dyn_offsets.vm_range_cache += shift;
                                this->dyn_offsets.vm_range_tree_root += shift;
                                this->dyn_offsets.vm_mmio2_table += shift;
                                this->dyn_offsets.vm_direct_ram_tag += shift;
                                this->dyn_offsets.vm_ram_range_mode += shift;
                                this->dyn_offsets.vm_direct_ram_window += shift;
                                this->dyn_offsets.chunk_count_offset += shift;
                                utility::verbose_log(verbose, XorStrS("[mem] VM Shift detectado e aplicado nos offsets: ") + std::to_string(shift));
                            }
                        }

                        utility::verbose_log(verbose, XorStrS("[mem] VM localizada dinamicamente! Base: ") + utility::hex(info.pvm) + XorStrS(" (perfil: ") + p->name + XorStrS(")"));
                        return info;
                    }
                }
            }

            const auto next_address_value = static_cast<uptr>(reinterpret_cast<uptr>(current_address) + memory_info.RegionSize);
            if (next_address_value <= reinterpret_cast<uptr>(current_address)) {
                break;
            }

            current_address = reinterpret_cast<u8*>(next_address_value);
        }

        utility::verbose_log(verbose, XorStrS("[mem] vm scan: nenhuma regiao validou. regioes_testadas=")
            + std::to_string(regions_scanned) + XorStrS(" maior_regiao=") + utility::hex(largest_region));
        return std::nullopt;
    }

    std::optional<u32> c_memory_engine::find_process_id_by_name(const std::wstring& process_name)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return std::nullopt;

        PROCESSENTRY32W entry{ };
        entry.dwSize = sizeof(entry);
        const std::wstring wanted_name = utility::to_lower_wide(process_name);
        std::optional<u32> pid;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (utility::to_lower_wide(entry.szExeFile) == wanted_name) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return pid;
    }

    std::optional<c_module_info> c_memory_engine::find_module_in_process(HANDLE proc_handle, const std::wstring& module_name)
    {
        static const auto nt_query_information_process = utility::get_nt_function<utility::t_nt_query_information_process>(XorStr("NtQueryInformationProcess"));
        if (nt_query_information_process == nullptr)
            return std::nullopt;

        if (proc_handle == nullptr)
            return std::nullopt;

        utility::c_process_basic_information process_information { };
        if (!utility::nt_success(nt_query_information_process(proc_handle, utility::e_process_info_class::process_basic_information, &process_information, sizeof(process_information), nullptr))) {
            return std::nullopt;
        }

        if (process_information.peb_base_address == nullptr) {
            return std::nullopt;
        }

        utility::c_peb_ldr_data loader_data { };
        PVOID loader_pointer { };
        if (!ReadProcessMemory(proc_handle, reinterpret_cast<PVOID>(reinterpret_cast<ULONG_PTR>(process_information.peb_base_address) + offsets::peb_ldr), &loader_pointer, sizeof(loader_pointer), nullptr) || loader_pointer == nullptr) {
            return std::nullopt;
        }

        if (!ReadProcessMemory(proc_handle, loader_pointer, &loader_data, sizeof(loader_data), nullptr)) {
            return std::nullopt;
        }

        const auto wanted_name = utility::to_lower_wide(module_name);
        auto* start_node = &loader_data.in_load_order_module_list;
        auto* current_node = loader_data.in_load_order_module_list.Flink;

        for (s32 index = 0; index < 512 && current_node != start_node && current_node != nullptr; ++index) {
            utility::c_ldr_data_table_entry loader_entry { };
            if (!ReadProcessMemory(proc_handle, current_node, &loader_entry, sizeof(loader_entry), nullptr)) {
                break;
            }

            if (loader_entry.base_dll_name.buffer != nullptr) {
                std::wstring base_name(loader_entry.base_dll_name.length / sizeof(wchar_t), L'\0');
                if (ReadProcessMemory(proc_handle, loader_entry.base_dll_name.buffer, base_name.data(), loader_entry.base_dll_name.length, nullptr) && utility::to_lower_wide(base_name) == wanted_name) {
                    c_module_info result { };
                    result.name = std::move(base_name);
                    result.base = static_cast<u64>(reinterpret_cast<uptr>(loader_entry.dll_base));
                    result.size = loader_entry.size_of_image;

                    if (loader_entry.full_dll_name.buffer != nullptr && loader_entry.full_dll_name.length != 0) {
                        std::wstring full_path(loader_entry.full_dll_name.length / sizeof(wchar_t), L'\0');
                        if (ReadProcessMemory(proc_handle, loader_entry.full_dll_name.buffer, full_path.data(), loader_entry.full_dll_name.length, nullptr)) {
                            result.path = std::move(full_path);
                        }
                    }

                    return result;
                }
            }

            current_node = loader_entry.in_load_order_links.Flink;
        }

        return std::nullopt;
    }

    std::optional<u64> c_memory_engine::find_physical_range(u64 guest_phys) const
    {
        const auto range_contains = [ & ] ( u64 candidate ) -> std::optional<bool>
        {
            if ( candidate == 0 )
                return false;
            u64 start = 0;
            u64 size = 0;
            if ( !read_host_value( candidate + 0x0, start ) || !read_host_value( candidate + 0x8, size ) )
                return std::nullopt;
            return guest_phys >= start && guest_phys - start < size;
        };

        const auto slot = ( guest_phys >> 20 ) & 0x7ULL;
        const auto cache_addr = vm_info_value.pvm + this->dyn_offsets.vm_range_cache + slot * sizeof( u64 );

        u64 node = 0;
        if ( !read_host_value( cache_addr, node ) )
            return std::nullopt;

        if ( node != 0 )
        {
            const auto contains = range_contains( node );
            if ( !contains.has_value( ) )
                return std::nullopt;
            if ( *contains )
                return node;
        }

        if ( !read_host_value( vm_info_value.pvm + this->dyn_offsets.vm_range_tree_root, node ) )
            return std::nullopt;

        while ( node != 0 )
        {
            u64 start = 0;
            u64 size = 0;
            if ( !read_host_value( node + 0x0, start ) || !read_host_value( node + 0x8, size ) )
                return std::nullopt;

            if ( guest_phys >= start && guest_phys - start < size )
                return node;

            const u64 child_offset = guest_phys < start ? this->dyn_offsets.range_left : this->dyn_offsets.range_right;
            if ( !read_host_value( node + child_offset, node ) )
                return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<u64> c_memory_engine::find_chunk_from_cache(u32 chunk_id) const
    {
        const auto slot = chunk_id & 0x3F;
        const auto slot_addr = vm_info_value.pvm + this->dyn_offsets.vm_chunk_cache + static_cast< u64 >( slot ) * 0x10;

        u32 cached_id = 0;
        u64 chunk_ptr = 0;
        if ( !read_host_value( slot_addr, cached_id ) || !read_host_value( slot_addr + 0x8, chunk_ptr ) )
            return std::nullopt;

        if ( cached_id != chunk_id || !utility::is_likely_user_pointer( chunk_ptr ) )
            return std::nullopt;

        return chunk_ptr;
    }

    std::optional<u64> c_memory_engine::find_chunk_from_tree(u32 chunk_id) const
    {
        u64 node = 0;
        if ( !read_host_value( vm_info_value.pvm + this->dyn_offsets.vm_chunk_tree_root, node ) )
            return std::nullopt;

        while ( node != 0 )
        {
            u32 key = 0;
            if ( !read_host_value( node + this->dyn_offsets.chunk_node_key, key ) )
                return std::nullopt;

            if ( key == chunk_id )
                return node;

            const u64 next_offset = chunk_id < key ? this->dyn_offsets.chunk_node_left : this->dyn_offsets.chunk_node_right;
            if ( !read_host_value( node + next_offset, node ) )
                return std::nullopt;
        }

        return std::nullopt;
    }

    bool c_memory_engine::map_chunk_via_bstk_vmm(u32 chunk_id, u64& out_chunk_pointer, bool verbose)
    {
        out_chunk_pointer = 0;
        if ( bstk_module_base == 0 || process_id_value == 0 )
            return false;

        if ( this->dyn_offsets.bstk_map_chunk_rva == 0 )
            return false;

        HANDLE remote_process = utility::open_remote_exec_process( process_id_value, process_handle );
        if ( remote_process == nullptr )
            return false;

        struct c_remote_chunk_map_result {
            u64 chunk_ptr = 0;
            s64 status = 0;
            u32 done = 0;
            u32 pad = 0;
        };

        const auto total_size = sizeof( c_remote_chunk_map_result ) + utility::k_remote_stub_size + utility::k_remote_stub_stack;
        auto cleanup = [ & ] ( LPVOID remote_mem )
        {
            if ( remote_mem != nullptr )
                VirtualFreeEx( remote_process, remote_mem, 0, MEM_RELEASE );
            CloseHandle( remote_process );
        };

        u8* remote_mem = static_cast< u8* >( VirtualAllocEx( remote_process, nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
        if ( remote_mem == nullptr )
        {
            cleanup( nullptr );
            return false;
        }

        const u64 remote_result = reinterpret_cast< u64 >( remote_mem );
        const u64 remote_code = remote_result + sizeof( c_remote_chunk_map_result );
        const u64 remote_func = bstk_module_base + this->dyn_offsets.bstk_map_chunk_rva;

        std::vector<u8> stub = {
            0x48, 0x83, 0xEC, 0x28,
            0x48, 0xB9,
            0, 0, 0, 0, 0, 0, 0, 0,
            0xBA,
            0, 0, 0, 0,
            0x49, 0xB8,
            0, 0, 0, 0, 0, 0, 0, 0,
            0x48, 0xB8,
            0, 0, 0, 0, 0, 0, 0, 0,
            0xFF, 0xD0,
            0x48, 0xB9,
            0, 0, 0, 0, 0, 0, 0, 0,
            0x48, 0x63, 0xC0,
            0x48, 0x89, 0x41, 0x08,
            0xC7, 0x41, 0x10, 0x01, 0x00, 0x00, 0x00,
            0x48, 0x83, 0xC4, 0x28,
            0xC3
        };

        const auto patch_u64 = [ & ] ( usize offset, u64 value )
        {
            std::memcpy( stub.data( ) + offset, &value, sizeof( value ) );
        };
        const auto patch_u32 = [ & ] ( usize offset, u32 value )
        {
            std::memcpy( stub.data( ) + offset, &value, sizeof( value ) );
        };

        patch_u64( 6, vm_info_value.pvm );
        patch_u32( 15, chunk_id );
        patch_u64( 21, remote_result );
        patch_u64( 31, remote_func );
        patch_u64( 43, remote_result );

        c_remote_chunk_map_result initial{ };
        SIZE_T written = 0;
        if ( !WriteProcessMemory( remote_process, remote_mem, &initial, sizeof( initial ), &written ) || written != sizeof( initial ) || !WriteProcessMemory( remote_process, reinterpret_cast< LPVOID >( remote_code ), stub.data( ), stub.size( ), &written ) || written != stub.size( ) )
        {
            cleanup( remote_mem );
            return false;
        }

        HANDLE thread = CreateRemoteThread(
            remote_process,
            nullptr,
            0,
            reinterpret_cast< LPTHREAD_START_ROUTINE >( remote_code ),
            nullptr,
            0,
            nullptr );
        if ( thread == nullptr )
        {
            cleanup( remote_mem );
            return false;
        }

        const DWORD wait = WaitForSingleObject( thread, config::k_remote_thread_timeout_ms );
        CloseHandle( thread );
        if ( wait != WAIT_OBJECT_0 )
        {
            cleanup( remote_mem );
            return false;
        }

        c_remote_chunk_map_result result{ };
        SIZE_T read = 0;
        const bool ok = ReadProcessMemory( remote_process, remote_mem, &result, sizeof( result ), &read ) && read == sizeof( result );
        cleanup( remote_mem );
        if ( !ok || result.status < 0 || result.chunk_ptr == 0 )
            return false;

        out_chunk_pointer = result.chunk_ptr;
        return true;
    }

    std::optional<u64> c_memory_engine::try_page_map_cache(u64 guest_phys) const
    {
        if ( this->dyn_offsets.vm_page_map_cache == 0 )
            return std::nullopt;

        const auto slot = ( guest_phys >> 12 ) & 0xFFULL;
        const auto entry = vm_info_value.pvm + this->dyn_offsets.vm_page_map_cache + slot * 0x20;
        const auto page_tag = guest_phys & ~0xFFFULL;

        u64 cached_tag = 0;
        u64 host_base = 0;
        u64 page_desc = 0;
        if ( !read_host_value( entry + 0x0, cached_tag ) || !read_host_value( entry + 0x8, page_desc ) || !read_host_value( entry + 0x18, host_base ) )
            return std::nullopt;

        if ( cached_tag == page_tag && utility::is_likely_user_pointer( host_base ) )
        {
            return host_base | ( guest_phys & 0xFFFULL );
        }
        return std::nullopt;
    }

    std::optional<u64> c_memory_engine::map_physical_page_to_host_base(u64 guest_phys)
    {
        const auto range = find_physical_range( guest_phys );
        if ( !range.has_value( ) )
            return std::nullopt;

        u64 range_start = 0;
        if ( !read_host_value( *range + 0x0, range_start ) )
            return std::nullopt;

        const auto page_index = ( guest_phys - range_start ) >> 12;
        const auto desc = *range + this->dyn_offsets.range_page_desc_table + page_index * 0x10;

        u64 state = 0;
        u32 info = 0;
        if ( !read_host_value( desc + 0x0, state ) || !read_host_value( desc + 0x8, info ) )
            return std::nullopt;

        const auto page_class = static_cast< u32 >( ( state >> 51 ) & 0x7 );
        const auto read_map_kind = static_cast< u32 >( ( state >> 48 ) & 0x3 );
        u8 ram_range_mode = 0;
        if ( this->dyn_offsets.vm_ram_range_mode != 0 )
        {
            if ( !read_host_value( vm_info_value.pvm + this->dyn_offsets.vm_ram_range_mode, ram_range_mode ) )
                ram_range_mode = 0;
        }

        if ( page_class != 2 && page_class != 3 && ram_range_mode != 0 )
        {
            u64 range_host_base = 0;
            if ( !read_host_value( *range + this->dyn_offsets.range_host_base, range_host_base ) || !utility::is_likely_user_pointer( range_host_base ) )
                return std::nullopt;
            return range_host_base + ( ( guest_phys - range_start ) & ~0xFFFULL );
        }

        if ( read_map_kind == 0 && this->dyn_offsets.vm_direct_ram_window != 0 )
        {
            return vm_info_value.pvm + this->dyn_offsets.vm_direct_ram_window;
        }

        if ( page_class == 2 || page_class == 3 )
        {
            const u32 mmio_id = info >> 24;
            const u32 mmio_page = info & 0x00FFFFFF;

            u64 mmio_range = 0;
            if ( !read_host_value( vm_info_value.pvm + this->dyn_offsets.vm_mmio2_table + static_cast< u64 >( mmio_id ) * sizeof( u64 ), mmio_range ) || !utility::is_likely_user_pointer( mmio_range ) )
                return std::nullopt;

            u64 host_base = 0;
            if ( !read_host_value( mmio_range + this->dyn_offsets.mmio_range_host_base, host_base ) )
                return std::nullopt;
            return host_base + ( static_cast< u64 >( mmio_page ) << 12 );
        }

        const u32 chunk_id = info >> 9;
        const u32 page_in_chunk = info & 0x1FF;
        if ( chunk_id != 0 )
        {
            auto chunk = find_chunk_from_cache( chunk_id );
            if ( !chunk.has_value( ) )
                chunk = find_chunk_from_tree( chunk_id );
            if ( !chunk.has_value( ) )
            {
                u64 remote_chunk = 0;
                if ( map_chunk_via_bstk_vmm( chunk_id, remote_chunk, false ) )
                    chunk = remote_chunk;
            }
            if ( !chunk.has_value( ) )
                return std::nullopt;

            u64 chunk_host_base = 0;
            if ( !read_host_value( *chunk + this->dyn_offsets.chunk_host_base, chunk_host_base ) )
                return std::nullopt;
            return chunk_host_base + ( static_cast< u64 >( page_in_chunk ) << 12 );
        }

        if ( info == 0 && this->dyn_offsets.vm_direct_ram_tag != 0 && this->dyn_offsets.vm_direct_ram_window != 0 )
        {
            u64 direct_tag = 0;
            if ( !read_host_value( vm_info_value.pvm + this->dyn_offsets.vm_direct_ram_tag, direct_tag ) )
                return std::nullopt;

            const bool direct_class = page_class == 4;
            const bool direct_state_match = ( state & 0x7000000000000ULL ) == 0 && ( state & 0x0FFFFFFFFFF000ULL ) == direct_tag;

            if ( direct_class || direct_state_match )
            {
                return vm_info_value.pvm + this->dyn_offsets.vm_direct_ram_window;
            }
        }

        return std::nullopt;
    }

    std::optional<u64> c_memory_engine::map_physical_to_host(u64 guest_phys)
    {
        if (const auto cached_host_page = lookup_cached_host_page(guest_phys); cached_host_page.has_value())
            return *cached_host_page | (guest_phys & utility::k_page_offset_mask);

        const auto page_map_result = try_page_map_cache(guest_phys);
        if (page_map_result.has_value()) {
            store_cached_host_page(guest_phys, *page_map_result & utility::k_page_mask);
            return page_map_result;
        }

        const auto page_base = map_physical_page_to_host_base(guest_phys);
        if (!page_base.has_value())
            return std::nullopt;

        store_cached_host_page(guest_phys, *page_base);
        return *page_base | (guest_phys & utility::k_page_offset_mask);
    }

    std::optional<c_translation_result> c_memory_engine::resolve_guest_physical(u64 guest_phys, bool verbose)
    {
        const auto host_pointer = map_physical_to_host(guest_phys);
        if (!host_pointer.has_value())
            return std::nullopt;

        return c_translation_result { guest_phys, *host_pointer };
    }

    bool c_memory_engine::read_guest_physical(u64 guest_phys, void* buffer, usize size, bool verbose)
    {
        if (buffer == nullptr || size == 0)
            return false;

        const auto translation = resolve_guest_physical(guest_phys, verbose);
        if (!translation.has_value())
            return false;

        return read_host_bytes(translation->host_pointer, buffer, size);
    }

    bool c_memory_engine::write_guest_physical(u64 guest_phys, const void* buffer, usize size, bool verbose)
    {
        if (buffer == nullptr || size == 0)
            return false;

        const auto translation = resolve_guest_physical(guest_phys, verbose);
        if (!translation.has_value())
            return false;

        return write_host_bytes(translation->host_pointer, buffer, size);
    }

    bool c_memory_engine::read_guest_kernel(u64 kernel_va, void* buffer, usize size, bool verbose)
    {
        auto* out = static_cast<u8*>(buffer);
        usize copied = 0;

        while (copied < size) {
            const auto current_va = kernel_va + copied;
            const auto chunk      = std::min<usize>(size - copied, utility::k_page_size - (current_va & utility::k_page_offset_mask));

            u64 phys = 0;
            if (!utility::try_convert_kernel_virtual_to_physical(current_va, phys))
                return false;

            if (!read_guest_physical(phys, out + copied, chunk, verbose))
                return false;

            copied += chunk;
        }

        return true;
    }

    bool c_memory_engine::write_guest_kernel(u64 kernel_va, const void* buffer, usize size, bool verbose)
    {
        const auto* in = static_cast<const u8*>(buffer);
        usize written  = 0;

        while (written < size) {
            const auto current_va = kernel_va + written;
            const auto chunk      = std::min<usize>(size - written, utility::k_page_size - (current_va & utility::k_page_offset_mask));

            u64 phys = 0;
            if (!utility::try_convert_kernel_virtual_to_physical(current_va, phys))
                return false;

            if (!write_guest_physical(phys, in + written, chunk, verbose))
                return false;

            written += chunk;
        }

        return true;
    }

    std::optional<std::string> c_memory_engine::read_guest_kernel_c_string(u64 kernel_va, usize max_chars, bool verbose)
    {
        if (kernel_va == 0 || max_chars == 0)
            return std::nullopt;

        std::string result;
        result.reserve(std::min<usize>(max_chars, 256));

        for (usize index = 0; index < max_chars; ++index) {
            char character { };
            if (!read_guest_kernel(kernel_va + index, character, verbose))
                return std::nullopt;

            if (character == '\0')
                break;

            result.push_back(character);
        }

        return utility::sanitize_guest_string(std::move(result));
    }

    std::optional<std::string> c_memory_engine::read_guest_kernel_string(u64 kernel_va, usize size, bool verbose)
    {
        if (kernel_va == 0 || size == 0)
            return std::nullopt;

        std::string result(size, '\0');
        if (!read_guest_kernel(kernel_va, result.data(), size, verbose))
            return std::nullopt;

        return utility::sanitize_guest_string(std::move(result));
    }

    std::optional<u64> c_memory_engine::read_mm_cr3(u64 mm_va, bool verbose)
    {
        u64 pgd_kernel_va = 0;
        if (!read_guest_kernel(mm_va + offsetof(kernel::c_guest_mm_struct, pgd), pgd_kernel_va, verbose) || pgd_kernel_va == 0)
            return std::nullopt;

        u64 pgd_phys = 0;
        if (!utility::try_convert_kernel_virtual_to_physical(pgd_kernel_va, pgd_phys))
            return std::nullopt;

        return pgd_phys;
    }

    std::optional<u64> c_memory_engine::translate_guest_va(u64 guest_cr3, u64 guest_va, bool verbose)
    {
        const u64 canonical_check = guest_va >> 47;
        if (canonical_check != 0 && canonical_check != 0x1FFFFULL) {
            utility::verbose_log(verbose, XorStrS("[mem] translate: va ") + utility::hex(guest_va) + XorStrS(" failed canonical check"));
            return std::nullopt;
        }

        if (const auto cached_phys_page = lookup_cached_physical_page(guest_cr3, guest_va); cached_phys_page.has_value())
            return *cached_phys_page | (guest_va & utility::k_page_offset_mask);

        const auto read_pte = [&](u64 base, u64 index, u64& entry) -> bool {
            bool ok = read_guest_physical(base + index * sizeof(u64), entry, verbose);
            if (!ok) {
                utility::verbose_log(verbose, XorStrS("[mem] translate: read_guest_physical failed at base=") + utility::hex(base) + XorStrS(" index=") + utility::hex(index));
            } else if ((entry & utility::k_present_bit) == 0) {
                utility::verbose_log(verbose, XorStrS("[mem] translate: entry not present at base=") + utility::hex(base) + XorStrS(" index=") + utility::hex(index) + XorStrS(" entry=") + utility::hex(entry));
            }
            return ok && (entry & utility::k_present_bit) != 0;
        };

        const u64 pml4_base = guest_cr3 & utility::k_long_mode_page_frame_mask;
        u64 pml4e { };
        if (!read_pte(pml4_base, (guest_va >> 39) & 0x1FFULL, pml4e)) {
            utility::verbose_log(verbose, XorStrS("[mem] translate: PML4 read failed for va ") + utility::hex(guest_va) + XorStrS(" pml4_base=") + utility::hex(pml4_base));
            return std::nullopt;
        }

        const u64 pdpt_base = pml4e & utility::k_long_mode_page_frame_mask;
        u64 pdpte { };
        if (!read_pte(pdpt_base, (guest_va >> 30) & 0x1FFULL, pdpte)) {
            utility::verbose_log(verbose, XorStrS("[mem] translate: PDPT read failed for va ") + utility::hex(guest_va) + XorStrS(" pdpt_base=") + utility::hex(pdpt_base));
            return std::nullopt;
        }

        if ((pdpte & utility::k_page_size_bit) != 0) {
            const u64 resolved = (pdpte & utility::k_large_page_1gb_mask) | (guest_va & 0x3FFFFFFFULL);
            store_cached_physical_page(guest_cr3, guest_va, resolved & utility::k_page_mask);
            return resolved;
        }

        const u64 pd_base = pdpte & utility::k_long_mode_page_frame_mask;
        u64 pde { };
        if (!read_pte(pd_base, (guest_va >> 21) & 0x1FFULL, pde)) {
            utility::verbose_log(verbose, XorStrS("[mem] translate: PD read failed for va ") + utility::hex(guest_va) + XorStrS(" pd_base=") + utility::hex(pd_base));
            return std::nullopt;
        }

        if ((pde & utility::k_page_size_bit) != 0) {
            const u64 resolved = (pde & utility::k_large_page_2mb_mask) | (guest_va & 0x1FFFFFULL);
            store_cached_physical_page(guest_cr3, guest_va, resolved & utility::k_page_mask);
            return resolved;
        }

        const u64 pt_base = pde & utility::k_long_mode_page_frame_mask;
        u64 pte { };
        if (!read_pte(pt_base, (guest_va >> 12) & 0x1FFULL, pte)) {
            utility::verbose_log(verbose, XorStrS("[mem] translate: PT read failed for va ") + utility::hex(guest_va) + XorStrS(" pt_base=") + utility::hex(pt_base));
            return std::nullopt;
        }

        const u64 resolved = (pte & utility::k_long_mode_page_frame_mask) | (guest_va & utility::k_page_offset_mask);
        store_cached_physical_page(guest_cr3, guest_va, resolved & utility::k_page_mask);
        return resolved;
    }

    bool c_memory_engine::read_guest_user(u64 guest_cr3, u64 guest_va, void* buffer, usize size, bool verbose)
    {
        auto* out    = static_cast<u8*>(buffer);
        usize copied = 0;

        while (copied < size) {
            const auto current_va = guest_va + copied;
            const auto chunk      = std::min<usize>(size - copied, utility::k_page_size - (current_va & utility::k_page_offset_mask));
            const auto page_offset = current_va & utility::k_page_offset_mask;

            if (const auto host_page = lookup_cached_host_page_by_gva(guest_cr3, current_va); host_page.has_value()) {
                if (read_host_bytes(*host_page + page_offset, out + copied, chunk)) {
                    copied += chunk;
                    continue;
                }
                invalidate_cached_host_page_by_gva(guest_cr3, current_va);
            }

            bool read_ok = false;
            for (int attempt = 0; attempt < 2 && !read_ok; ++attempt) {
                const auto phys = translate_guest_va(guest_cr3, current_va, verbose);
                if (!phys.has_value())
                    break;

                const auto host_pointer = map_physical_to_host(*phys);
                if (!host_pointer.has_value())
                    break;

                store_cached_host_page_by_gva(guest_cr3, current_va, *host_pointer & utility::k_page_mask);

                if (read_host_bytes(*host_pointer, out + copied, chunk)) {
                    read_ok = true;
                    copied += chunk;
                } else {
                    invalidate_cached_physical_page(guest_cr3, current_va);
                    invalidate_cached_host_page(*phys);
                    invalidate_cached_host_page_by_gva(guest_cr3, current_va);
                }
            }

            if (!read_ok)
                return false;
        }

        return true;
    }

    bool c_memory_engine::write_guest_user(u64 guest_cr3, u64 guest_va, const void* buffer, usize size, bool verbose)
    {
        const auto* in = static_cast<const u8*>(buffer);
        usize written  = 0;

        while (written < size) {
            const auto current_va = guest_va + written;
            const auto chunk      = std::min<usize>(size - written, utility::k_page_size - (current_va & utility::k_page_offset_mask));
            const auto page_offset = current_va & utility::k_page_offset_mask;

            if (const auto host_page = lookup_cached_host_page_by_gva(guest_cr3, current_va); host_page.has_value()) {
                if (write_host_bytes(*host_page + page_offset, in + written, chunk)) {
                    written += chunk;
                    continue;
                }
                invalidate_cached_host_page_by_gva(guest_cr3, current_va);
            }

            bool write_ok = false;
            for (int attempt = 0; attempt < 2 && !write_ok; ++attempt) {
                const auto phys = translate_guest_va(guest_cr3, current_va, verbose);
                if (!phys.has_value())
                    break;

                const auto host_pointer = map_physical_to_host(*phys);
                if (!host_pointer.has_value())
                    break;

                store_cached_host_page_by_gva(guest_cr3, current_va, *host_pointer & utility::k_page_mask);

                if (write_host_bytes(*host_pointer, in + written, chunk)) {
                    write_ok = true;
                    written += chunk;
                } else {
                    invalidate_cached_physical_page(guest_cr3, current_va);
                    invalidate_cached_host_page(*phys);
                    invalidate_cached_host_page_by_gva(guest_cr3, current_va);
                }
            }

            if (!write_ok)
                return false;
        }

        return true;
    }

    std::optional<std::string> c_memory_engine::read_guest_user_c_string(u64 guest_cr3, u64 guest_va, usize max_chars, bool verbose)
    {
        if (guest_va == 0 || max_chars == 0)
            return std::nullopt;

        std::string result;
        result.reserve(std::min<usize>(max_chars, 256));

        for (usize index = 0; index < max_chars; ++index) {
            char character { };
            if (!read_guest_user(guest_cr3, guest_va + index, character, verbose))
                return std::nullopt;

            if (character == '\0')
                break;

            result.push_back(character);
        }

        return utility::sanitize_guest_string(std::move(result));
    }

    bool c_memory_engine::read_gva(u64 guest_cr3, u64 guest_va, void* buffer, usize size, bool verbose)
    {
        return read_guest_user(guest_cr3, guest_va, buffer, size, verbose);
    }

    bool c_memory_engine::write_gva(u64 guest_cr3, u64 guest_va, const void* buffer, usize size, bool verbose)
    {
        return write_guest_user(guest_cr3, guest_va, buffer, size, verbose);
    }

    std::optional<u64> c_memory_engine::lookup_cached_physical_page(u64 guest_cr3, u64 guest_va) const
    {
        const c_gva_page_key key { guest_cr3 & utility::k_long_mode_page_frame_mask, guest_va & utility::k_page_mask };
        std::shared_lock<std::shared_mutex> lock(cache_mutex);

        const auto iterator = gva_to_physical_page_cache.find(key);
        if (iterator == gva_to_physical_page_cache.end())
            return std::nullopt;

        return iterator->second;
    }

    void c_memory_engine::store_cached_physical_page(u64 guest_cr3, u64 guest_va, u64 guest_phys_page)
    {
        const c_gva_page_key key { guest_cr3 & utility::k_long_mode_page_frame_mask, guest_va & utility::k_page_mask };
        std::unique_lock<std::shared_mutex> lock(cache_mutex);

        if (gva_to_physical_page_cache.size() >= k_max_gva_cache_entries) {
            gva_to_physical_page_cache.clear();
        }

        gva_to_physical_page_cache[key] = guest_phys_page & utility::k_page_mask;
    }

    void c_memory_engine::invalidate_cached_physical_page(u64 guest_cr3, u64 guest_va)
    {
        const c_gva_page_key key { guest_cr3 & utility::k_long_mode_page_frame_mask, guest_va & utility::k_page_mask };
        std::unique_lock<std::shared_mutex> lock(cache_mutex);

        gva_to_physical_page_cache.erase(key);
        gva_to_host_page_cache.erase(key);
    }

    std::optional<u64> c_memory_engine::lookup_cached_host_page_by_gva(u64 guest_cr3, u64 guest_va) const
    {
        const c_gva_page_key key { guest_cr3 & utility::k_long_mode_page_frame_mask, guest_va & utility::k_page_mask };
        std::shared_lock<std::shared_mutex> lock(cache_mutex);

        const auto iterator = gva_to_host_page_cache.find(key);
        if (iterator == gva_to_host_page_cache.end())
            return std::nullopt;

        return iterator->second;
    }

    void c_memory_engine::store_cached_host_page_by_gva(u64 guest_cr3, u64 guest_va, u64 host_page_base)
    {
        const c_gva_page_key key { guest_cr3 & utility::k_long_mode_page_frame_mask, guest_va & utility::k_page_mask };
        std::unique_lock<std::shared_mutex> lock(cache_mutex);

        if (gva_to_host_page_cache.size() >= k_max_gva_host_cache_entries) {
            gva_to_host_page_cache.clear();
        }

        gva_to_host_page_cache[key] = host_page_base & utility::k_page_mask;
    }

    void c_memory_engine::invalidate_cached_host_page_by_gva(u64 guest_cr3, u64 guest_va)
    {
        const c_gva_page_key key { guest_cr3 & utility::k_long_mode_page_frame_mask, guest_va & utility::k_page_mask };
        std::unique_lock<std::shared_mutex> lock(cache_mutex);

        gva_to_host_page_cache.erase(key);
    }

    std::optional<u64> c_memory_engine::lookup_cached_host_page(u64 guest_phys) const
    {
        const auto guest_phys_page = guest_phys & utility::k_page_mask;
        std::shared_lock<std::shared_mutex> lock(cache_mutex);

        const auto iterator = phys_to_host_page_cache.find(guest_phys_page);
        if (iterator == phys_to_host_page_cache.end())
            return std::nullopt;

        return iterator->second;
    }

    void c_memory_engine::store_cached_host_page(u64 guest_phys, u64 host_page_base)
    {
        const auto guest_phys_page = guest_phys & utility::k_page_mask;
        std::unique_lock<std::shared_mutex> lock(cache_mutex);

        if (phys_to_host_page_cache.size() >= k_max_phys_cache_entries) {
            phys_to_host_page_cache.clear();
        }

        phys_to_host_page_cache[guest_phys_page] = host_page_base & utility::k_page_mask;
    }

    void c_memory_engine::invalidate_cached_host_page(u64 guest_phys)
    {
        const auto guest_phys_page = guest_phys & utility::k_page_mask;
        std::unique_lock<std::shared_mutex> lock(cache_mutex);

        phys_to_host_page_cache.erase(guest_phys_page);
    }
}

#endif

