#include "ClothIcons.hpp"

#include <Windows.h>
#include <winhttp.h>
#include <d3dx11.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    enum class IconState : uint8_t
    {
        None = 0,
        Queued,
        Downloading,
        BytesReady,
        Ready,
        Failed
    };

    struct IconSlot
    {
        IconState state = IconState::None;
        std::vector<uint8_t> bytes;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    constexpr int kWorkerCount = 8;

    ID3D11Device* g_Device = nullptr;
    std::mutex g_Mutex;
    std::unordered_map<uint32_t, IconSlot> g_Icons;
    std::queue<uint32_t> g_Queue;
    std::condition_variable g_Cv;
    std::atomic<bool> g_Running{ false };
    std::vector<std::thread> g_Workers;

    // One shared WinHTTP session per worker (created in thread)
    thread_local HINTERNET t_Session = nullptr;

    static std::wstring CachePath(uint32_t itemID)
    {
        wchar_t modulePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        std::filesystem::path dir = std::filesystem::path(modulePath).parent_path() / L"icon_cache";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / (std::to_wstring(itemID) + L".png")).wstring();
    }

    static bool LoadFromDisk(uint32_t itemID, std::vector<uint8_t>& out)
    {
        const std::wstring path = CachePath(itemID);
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        file.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0 || size > 8 * 1024 * 1024)
            return false;
        file.seekg(0, std::ios::beg);
        out.resize(size);
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        return static_cast<bool>(file) || file.eof();
    }

    static void SaveToDisk(uint32_t itemID, const std::vector<uint8_t>& data)
    {
        if (data.empty())
            return;
        const std::wstring path = CachePath(itemID);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return;
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    static HINTERNET EnsureSession()
    {
        if (!t_Session)
        {
            t_Session = WinHttpOpen(L"ClothIcons/1.1",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);
            if (t_Session)
            {
                DWORD timeout = 8000;
                WinHttpSetTimeouts(t_Session, timeout, timeout, timeout, timeout);
            }
        }
        return t_Session;
    }

    static bool DownloadPng(uint32_t itemID, std::vector<uint8_t>& out)
    {
        out.clear();
        HINTERNET session = EnsureSession();
        if (!session)
            return false;

        HINTERNET connect = WinHttpConnect(session, L"ffitems.devhubx.org",
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect)
            return false;

        const std::wstring path = L"/items/" + std::to_wstring(itemID);
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request)
        {
            WinHttpCloseHandle(connect);
            return false;
        }

        // Keep-alive / HTTP1.1
        WinHttpAddRequestHeaders(request, L"Connection: keep-alive", (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

        BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!ok || !WinHttpReceiveResponse(request, nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (status != 200)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            return false;
        }

        out.reserve(16384);
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail))
                break;
            if (avail == 0)
                break;

            const size_t old = out.size();
            out.resize(old + avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, out.data() + old, avail, &read))
            {
                out.resize(old);
                break;
            }
            out.resize(old + read);
            if (out.size() > 8 * 1024 * 1024)
            {
                out.clear();
                break;
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        return out.size() > 8 && out[0] == 0x89 && out[1] == 0x50;
    }

    static void WorkerLoop()
    {
        while (g_Running.load())
        {
            uint32_t itemID = 0;
            {
                std::unique_lock lock(g_Mutex);
                g_Cv.wait(lock, [] { return !g_Running.load() || !g_Queue.empty(); });
                if (!g_Running.load())
                    break;
                itemID = g_Queue.front();
                g_Queue.pop();
                g_Icons[itemID].state = IconState::Downloading;
            }

            std::vector<uint8_t> bytes;
            bool ok = LoadFromDisk(itemID, bytes);
            if (!ok)
            {
                ok = DownloadPng(itemID, bytes);
                if (ok)
                    SaveToDisk(itemID, bytes);
            }

            std::lock_guard lock(g_Mutex);
            auto& slot = g_Icons[itemID];
            if (ok && !bytes.empty())
            {
                slot.bytes = std::move(bytes);
                slot.state = IconState::BytesReady;
            }
            else
            {
                slot.state = IconState::Failed;
            }
        }

        if (t_Session)
        {
            WinHttpCloseHandle(t_Session);
            t_Session = nullptr;
        }
    }

    static void EnqueueUnlocked(uint32_t itemID)
    {
        if (itemID == 0)
            return;
        auto& slot = g_Icons[itemID];
        if (slot.state != IconState::None)
            return;
        slot.state = IconState::Queued;
        g_Queue.push(itemID);
    }
}

namespace ClothIcons
{
    void Init(ID3D11Device* device)
    {
        Shutdown();
        g_Device = device;
        g_Running = true;
        g_Workers.reserve(kWorkerCount);
        for (int i = 0; i < kWorkerCount; ++i)
            g_Workers.emplace_back(WorkerLoop);
    }

    void Shutdown()
    {
        if (g_Running.exchange(false))
        {
            g_Cv.notify_all();
            for (auto& t : g_Workers)
            {
                if (t.joinable())
                    t.join();
            }
            g_Workers.clear();
        }

        std::lock_guard lock(g_Mutex);
        for (auto& [id, slot] : g_Icons)
        {
            if (slot.srv)
            {
                slot.srv->Release();
                slot.srv = nullptr;
            }
        }
        g_Icons.clear();
        while (!g_Queue.empty())
            g_Queue.pop();
        g_Device = nullptr;
    }

    void Request(uint32_t itemID)
    {
        if (itemID == 0 || !g_Device)
            return;
        bool notify = false;
        {
            std::lock_guard lock(g_Mutex);
            const size_t before = g_Queue.size();
            EnqueueUnlocked(itemID);
            notify = g_Queue.size() > before;
        }
        if (notify)
            g_Cv.notify_one();
    }

    void RequestMany(const uint32_t* ids, size_t count)
    {
        if (!ids || count == 0 || !g_Device)
            return;
        size_t added = 0;
        {
            std::lock_guard lock(g_Mutex);
            for (size_t i = 0; i < count; ++i)
            {
                const size_t before = g_Queue.size();
                EnqueueUnlocked(ids[i]);
                if (g_Queue.size() > before)
                    ++added;
            }
        }
        if (added)
            g_Cv.notify_all();
    }

    void Pump()
    {
        if (!g_Device)
            return;

        // Upload up to 24 textures per frame for faster appearance
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> pending;
        {
            std::lock_guard lock(g_Mutex);
            for (auto& [id, slot] : g_Icons)
            {
                if (slot.state == IconState::BytesReady && !slot.bytes.empty() && !slot.srv)
                {
                    pending.emplace_back(id, std::move(slot.bytes));
                    slot.bytes.clear();
                    if (pending.size() >= 24)
                        break;
                }
            }
        }

        for (auto& [id, bytes] : pending)
        {
            ID3D11ShaderResourceView* srv = nullptr;
            HRESULT hr = D3DX11CreateShaderResourceViewFromMemory(
                g_Device, bytes.data(), static_cast<SIZE_T>(bytes.size()),
                nullptr, nullptr, &srv, nullptr);

            std::lock_guard lock(g_Mutex);
            auto& slot = g_Icons[id];
            if (SUCCEEDED(hr) && srv)
            {
                slot.srv = srv;
                slot.state = IconState::Ready;
            }
            else
            {
                slot.state = IconState::Failed;
            }
        }
    }

    ID3D11ShaderResourceView* Get(uint32_t itemID)
    {
        if (itemID == 0)
            return nullptr;

        Request(itemID);

        std::lock_guard lock(g_Mutex);
        auto it = g_Icons.find(itemID);
        if (it == g_Icons.end())
            return nullptr;
        return it->second.srv;
    }
}
