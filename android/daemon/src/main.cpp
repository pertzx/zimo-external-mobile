#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <string>
#include <memory>
#include <cstdint>

#include "daemon.hpp"

volatile sig_atomic_t g_running = 1;
std::unique_ptr<ZmInternal::Daemon::Daemon> g_daemon;

void signal_handler(int signal) {
    if (signal == SIGTERM || signal == SIGINT) {
        g_running = 0;
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string target_pid_str;
    std::string socket_name = "\\0zm_internal_ipc"; // default abstract socket name

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--target-pid" && i + 1 < argc) {
            target_pid_str = argv[++i];
        } else if (arg == "--socket-name" && i + 1 < argc) {
            socket_name = argv[++i];
        }
    }

    if (target_pid_str.empty()) {
        std::cerr << "Error: --target-pid is required" << std::endl;
        return 1;
    }

    pid_t target_pid = std::stoi(target_pid_str);

    // Set up signal handlers
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    std::cout << "Daemon started. Target PID: " << target_pid << ", Socket: " << socket_name << std::endl;

    // Create and initialize daemon
    g_daemon = std::make_unique<ZmInternal::Daemon::Daemon>();
    if (!g_daemon->Initialize(target_pid, socket_name.c_str())) {
        std::cerr << "Failed to initialize daemon" << std::endl;
        return 1;
    }

    // Run daemon
    g_daemon->Run();

    // Cleanup
    g_daemon->Shutdown();

    std::cout << "Daemon shutting down..." << std::endl;
    return 0;
}