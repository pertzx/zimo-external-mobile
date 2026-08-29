#include "resolver.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace ZmInternal {
    namespace Daemon {
        namespace Memory {

            Resolver::Resolver() {
                // Constructor
            }

            Resolver::~Resolver() {
                // Destructor
            }

            bool Resolver::Initialize() {
                // In a real implementation, we might do some initial setup here
                // For now, just return true
                return true;
            }

            bool Resolver::FindTargetProcess(uintptr_t& pid) {
                // Read /proc/*/cmdline to find Free Fire process
                // We're looking for com.garena.game.freefire or similar

                std::ifstream procDir("/proc");
                if (!procDir.is_open()) {
                    return false;
                }

                std::string line;
                while (std::getline(procDir, line)) {
                    // Check if line is a number (PID directory)
                    bool isNumber = !line.empty() &&
                                    std::all_of(line.begin(), line.end(), ::isdigit);

                    if (isNumber) {
                        uintptr_t testPid = std::stoul(line);
                        std::string cmdlinePath = "/proc/" + line + "/cmdline";

                        std::ifstream cmdlineFile(cmdlinePath);
                        if (cmdlineFile.is_open()) {
                            std::string cmdline;
                            std::getline(cmdlineFile, cmdline, '\0'); // Read until null terminator

                            // Check if this is Free Fire
                            if (cmdline.find("freefire") != std::string::npos ||
                                cmdline.find("FreeFire") != std::string::npos ||
                                cmdline.find("GArena") != std::string::npos) {
                                pid = testPid;
                                cmdlineFile.close();
                                return true;
                            }
                            cmdlineFile.close();
                        }
                    }
                }

                return false;
            }

            bool Resolver::FindIl2cppBase(uintptr_t pid, uintptr_t& baseAddress) {
                // Read /proc/<pid>/maps to find libil2cpp.so
                std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
                std::ifstream mapsFile(mapsPath);

                if (!mapsFile.is_open()) {
                    return false;
                }

                std::string line;
                while (std::getline(mapsFile, line)) {
                    // Look for libil2cpp.so in the maps
                    if (line.find("libil2cpp.so") != std::string::npos) {
                        // Parse the address range (first part of the line)
                        std::istringstream iss(line);
                        std::string addressRange;
                        if (std::getline(iss, addressRange, '-')) {
                            try {
                                baseAddress = std::stoul(addressRange, nullptr, 16);
                                mapsFile.close();
                                return true;
                            } catch (...) {
                                // Continue to next line
                            }
                        }
                    }
                }

                mapsFile.close();
                return false;
            }

        } // namespace Memory
    } // namespace Daemon
} // namespace ZmInternal