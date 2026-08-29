#include "readloop.hpp"
#include <android/log.h>
#include <thread>
#include <chrono>

#define LOG_TAG "ZmInternal-ReadLoop"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ZmInternal {
    namespace Daemon {
        namespace Game {

            ReadLoop::ReadLoop() {
                // Constructor
            }

            ReadLoop::~ReadLoop() {
                Stop();
            }

            bool ReadLoop::Start() {
                if (m_running.load()) {
                    return true; // Already running
                }

                m_running.store(true);
                m_thread = std::thread(&ReadLoop::ReadLoopThread, this);
                LOGI("Read loop started");
                return true;
            }

            void ReadLoop::Stop() {
                if (!m_running.load()) {
                    return;
                }

                m_running.store(false);
                if (m_thread.joinable()) {
                    m_thread.join();
                }
                LOGI("Read loop stopped");
            }

            bool ReadLoop::GetLatestSnapshot(EntitySnapshot* snapshot, uint32_t& count) {
                std::lock_guard<std::mutex> lock(m_snapshotMutex);
                if (m_readySnapshot->empty()) {
                    return false;
                }

                count = static_cast<uint32_t>(m_readySnapshot->size());
                if (snapshot && count > 0) {
                    // Copy the snapshot data
                    std::copy(m_readySnapshot->begin(), m_readySnapshot->end(), snapshot);
                }
                return true;
            }

            void ReadLoop::ReadLoopThread() {
                LOGI("Read loop thread started");

                while (m_running.load()) {
                    auto startTime = std::chrono::steady_clock::now();

                    // Read entities from memory
                    std::vector<EntitySnapshot> entities;
                    if (ReadEntities(entities)) {
                        // Update snapshots with carry-over logic
                        {
                            std::lock_guard<std::mutex> lock(m_snapshotMutex);

                            // Apply carry-over: keep entities from previous snapshot that are no longer visible
                            if (!entities.empty()) {
                                // Add carry-over entities
                                for (const auto& prevEntity : m_previousSnapshot) {
                                    bool found = false;
                                    for (const auto& currEntity : entities) {
                                        if (prevEntity.entityId == currEntity.entityId) {
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found) {
                                        // Entity was in previous snapshot but not current - add it for carry-over
                                        entities.push_back(prevEntity);
                                    }
                                }

                                // Limit carry-over frames
                                if (m_previousSnapshot.size() > CARRY_OVER_FRAMES * 2) { // Arbitrary limit
                                    m_previousSnapshot.erase(m_previousSnapshot.begin());
                                }
                                m_previousSnapshot = entities; // Store for next frame
                            }

                            // Swap snapshots
                            std::swap(m_currentSnapshot, m_readySnapshot);
                            m_currentSnapshot->clear();
                            m_currentSnapshot->insert(m_currentSnapshot->end(), entities.begin(), entities.end());
                            m_lastValidRead = std::chrono::steady_clock::now();
                        }
                    } else {
                        // Read failed - check watchdog
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastValidRead).count();
                        if (elapsed > WATCHDOG_TIMEOUT_MS) {
                            LOGW("Read loop watchdog triggered - no valid reads for %d ms", elapsed);
                            // In a real implementation, we might try to reacquire the process or reset
                        }
                    }

                    // Sleep to maintain read interval
                    auto endTime = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
                    if (elapsed < READ_INTERVAL_MS) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(READ_INTERVAL_MS - elapsed));
                    }
                }

                LOGI("Read loop thread ended");
            }

            bool ReadLoop::ReadEntities(std::vector<EntitySnapshot>& entities) {
                entities.clear();

                // In a real implementation, we would:
                // 1. Find the game process (already done in daemon initialization)
                // 2. Get the base address of libil2cpp.so
                // 3. Use offsets to find the entity list
                // 4. Read each entity's data

                // For now, we'll return false to indicate this is a placeholder
                // The actual implementation would use Memory::Read and offsets from Offsets.hpp

                // Placeholder: simulate reading some entities
                // In a real cheat, this would be replaced with actual memory reads

                // Simulate having no entities for now
                return false;
            }

            bool ReadLoop::ReadEntity(uintptr_t entityPtr, EntitySnapshot& entity) {
                // In a real implementation, we would read all the fields from memory
                // using the Memory class and appropriate offsets
                // For now, return false as placeholder
                return false;
            }

        } // namespace Game
    } // namespace Daemon
} // namespace ZmInternal