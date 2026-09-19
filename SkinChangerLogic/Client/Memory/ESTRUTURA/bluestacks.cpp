#ifndef BLUESTACKS_CPP
#define BLUESTACKS_CPP

#include <IMPLEMENTACAO/includes.hpp>
#include <unordered_set>
#include "../../XorStr.hpp"



namespace offsets {
    constexpr u64 vma_vm_start = 0x0;
    constexpr u64 vma_vm_next  = 0x10;
    constexpr u64 vma_vm_file  = 0xA0;
}

namespace {

    void log_verbose(bool verbose, const std::string& message)
    {
        if (verbose) {
            std::cout << message << '\n';
        }
    }
}

namespace bstk {

    bool c_bluestacks::setup(const std::string& package_name, bool verbose)
    {
        package_name_value = package_name;
        task_value         = { };
        guest_cr3_value    = 0;
        cached_module_candidates.clear();
        cached_module_arch.clear();

        constexpr s32 k_setup_attempts = 20;
        constexpr u32 k_setup_delay_ms = 500;

        memory->destroy();

        for (s32 attempt = 0; attempt < k_setup_attempts; ++attempt) {
            log_verbose(verbose, XorStrS("[bstk] setup attempt ") + std::to_string(attempt + 1));
            memory->clear_cr3();

            if (memory->setup(verbose)) {
                log_verbose(verbose, XorStrS("[bstk] memory setup ok"));
                if (find_task(package_name, task_value, verbose)) {
                    if (!task_value.cmdline.empty()) {
                        package_name_value = task_value.cmdline;
                    } else {
                        package_name_value = task_value.comm;
                    }
                    log_verbose(verbose, XorStrS("[bstk] task ok pid=") + std::to_string(task_value.pid) + XorStrS(" mm=") + utility::hex(task_value.mm) + XorStrS(" comm=") + task_value.comm + XorStrS(" cmdline=") + task_value.cmdline);
                    guest_cr3_value = memory->get_cr3(task_value.mm, verbose);
                    if (guest_cr3_value != 0) {
                        log_verbose(verbose, XorStrS("[bstk] cr3=") + utility::hex(guest_cr3_value));
                    std::cout << std::flush;
                        memory->set_cr3(guest_cr3_value);
                        return true;
                    }

                    log_verbose(verbose, XorStrS("[bstk] cr3 read failed"));
                } else {
                    log_verbose(verbose, XorStrS("[bstk] task not found"));
                }
            } else {
                log_verbose(verbose, XorStrS("[bstk] memory setup failed"));
            }

            memory->destroy();

            if (attempt + 1 < k_setup_attempts) {
                Sleep(k_setup_delay_ms);
            }
        }

        package_name_value.clear();
        task_value      = { };
        guest_cr3_value = 0;
        return false;
    }

    void c_bluestacks::destroy()
    {
        package_name_value.clear();
        task_value      = { };
        guest_cr3_value = 0;
        cached_module_candidates.clear();
        cached_module_arch.clear();
        memory->destroy();
    }

    u64 c_bluestacks::get_cr3() const
    {
        return guest_cr3_value;
    }

    u16 c_bluestacks::get_module_machine_type(const std::string& module_name) const
    {
        auto it = cached_module_arch.find(module_name);
        if (it != cached_module_arch.end())
            return it->second;
        return 0;
    }

    u64 c_bluestacks::get_module_base(const std::string& module_name, usize map_index, bool verbose, usize candidate_index)
    {
        if (task_value.mm == 0 || guest_cr3_value == 0 || map_index == 0 || candidate_index == 0)
            return 0;

        if (verbose) std::cout << XorStr("[bstk] get_module_base: ") << module_name << XorStr(" map=") << map_index << XorStr(" cand=") << candidate_index << std::flush << "\n";

        auto it_cache = cached_module_candidates.find(module_name);
        if (it_cache != cached_module_candidates.end()) {
            if (candidate_index <= it_cache->second.size()) {
                const auto& bases = it_cache->second[candidate_index - 1];
                if (map_index <= bases.size())
                    return bases[map_index - 1];
            }
            return 0;
        }

        constexpr usize k_max_vmas = 8192;

        u64 current_vma { };
        if (!memory->read_kernel(task_value.mm + offsetof(kernel::c_guest_mm_struct, mmap), current_vma, verbose))
            return 0;

        struct c_module_candidate {
            u64 vm_file { };
            std::string path;
            std::vector<u64> bases;
            u64 total_size { };      // sum of (vm_end - vm_start) for all VMAs
            u64 max_seg_size { };    // size of the single largest VMA
            u16 machine_type { };
        };

        std::unordered_set<u64> seen_vmas;
        seen_vmas.reserve(256);
        std::vector<c_module_candidate> candidates;

        // Cache sets to avoid scanning same files repeatedly
        std::unordered_set<u64> matching_files;
        std::unordered_set<u64> non_matching_files;

        while (current_vma != 0 && seen_vmas.size() < k_max_vmas) {
            if (seen_vmas.count(current_vma) != 0) {
                break;
            }

            seen_vmas.insert(current_vma);

            u8 vma_data[0xA8] { };
            if (!memory->read_kernel(current_vma, vma_data, sizeof(vma_data), verbose)) {
                break;
            }

            u64 vm_start = *reinterpret_cast<u64*>(&vma_data[0x0]);
            u64 vm_end   = *reinterpret_cast<u64*>(&vma_data[0x8]);
            u64 vm_next  = *reinterpret_cast<u64*>(&vma_data[0x10]);
            u64 vm_file  = *reinterpret_cast<u64*>(&vma_data[0xA0]);
            u64 seg_size = (vm_end > vm_start) ? (vm_end - vm_start) : 0;

            if (vm_file != 0) {
                bool is_match = false;
                if (matching_files.count(vm_file)) {
                    is_match = true;
                } else if (!non_matching_files.count(vm_file)) {
                    const auto file_name = read_file_name(vm_file, verbose);
                    if (utility::matches_library_name(file_name, module_name)) {
                        matching_files.insert(vm_file);
                        is_match = true;
                    } else {
                        non_matching_files.insert(vm_file);
                    }
                }

                if (is_match) {
                    auto it = std::find_if(candidates.begin(), candidates.end(), [vm_file](const c_module_candidate& c) {
                        return c.vm_file == vm_file;
                    });
                    if (it == candidates.end()) {
                        c_module_candidate c;
                        c.vm_file = vm_file;
                        c.path = read_file_path(vm_file, verbose);
                        c.bases.push_back(vm_start);
                        c.total_size = seg_size;
                        c.max_seg_size = seg_size;
                        candidates.push_back(c);
                    } else {
                        if (std::find(it->bases.begin(), it->bases.end(), vm_start) == it->bases.end()) {
                            it->bases.push_back(vm_start);
                            it->total_size += seg_size;
                            if (seg_size > it->max_seg_size) it->max_seg_size = seg_size;
                        }
                    }
                    if (verbose) {
                        std::cout << XorStr("[bstk] vma match: base=0x") << std::hex << vm_start
                            << XorStr(" end=0x") << vm_end << XorStr(" size=0x") << seg_size
                            << XorStr(" file=0x") << vm_file << std::dec << "\n";
                    }
                }
            }

            if (vm_next == 0 || vm_next == current_vma) {
                break;
            }

            current_vma = vm_next;
        }

        if (verbose) {
            std::cout << XorStr("[bstk] scan done: ") << candidates.size() << XorStr(" candidate(s) for ") << module_name << std::flush << "\n";
        }

        if (candidates.empty()) {
            cached_module_candidates[module_name] = {};
            return 0;
        }

        // Sort candidates: for x86 guests the real loaded ELF has multiple PT_LOAD
        // segments (one per section group) while linker-namespace stubs appear as a
        // single large anonymous VMA — prefer more segments for x86 paths.
        // For ARM / other: largest individual VMA segment wins (original behaviour).
        std::sort(candidates.begin(), candidates.end(), [](const c_module_candidate& a, const c_module_candidate& b) {
            auto is_x86_path = [](const std::string& p) {
                return p.find(XorStrS("/lib/x86/")) != std::string::npos ||
                       p.find(XorStrS("/lib/x86_64/")) != std::string::npos;
            };
            bool a_x86 = is_x86_path(a.path);
            bool b_x86 = is_x86_path(b.path);
            
            // Consistently sort x86 candidates before non-x86 ones to prevent cycles
            if (a_x86 != b_x86) {
                return a_x86;
            }
            
            if (a_x86) {
                // More segments = properly loaded ELF; fewer = namespace stub
                if (a.bases.size() != b.bases.size()) return a.bases.size() > b.bases.size();
                if (a.total_size != b.total_size) return a.total_size > b.total_size;
                return a.max_seg_size > b.max_seg_size;
            }
            
            // non-x86: sort by max segment size first
            if (a.max_seg_size != b.max_seg_size) return a.max_seg_size > b.max_seg_size;
            if (a.total_size != b.total_size) return a.total_size > b.total_size;
            return a.bases.size() > b.bases.size();
        });

        for (auto& c : candidates) {
            std::sort(c.bases.begin(), c.bases.end());
            if (!c.bases.empty()) {
                u32 magic = 0;
                bool read_ok = memory->read<u32>(c.bases[0], magic, verbose);
                if (verbose) {
                    std::cout << XorStr("[bstk] magic check: path=") << c.path
                        << XorStr(" base=0x") << std::hex << c.bases[0]
                        << XorStr(" read_ok=") << read_ok
                        << XorStr(" magic=0x") << magic << std::dec << "\n";
                }
                if (read_ok && magic == 0x464C457F) {
                    memory->read<u16>(c.bases[0] + 0x12, c.machine_type, verbose);
                }
            }
        }

        if (verbose) {
            std::cout << XorStr("[bstk] ") << candidates.size() << XorStr(" candidates for ") << module_name << XorStr(":\n");
            for (const auto& c : candidates) {
                std::cout << XorStr("[bstk]   base=0x") << std::hex << (c.bases.empty() ? 0 : c.bases[0])
                    << XorStr(" total_size=0x") << c.total_size
                    << XorStr(" max_seg=0x") << c.max_seg_size
                    << std::dec << XorStr(" segs=") << c.bases.size()
                    << XorStr(" arch=") << c.machine_type
                    << XorStr(" path=") << c.path << "\n";
            }
        }

        // Derive machine_type from path for candidates where ELF header was paged out.
        // This is the common case on x86 Android guests: the ELF header page is reclaimed
        // by the kernel when unused, so the magic check yields read_ok=0 and machine_type=0.
        // The ABI directory (/lib/x86/, /lib/arm64-v8a/, etc.) is always present in the path.
        for (auto& c : candidates) {
            if (c.machine_type == 0 && !c.path.empty()) {
                if (c.path.find(XorStrS("/lib/x86_64/")) != std::string::npos)      c.machine_type = 62;
                else if (c.path.find(XorStrS("/lib/x86/")) != std::string::npos)    c.machine_type = 3;
                else if (c.path.find(XorStrS("/lib/arm64")) != std::string::npos)   c.machine_type = 183;
                else if (c.path.find(XorStrS("/lib/armeabi")) != std::string::npos) c.machine_type = 40;
                else if (c.path.find(XorStrS("/lib/arm/")) != std::string::npos)    c.machine_type = 40;
            }
        }

        if (verbose) {
            std::cout << XorStr("[bstk] ") << candidates.size() << XorStr(" libil2cpp candidate(s) in guest VMA list\n");
        }

        if (candidates.empty()) {
            if (verbose)
                std::cout << XorStr("[bstk] no libil2cpp.so mapping found (game still loading?)\n");
            return 0;
        }

        const c_module_candidate* selected = nullptr;

        // Helper: check if at least one segment of a candidate is readable.
        // Tests the second page (offset 0x1000) because the first page (ELF header)
        // is frequently paged out on x86 Android guests.
        auto is_candidate_readable = [&](const c_module_candidate& c) -> bool {
            // Try offset 0x1000 first (second page, usually .text start)
            for (const auto& base : c.bases) {
                u32 test_val = 0;
                if (memory->read<u32>(base + 0x1000, test_val, false))
                    return true;
            }
            // Also try offset 0x0 in case the second page is the one paged out
            for (const auto& base : c.bases) {
                u32 test_val = 0;
                if (memory->read<u32>(base, test_val, false))
                    return true;
            }
            if (c.bases.size() > 1) {
                u32 test_val = 0;
                u64 last_base = c.bases.back();
                if (memory->read<u32>(last_base + 0x1000, test_val, false))
                    return true;
                if (memory->read<u32>(last_base + 0x4000, test_val, false))
                    return true;
            }
            return false;
        };

        auto try_v7a_chain = [&](u64 base, u64 init_off) -> bool {
            u32 step1 = 0;
            if (!memory->read<u32>(base + init_off, step1, false) || step1 < 0x10000) return false;
            u32 step2 = 0;
            if (!memory->read<u32>(step1 + 0x5C, step2, false) || step2 < 0x10000) return false;
            u32 ge = 0;
            if (!memory->read<u32>(step2, ge, false) || ge < 0x10000) return false;

            u32 matchGame = 0;
            if (memory->read<u32>(ge + 0x10, matchGame, false) && matchGame >= 0x10000) {
                u32 match = 0;
                if (memory->read<u32>(matchGame + 0x50, match, false) && match >= 0x10000)
                    return true;
            }
            u32 match = 0;
            return memory->read<u32>(ge + 0x50, match, false) && match >= 0x10000;
        };

        auto v7a_chain_ok = [&](const c_module_candidate& c) -> bool {
            if (c.machine_type != 40 && c.machine_type != 183) return false;
            u64 base = c.bases.empty() ? 0 : c.bases[0];
            if (!base) return false;
            return try_v7a_chain(base, 0xA986E9C) || try_v7a_chain(base, 0xABFF6E0);
        };

        auto is_lib_valid = [&](const c_module_candidate& c) -> bool {
            if (!is_candidate_readable(c)) return false;

            u64 base = c.bases.empty() ? 0 : c.bases[0];
            if (base == 0) return false;

            // x86: reject namespace stubs that don't resolve the GameEngine chain.
            if (c.machine_type == 3 || c.machine_type == 62) {
                std::vector<u64> init_bases = {0xA9EA0F4, 0xAEFCEC8};
                for (u64 init : init_bases) {
                    u32 step1 = 0;
                    if (!memory->read<u32>(base + init, step1, false) || !step1 || step1 < 0x10000) continue;
                    u32 step_mid = 0;
                    if (!memory->read<u32>(step1, step_mid, false) || !step_mid || step_mid < 0x10000) continue;
                    u32 step2 = 0;
                    if (!memory->read<u32>(step_mid + 0x5C, step2, false) || !step2 || step2 < 0x10000) continue;
                    u32 ge = 0;
                    if (memory->read<u32>(step2, ge, false) && ge >= 0x10000)
                        return true;
                }
                return false;
            }

            // ARM: lib must be readable; chain is checked separately for ranking.
            // InitBase can fail in lobby / before il2cpp statics init — don't reject the mapping.
            return true;
        };

        auto log_skip = [&](const c_module_candidate& c, const char* reason) {
            if (!verbose) return;
            std::cout << XorStr("[bstk] skip base=0x") << std::hex << (c.bases.empty() ? 0 : c.bases[0])
                << std::dec << XorStr(" arch=") << c.machine_type << XorStr(" path=") << c.path
                << XorStr(" (") << reason << XorStr(")\n");
        };

        // Selection: prefer ARM on v7a guests (before x86 stubs), then x86 with valid chain.
        auto select_best_readable = [&]() -> const c_module_candidate* {

            // 1. ARM / ARM64 with package in path + readable (+ prefer chain ok)
            for (const auto& c : candidates) {
                if ((c.machine_type == 40 || c.machine_type == 183) &&
                    !package_name_value.empty() &&
                    utility::contains_case_insensitive(c.path, package_name_value) &&
                    is_lib_valid(c) && v7a_chain_ok(c))
                    return &c;
            }
            for (const auto& c : candidates) {
                if ((c.machine_type == 40 || c.machine_type == 183) &&
                    !package_name_value.empty() &&
                    utility::contains_case_insensitive(c.path, package_name_value) &&
                    is_lib_valid(c))
                    return &c;
            }

            // 2. Any ARM / ARM64 readable (chain optional — lobby / offset may not resolve yet)
            for (const auto& c : candidates) {
                if ((c.machine_type == 40 || c.machine_type == 183) && is_lib_valid(c)) {
                    if (verbose && !v7a_chain_ok(c)) {
                        std::cout << XorStr("[bstk] using ARM lib base=0x") << std::hex << c.bases[0]
                            << std::dec << XorStr(" (readable; InitBase chain not ready yet)\n");
                    }
                    return &c;
                }
            }

            // 3. x86 / x86_64 with package name in path
            for (const auto& c : candidates) {
                if ((c.machine_type == 3 || c.machine_type == 62) &&
                    !package_name_value.empty() &&
                    utility::contains_case_insensitive(c.path, package_name_value)) {
                    if (is_lib_valid(c))
                        return &c;
                    log_skip(c, XorStr("unreadable or bad x86 chain"));
                }
            }

            // 4. x86 / x86_64 as fallback
            for (const auto& c : candidates) {
                if (c.machine_type == 3 || c.machine_type == 62) {
                    if (is_lib_valid(c))
                        return &c;
                    log_skip(c, XorStr("unreadable or bad x86 chain"));
                }
            }

            // 5. Any readable candidate
            for (const auto& c : candidates) {
                if (is_candidate_readable(c))
                    return &c;
            }

            // 6. Last resort: first candidate
            return &candidates[0];
        };

        selected = select_best_readable();

        if (verbose && selected) {
            std::cout << XorStr("[bstk] selected: base=0x") << std::hex
                << (selected->bases.empty() ? 0 : selected->bases[0])
                << XorStr(" total_size=0x") << selected->total_size
                << XorStr(" segs=") << std::dec << selected->bases.size()
                << XorStr(" arch=") << selected->machine_type
                << XorStr(" path=") << selected->path << std::flush << "\n";
        }

        if (!selected)
            return 0;

        // Determine and cache the machine type for this module.
        // machine_type may be 0 if the ELF header page was paged out; fall back to path.
        u16 arch = selected->machine_type;
        if (arch == 0) {
            for (const auto& base : selected->bases) {
                u32 magic = 0;
                if (memory->read<u32>(base, magic) && magic == 0x464C457F) {
                    memory->read<u16>(base + 0x12, arch);
                    if (arch != 0) break;
                }
            }
        }
        if (arch == 0) {
            const std::string& p = selected->path;
            if (p.find(XorStrS("/lib/x86_64/")) != std::string::npos)      arch = 62;
            else if (p.find(XorStrS("/lib/x86/")) != std::string::npos)    arch = 3;
            else if (p.find(XorStrS("/lib/arm64-v8a/")) != std::string::npos) arch = 183;
            else if (p.find(XorStrS("/lib/armeabi-v7a/")) != std::string::npos) arch = 40;
            else if (p.find(XorStrS("/lib/armeabi/")) != std::string::npos) arch = 40;
            else if (p.find(XorStrS("/lib/arm/")) != std::string::npos) arch = 40;
        }
        cached_module_arch[module_name] = arch;

        // Store all candidates: selected candidate first (index 0), then the rest in sorted order.
        // This lets callers request candidate_index=2 to skip the first one.
        std::vector<std::vector<u64>> all_cand_bases;
        all_cand_bases.push_back(selected->bases);
        for (const auto& c : candidates) {
            if (&c != selected)
                all_cand_bases.push_back(c.bases);
        }
        cached_module_candidates[module_name] = std::move(all_cand_bases);

        auto& stored = cached_module_candidates[module_name];
        if (candidate_index > stored.size())
            return 0;

        const auto& chosen_bases = stored[candidate_index - 1];
        if (map_index > chosen_bases.size())
            return 0;

        return chosen_bases[map_index - 1];
    }

    bool c_bluestacks::read_task(u64 task, c_task& out_task, bool verbose)
    {
        out_task.task = task;

        if (!memory->read_kernel(task + offsetof(kernel::c_guest_task_struct, mm), out_task.mm, verbose) ||
            !memory->read_kernel(task + offsetof(kernel::c_guest_task_struct, pid), out_task.pid, verbose) ||
            !memory->read_kernel(task + offsetof(kernel::c_guest_task_struct, tgid), out_task.tgid, verbose)) {
            return false;
        }

        out_task.comm = read_task_comm(task, verbose);
        if (out_task.comm.empty())
            return false;

        if (out_task.mm != 0 && utility::is_likely_guest_kernel_pointer(out_task.mm)) {
            const auto task_cr3 = memory->get_cr3(out_task.mm, verbose);
            if (task_cr3 != 0) {
                out_task.cmdline = read_task_cmdline(out_task.mm, task_cr3, verbose);
            }
        }

        return true;
    }

    std::string c_bluestacks::read_task_comm(u64 task, bool verbose)
    {
        char comm[16] { };
        if (!memory->read_kernel(task + offsetof(kernel::c_guest_task_struct, comm), comm, sizeof(comm), verbose))
            return { };

        return utility::sanitize_guest_string(std::string(comm, utility::bounded_str_len(comm, sizeof(comm))));
    }

    std::string c_bluestacks::read_task_cmdline(u64 mm, u64 guest_cr3, bool verbose)
    {
        u64 arg_start { };
        u64 arg_end   { };
        if (!memory->read_kernel(mm + offsetof(kernel::c_guest_mm_struct, arg_start), arg_start, verbose) ||
            !memory->read_kernel(mm + offsetof(kernel::c_guest_mm_struct, arg_end), arg_end, verbose) ||
            arg_start == 0 ||
            arg_end == 0 ||
            arg_end <= arg_start) {
            return { };
        }

        auto span = arg_end - arg_start;
        if (span > 0x2000ULL) {
            span = 0x2000ULL;
        }

        if (span == 0)
            return { };

        std::vector<char> data(static_cast<usize>(span), '\0');
        if (!memory->read_user(guest_cr3, arg_start, data.data(), data.size(), verbose))
            return { };

        return utility::sanitize_guest_string(std::string(data.data(), utility::bounded_str_len(data.data(), data.size())));
    }

    std::string c_bluestacks::read_kernel_string(u64 address, usize max_length, bool verbose)
    {
        if (address == 0 || max_length == 0)
            return { };

        std::vector<char> data(max_length + 1, '\0');
        if (!memory->read_kernel(address, data.data(), max_length, verbose))
            return { };

        return utility::sanitize_guest_string(std::string(data.data(), utility::bounded_str_len(data.data(), max_length)));
    }

    std::string c_bluestacks::read_user_string(u64 guest_cr3, u64 address, usize max_length, bool verbose)
    {
        if (address == 0 || max_length == 0)
            return { };

        std::vector<char> data(max_length + 1, '\0');
        if (!memory->read_user(guest_cr3, address, data.data(), max_length, verbose))
            return { };

        return utility::sanitize_guest_string(std::string(data.data(), utility::bounded_str_len(data.data(), max_length)));
    }

    std::string c_bluestacks::read_dentry_name(u64 dentry, bool verbose)
    {
        kernel::c_guest_dentry guest_dentry { };
        if (!memory->read_kernel(dentry, guest_dentry, verbose))
            return { };

        if (guest_dentry.d_name.len < sizeof(guest_dentry.d_iname)) {
            return utility::sanitize_guest_string(std::string(guest_dentry.d_iname, guest_dentry.d_name.len));
        }

        if (guest_dentry.d_name.len > 0 && guest_dentry.d_name.len <= 256 && guest_dentry.d_name.name != 0) {
            const auto exact_name = read_kernel_string(guest_dentry.d_name.name, guest_dentry.d_name.len, verbose);
            if (!exact_name.empty())
                return exact_name;
        }

        return utility::sanitize_guest_string(std::string(guest_dentry.d_iname, utility::bounded_str_len(guest_dentry.d_iname, sizeof(guest_dentry.d_iname))));
    }

    std::string c_bluestacks::read_file_name(u64 file, bool verbose)
    {
        kernel::c_guest_file guest_file { };
        if (!memory->read_kernel(file, guest_file, verbose))
            return { };

        if (guest_file.f_path.dentry == 0)
            return { };

        return read_dentry_name(guest_file.f_path.dentry, verbose);
    }

    std::string c_bluestacks::read_dentry_path(u64 dentry, bool verbose)
    {
        std::string path;
        u64 current = dentry;
        for (int i = 0; i < 32; ++i) {
            if (current == 0) break;

            kernel::c_guest_dentry gd { };
            if (!memory->read_kernel(current, gd, verbose)) break;

            std::string name;
            if (gd.d_name.len > 0 && gd.d_name.len <= 256 && gd.d_name.name != 0) {
                name = read_kernel_string(gd.d_name.name, gd.d_name.len, verbose);
            }
            if (name.empty()) {
                name = utility::sanitize_guest_string(std::string(gd.d_iname, utility::bounded_str_len(gd.d_iname, sizeof(gd.d_iname))));
            }

            if (name.empty()) break;

            if (name != XorStrS("/")) {
                path = XorStrS("/") + name + path;
            }

            if (gd.d_parent == 0 || gd.d_parent == current) break;
            current = gd.d_parent;
        }
        return path.empty() ? XorStrS("/") : path;
    }

    std::string c_bluestacks::read_file_path(u64 file, bool verbose)
    {
        kernel::c_guest_file guest_file { };
        if (!memory->read_kernel(file, guest_file, verbose))
            return { };

        if (guest_file.f_path.dentry == 0)
            return { };

        return read_dentry_path(guest_file.f_path.dentry, verbose);
    }

    s32 c_bluestacks::score_task(const c_task& task, const std::string& package_name) const
    {
        if (package_name.empty())
            return 1;

        auto check_score = [&](const std::string& target_pkg) -> s32 {
            s32 score { };

            if (!task.cmdline.empty()) {
                if (utility::equals_case_insensitive(task.cmdline, target_pkg)) {
                    score = (std::max)(score, 100);
                } else if (utility::contains_case_insensitive(task.cmdline, target_pkg)) {
                    score = (std::max)(score, 90);
                } else if (utility::contains_case_insensitive(target_pkg, task.cmdline)) {
                    score = (std::max)(score, 80);
                }
            }

            if (utility::equals_case_insensitive(task.comm, target_pkg)) {
                score = (std::max)(score, 60);
            } else if (utility::matches_task_name(task.comm, target_pkg)) {
                score = (std::max)(score, 50);
            }

            if (task.pid == task.tgid && score > 0) {
                ++score;
            }

            return score;
        };

        s32 score = check_score(package_name);
        if (package_name == XorStrS("com.dts.freefireth")) {
            score = (std::max)(score, check_score(XorStrS("com.dts.freefiremax")));
        } else if (package_name == XorStrS("com.dts.freefiremax")) {
            score = (std::max)(score, check_score(XorStrS("com.dts.freefireth")));
        }

        return score;
    }

    bool c_bluestacks::find_task(const std::string& package_name, c_task& out_task, bool verbose)
    {
        constexpr usize k_max_tasks = 4096;

        const auto init_tasks_head = kernel::k_init_task + offsetof(kernel::c_guest_task_struct, tasks);

        u64 init_tasks_next { };
        u64 init_tasks_prev { };
        if (!memory->read_kernel(init_tasks_head + offsetof(kernel::c_guest_list_head, next), init_tasks_next, verbose) ||
            !memory->read_kernel(init_tasks_head + offsetof(kernel::c_guest_list_head, prev), init_tasks_prev, verbose)) {
            log_verbose(verbose, XorStrS("[bstk] find_task: falha ao ler init_tasks_head (") + utility::hex(init_tasks_head) + XorStrS(")"));
            return false;
        }

        log_verbose(verbose, XorStrS("[bstk] find_task: init_tasks_head=") + utility::hex(init_tasks_head) + XorStrS(" next=") + utility::hex(init_tasks_next) + XorStrS(" prev=") + utility::hex(init_tasks_prev));

        std::unordered_set<u64> seen_heads { };
        seen_heads.reserve(256);
        c_task best_task { };
        s32 best_score { };
        u32 total_tasks_scanned = 0;
        u32 tasks_with_mm = 0;

        auto walk = [&](u64 start_head, bool reverse) {
            u64 current_head = start_head;

            while (current_head != 0 && current_head != init_tasks_head && seen_heads.size() < k_max_tasks) {
                if (current_head < offsetof(kernel::c_guest_task_struct, tasks)) {
                    break;
                }

                if (seen_heads.count(current_head) != 0) {
                    break;
                }

                seen_heads.insert(current_head);
                total_tasks_scanned++;

                c_task current_task { };
                if (read_task(current_head - offsetof(kernel::c_guest_task_struct, tasks), current_task, verbose)) {
                    if (current_task.mm != 0 && utility::is_likely_guest_kernel_pointer(current_task.mm)) {
                        tasks_with_mm++;
                        const auto current_score = score_task(current_task, package_name);
                        if (current_score > best_score) {
                            log_verbose(verbose, XorStrS("[bstk] best task pid=") + std::to_string(current_task.pid) + XorStrS(" score=") + std::to_string(current_score) + XorStrS(" comm=") + current_task.comm + XorStrS(" cmdline=") + current_task.cmdline);
                            best_score = current_score;
                            best_task  = current_task;
                        }
                    }
                }

                u64 next_head { };
                const auto link_offset = reverse ? offsetof(kernel::c_guest_list_head, prev) : offsetof(kernel::c_guest_list_head, next);
                if (!memory->read_kernel(current_head + link_offset, next_head, false)) {
                    break;
                }

                if (next_head == 0 || next_head == current_head) {
                    break;
                }

                current_head = next_head;
            }
        };

        walk(init_tasks_next, false);
        walk(init_tasks_prev, true);

        log_verbose(verbose, XorStrS("[bstk] find_task: escaneados=") + std::to_string(total_tasks_scanned) + XorStrS(" com_mm=") + std::to_string(tasks_with_mm) + XorStrS(" best_score=") + std::to_string(best_score));

        if (best_score == 0 || best_task.mm == 0 || !utility::is_likely_guest_kernel_pointer(best_task.mm))
            return false;

        out_task = best_task;
        return true;
    }
}

#endif