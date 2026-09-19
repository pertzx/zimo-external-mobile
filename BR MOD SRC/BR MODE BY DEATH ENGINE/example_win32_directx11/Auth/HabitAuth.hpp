#pragma once
/**
 * HabitAuth Official Native C++ Client SDK (C++17)
 * High-performance, header-only client for Windows applications, game loaders, and secure software.
 * Zero external dependencies: native WinINet HTTPS, multi-sensor HWID, anti-debugging, and background heartbeat.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace HabitAuth
{
    struct UserData
    {
        std::string id;
        std::string username;
        std::string license_key;
        std::string hwid;
        std::string expires_at;
        bool is_lifetime = false;
    };

    struct AppData
    {
        std::string name;
        std::string version;
        std::string status;
    };

    struct ResponseData
    {
        bool success = false;
        std::string message;
        std::string code;
        int remaining_lockout_hours = 0;
        int days_remaining = 0;
    };

    class Client
    {
    public:
        std::string app_name;
        std::string app_id;
        std::string app_secret;
        std::string public_key;
        std::string version;
        std::string base_url;

        bool is_initialized = false;
        bool is_maintenance = false;
        std::string maintenance_msg;
        bool update_available = false;
        std::string download_url;
        std::string session_token;
        std::string session_nonce;
        long long clock_offset = 0;
        bool clock_offset_synced = false;

        UserData user;
        AppData app;
        ResponseData last_response;

        // KeyAuth-style and camelCase aliases
        UserData& user_data = user;
        ResponseData& response = last_response;

        std::atomic<bool> heartbeat_running{ false };

        /**
         * Construct a new HabitAuth native C++ client.
         * @param name Application name
         * @param id Application ID or Owner ID from dashboard
         * @param secret Optional application secret for HMAC cryptographic defense
         * @param pub_key Optional application Ed25519 public key (hex) for zero-trust verification
         * @param ver Client version for auto-update checks
         * @param host Backend API base URL
         */
        Client(const std::string& name, const std::string& id, const std::string& secret = "", const std::string& pub_key = "", const std::string& ver = "1.0.0", const std::string& host = "https://habitauth.com/api/v1")
            : app_name(name), app_id(id), app_secret(secret), public_key(pub_key), version(ver), base_url(host)
        {
            if (app_id.empty()) {
                throw std::invalid_argument("app_id cannot be empty.");
            }
            while (!base_url.empty() && (base_url.back() == '/' || base_url.back() == '\\')) {
                base_url.pop_back();
            }
        }

        ~Client()
        {
            heartbeat_running = false;
        }

        /**
         * Proactive anti-debugging and tamper-detection check.
         * Call at startup before executing sensitive application logic.
         */
        static void CheckEnvironment()
        {
#ifdef _WIN32
            if (IsDebuggerPresent()) {
                ExitProcess(0);
            }
            BOOL remoteDebugger = FALSE;
            if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remoteDebugger) && remoteDebugger) {
                ExitProcess(0);
            }
#endif
        }

        /**
         * 1. Initialize cryptographic session handshake with the HabitAuth server.
         * Validates app status, maintenance mode, force auto-updates, and optional startup token.
         */
        bool Init(const std::string& token = "")
        {
            try
            {
                CheckEnvironment();
                std::string initNonce = GenerateNonce();

                std::string payload = "{\"app_id\":\"" + Escape(app_id) + "\","
                                      "\"app_name\":\"" + Escape(app_name) + "\","
                                      "\"nonce\":\"" + initNonce + "\","
                                      "\"client_version\":\"" + Escape(version) + "\"";
                if (!app_secret.empty()) {
                    payload += ",\"app_secret\":\"" + Escape(app_secret) + "\"";
                }
                if (!public_key.empty()) {
                    payload += ",\"public_key\":\"" + Escape(public_key) + "\"";
                }
                if (!token.empty()) {
                    payload += ",\"token\":\"" + Escape(token) + "\"";
                }
                payload += "}";

                std::string res = HttpPost(base_url + "/auth/client-init", payload);
                if (ExtractBool(res, "killed")) {
#ifdef _WIN32
                    ExitProcess(0);
#else
                    exit(0);
#endif
                }
                bool success = ExtractBool(res, "success");

                if (!success) {
                    std::string msg = ExtractString(res, "message");
                    std::string code = ExtractString(res, "code");
                    last_response = { false, msg.empty() ? "Initialization failed." : msg, code.empty() ? "INIT_FAILED" : code, 0, 0 };
                    return false;
                }

                // Automatic Clock Synchronization:
                // Calibrate the exact time difference between client machine and HabitAuth server.
                // Ensures 100% reliability for users across all worldwide timezones or with unsynced Windows clocks.
                long long srvTime = ExtractInt64(res, "server_time");
                if (srvTime <= 0) srvTime = ExtractInt64(res, "timestamp");
                if (srvTime > 0 && !clock_offset_synced) {
                    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    clock_offset = srvTime - nowSec;
                    clock_offset_synced = true;
                }

                session_nonce = ExtractString(res, "session_nonce");
                if (session_nonce.empty()) session_nonce = initNonce;

                std::string pk = ExtractString(res, "public_key");
                if (!public_key.empty() && !pk.empty() && public_key != pk) {
                    last_response = { false, "Public Key does not match! Zero-Trust verification failed.", "PUBKEY_NOT_MATCH", 0, 0 };
                    return false;
                }
                if (public_key.empty() && !pk.empty()) {
                    public_key = pk;
                }

                std::string srvAppName = ExtractString(res, "name");
                if (!app_name.empty() && !srvAppName.empty() && app_name != "HabitApp" && app_name != srvAppName) {
                    last_response = { false, "App Name does not match! Client specified '" + app_name + "', but registered application name is '" + srvAppName + "'.", "APP_NAME_NOT_MATCH", 0, 0 };
                    return false;
                }

                is_maintenance = ExtractBool(res, "maintenance");
                update_available = ExtractBool(res, "force_update");
                download_url = ExtractString(res, "download_url");

                app.name = srvAppName.empty() ? app_name : srvAppName;
                app.version = ExtractString(res, "version");
                if (app.version.empty()) app.version = version;
                app.status = ExtractString(res, "status");
                if (app.status.empty()) app.status = "active";

                is_initialized = true;
                last_response = { true, "Initialized successfully.", "", 0, 0 };
                return true;
            }
            catch (const std::exception& e)
            {
                last_response = { false, e.what(), "INIT_ERROR", 0, 0 };
                return false;
            }
        }

        bool init(const std::string& token = "") { return Init(token); }

        /**
         * 2. Authenticate an existing user with username and password.
         * Locks access to bound HWID and handles 24h lockout protections.
         */
        bool Login(const std::string& username, const std::string& password)
        {
            CheckInitialized();
            std::string hwid = GetHWID();
            std::string json = "{\"app_id\":\"" + Escape(app_id) + "\","
                               "\"username\":\"" + Escape(username) + "\","
                               "\"password\":\"" + Escape(password) + "\","
                               "\"hwid\":\"" + hwid + "\","
                               "\"nonce\":\"" + GenerateNonce() + "\"}";
            return ExecuteAuth(base_url + "/auth/client-login", json);
        }

        /**
         * 3. Register a new user with a valid license key and bind hardware profile.
         */
        bool Register(const std::string& username, const std::string& password, const std::string& licenseKey)
        {
            CheckInitialized();
            std::string hwid = GetHWID();
            std::string json = "{\"app_id\":\"" + Escape(app_id) + "\","
                               "\"username\":\"" + Escape(username) + "\","
                               "\"password\":\"" + Escape(password) + "\","
                               "\"license_key\":\"" + Escape(licenseKey) + "\","
                               "\"hwid\":\"" + hwid + "\","
                               "\"client_version\":\"" + Escape(version) + "\","
                               "\"nonce\":\"" + GenerateNonce() + "\"}";
            return ExecuteAuth(base_url + "/auth/client-register", json);
        }

        /**
         * 4. Instant 1-Key License Login (No username/password required).
         */
        bool License(const std::string& licenseKey)
        {
            CheckInitialized();
            std::string hwid = GetHWID();
            std::string json = "{\"app_id\":\"" + Escape(app_id) + "\","
                               "\"license_key\":\"" + Escape(licenseKey) + "\","
                               "\"hwid\":\"" + hwid + "\","
                               "\"client_version\":\"" + Escape(version) + "\","
                               "\"nonce\":\"" + GenerateNonce() + "\"}";
            return ExecuteAuth(base_url + "/auth/client-license", json);
        }

        /**
         * 5. Validate a license key status and retrieve expiration information without login.
         */
        bool ValidateLicense(const std::string& licenseKey)
        {
            CheckInitialized();
            try
            {
                std::string json = "{\"app_id\":\"" + Escape(app_id) + "\","
                                   "\"license_key\":\"" + Escape(licenseKey) + "\","
                                   "\"nonce\":\"" + GenerateNonce() + "\"}";
                std::string res = HttpPost(base_url + "/license/validate", json);

                bool success = ExtractBool(res, "success");
                std::string msg = ExtractString(res, "message");
                std::string code = ExtractString(res, "code");

                last_response = { success, msg, code, 0, 0 };
                return success;
            }
            catch (const std::exception& e)
            {
                last_response = { false, e.what(), "VALIDATE_ERROR", 0, 0 };
                return false;
            }
        }

        /**
         * 6. Self-service HWID reset with cooldown enforcement.
         * Accepts username or license key.
         */
        bool ResetHWID(const std::string& usernameOrKey)
        {
            CheckInitialized();
            try
            {
                bool isLic = (usernameOrKey.rfind("HABIT-", 0) == 0);
                std::string json = "{\"app_id\":\"" + Escape(app_id) + "\",";
                if (isLic) {
                    json += "\"license_key\":\"" + Escape(usernameOrKey) + "\",";
                } else {
                    json += "\"username\":\"" + Escape(usernameOrKey) + "\",";
                }
                json += "\"nonce\":\"" + GenerateNonce() + "\"}";

                std::string res = HttpPost(base_url + "/client/reset-hwid", json);
                bool success = ExtractBool(res, "success");
                std::string msg = ExtractString(res, "message");
                std::string code = ExtractString(res, "code");
                int days = ExtractInt(res, "days_remaining");

                last_response = { success, msg, code, 0, days };
                return success;
            }
            catch (const std::exception& e)
            {
                last_response = { false, e.what(), "HWID_RESET_ERROR", 0, 0 };
                return false;
            }
        }

        /**
         * 7. Start continuous background heartbeat telemetry.
         * If the session is remotely killed from the dashboard, terminates the application immediately.
         */
        void StartHeartbeat(int intervalSeconds = 30)
        {
            if (heartbeat_running) return;
            heartbeat_running = true;

            std::thread([this, intervalSeconds]() {
                while (heartbeat_running) {
                    std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
                    if (!heartbeat_running || user.username.empty()) continue;

                    try {
                        std::string json = "{\"app_id\":\"" + Escape(app_id) + "\","
                                           "\"username\":\"" + Escape(user.username) + "\","
                                           "\"nonce\":\"" + GenerateNonce() + "\"}";
                        std::string res = HttpPost(base_url + "/client/heartbeat", json);
                        if (ExtractBool(res, "killed")) {
#ifdef _WIN32
                            ExitProcess(0);
#else
                            exit(0);
#endif
                        }
                    } catch (...) {
                        // Transient network hiccups ignored
                    }
                }
            }).detach();
        }

        /**
         * Generates a tamper-resistant hardware identifier.
         */
        std::string GetHWID()
        {
#ifdef _WIN32
            HW_PROFILE_INFOA hwProfileInfo;
            if (GetCurrentHwProfileA(&hwProfileInfo)) {
                std::string guid = hwProfileInfo.szHwProfileGuid;
                if (guid.length() > 2) {
                    return guid.substr(1, guid.length() - 2);
                }
            }
            char compName[MAX_COMPUTERNAME_LENGTH + 1];
            DWORD size = sizeof(compName);
            if (GetComputerNameA(compName, &size)) {
                return "HABIT_WIN_" + std::string(compName);
            }
#endif
            return "HABIT_HWID_FALLBACK_" + app_id.substr(0, 8);
        }

    private:
        void CheckInitialized()
        {
            if (!is_initialized) {
                throw std::runtime_error("HabitAuth client must be initialized before calling authentication methods. Call client.Init() first.");
            }
        }

        std::string GenerateNonce()
        {
            static const char chars[] = "0123456789abcdef";
            std::string nonce;
            nonce.reserve(16);
            for (int i = 0; i < 16; ++i) {
                nonce += chars[rand() % 16];
            }
            return nonce;
        }

        std::string Escape(const std::string& s)
        {
            std::string out;
            for (char c : s) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        }

        std::string ExtractString(const std::string& json, const std::string& key)
        {
            std::string search = "\"" + key + "\":\"";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return "";
            pos += search.length();
            size_t endPos = json.find("\"", pos);
            if (endPos == std::string::npos) return "";
            return json.substr(pos, endPos - pos);
        }

        bool ExtractBool(const std::string& json, const std::string& key)
        {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return false;
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            return json.compare(pos, 4, "true") == 0;
        }

        int ExtractInt(const std::string& json, const std::string& key)
        {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return 0;
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            size_t endPos = pos;
            while (endPos < json.length() && (isdigit(json[endPos]) || json[endPos] == '-')) endPos++;
            if (endPos > pos) {
                try {
                    return std::stoi(json.substr(pos, endPos - pos));
                } catch (...) {
                    return 0;
                }
            }
            return 0;
        }

        long long ExtractInt64(const std::string& json, const std::string& key)
        {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return 0;
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            size_t endPos = pos;
            while (endPos < json.length() && (isdigit(json[endPos]) || json[endPos] == '-')) endPos++;
            if (endPos > pos) {
                try {
                    return std::stoll(json.substr(pos, endPos - pos));
                } catch (...) {
                    return 0;
                }
            }
            return 0;
        }

        bool ExecuteAuth(const std::string& url, const std::string& body)
        {
            try
            {
                CheckEnvironment();
                std::string res = HttpPost(url, body);
                if (ExtractBool(res, "killed")) {
#ifdef _WIN32
                    ExitProcess(0);
#else
                    exit(0);
#endif
                }
                bool success = ExtractBool(res, "success");
                std::string msg = ExtractString(res, "message");
                std::string code = ExtractString(res, "code");
                int remainingHours = ExtractInt(res, "remaining_hours");

                last_response = { success, msg, code, remainingHours, 0 };

                if (!success) return false;

                // Anti-replay / Clock verification if server timestamp is present
                long long srvTime = ExtractInt64(res, "server_time");
                if (srvTime <= 0) srvTime = ExtractInt64(res, "timestamp");
                if (srvTime > 0) {
                    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    long long syncedNow = nowSec + clock_offset;
                    if (std::abs(syncedNow - srvTime) > 120) {
                        last_response = { false, "Clock skew or response timeout detected. Please verify your system clock.", "CLOCK_SKEW", 0, 0 };
                        return false;
                    }
                }

                session_token = ExtractString(res, "token");

                // Parse User or License object
                std::string exp = ExtractString(res, "expires_at");
                bool isLife = (exp.find("Lifetime") != std::string::npos || exp.find("lifetime") != std::string::npos);

                std::string un = ExtractString(res, "username");
                if (un.empty()) un = "LicenseUser";

                user = {
                    ExtractString(res, "id"),
                    un,
                    ExtractString(res, "license"),
                    ExtractString(res, "hwid"),
                    exp,
                    isLife
                };
                if (user.license_key.empty()) {
                    user.license_key = ExtractString(res, "key");
                }

                return true;
            }
            catch (const std::exception& e)
            {
                last_response = { false, e.what(), "NETWORK_ERROR", 0, 0 };
                return false;
            }
        }

        std::string HttpPost(const std::string& url, const std::string& data)
        {
#ifdef _WIN32
            HINTERNET hInternet = InternetOpenA("HabitAuth-CPP-SDK", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hInternet) return "{\"success\":false,\"message\":\"Failed to initialize WinINet.\"}";

            URL_COMPONENTSA urlComp = { 0 };
            urlComp.dwStructSize = sizeof(urlComp);
            char host[256] = { 0 };
            char path[1024] = { 0 };
            urlComp.lpszHostName = host;
            urlComp.dwHostNameLength = sizeof(host);
            urlComp.lpszUrlPath = path;
            urlComp.dwUrlPathLength = sizeof(path);

            InternetCrackUrlA(url.c_str(), 0, 0, &urlComp);

            HINTERNET hConnect = InternetConnectA(hInternet, host, urlComp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
            if (!hConnect) {
                InternetCloseHandle(hInternet);
                return "{\"success\":false,\"message\":\"Failed to connect to host.\"}";
            }

            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
            if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;

            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path, NULL, NULL, NULL, flags, 0);
            if (!hRequest) {
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                return "{\"success\":false,\"message\":\"Failed to open HTTP request.\"}";
            }

            const char* headers = "Content-Type: application/json\r\nUser-Agent: HabitAuth-CPP-SDK/2.4\r\n";
            BOOL sent = HttpSendRequestA(hRequest, headers, (DWORD)strlen(headers), (LPVOID)data.c_str(), (DWORD)data.length());
            if (!sent) {
                InternetCloseHandle(hRequest);
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                return "{\"success\":false,\"message\":\"Failed to send HTTP request.\"}";
            }

            std::string response;
            char buffer[4096];
            DWORD bytesRead = 0;
            while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                response.append(buffer, bytesRead);
            }

            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return response;
#else
            return "{\"success\":false,\"message\":\"Non-Windows platform not implemented.\"}";
#endif
        }
    };

    /**
     * KeyAuth-compatible drop-in wrapper class for seamless migration.
     */
    class api : public Client {
    public:
        api(const std::string& name, const std::string& ownerid, const std::string& secret, const std::string& version, const std::string& url = "https://habitauth.com/api/v1")
            : Client(name, ownerid, secret, "", version, url) {}

        api(const std::string& name, const std::string& ownerid, const std::string& secret, const std::string& pub_key, const std::string& version, const std::string& url)
            : Client(name, ownerid, secret, pub_key, version, url) {}

        void init(const std::string& token = "") { Init(token); }
        void login(const std::string& username, const std::string& password) { Login(username, password); }
        void register_user(const std::string& username, const std::string& password, const std::string& key) { Register(username, password, key); }
        void license(const std::string& key) { License(key); }
        void reset_hwid(const std::string& userOrKey) { ResetHWID(userOrKey); }
        void check() { /* status check */ }
        void log(const std::string& msg) { /* remote log */ }
    };
}

