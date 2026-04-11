/*
 * HTTP Web Server Implementation for WSPR-ease
 * Simple socket-based server for maximum portability
 */

#include "webserver.hpp"
#include "wifiManager.hpp"
#include "gnss.hpp"
#include "fpga.hpp"
#include "band.hpp"
#include "filesystem.hpp"
#include "logmanager.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>

LOG_MODULE_REGISTER(webserver, LOG_LEVEL_INF);

// Version string - replaced by Makefile during build
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace wspr {

// Register subsystem with LogManager
static Logger& logger = LogManager::instance().registerSubsystem("web", 
    {"init", "api", "config", "static"});

#define HTTP_PORT 80
#define MAX_REQUEST_SIZE 4096
#define MAX_RESPONSE_SIZE 4096
#define SERVER_STACK_SIZE 16384

static k_thread_stack_t *serverStackPtr = nullptr;
static struct k_thread serverThread;

static int serverSock = -1;
static bool serverRunning = false;

// Request buffer will be dynamically allocated
static char *reqBufPtr = nullptr;

static struct nvs_fs fs;
#define CONFIG_NVS_ID 1

// Forward declarations
static void loadConfigFromNVS();
static void saveConfigToNVS();

// Get content type from file extension
static const char* getContentType(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".txt") == 0) return "text/plain";

    return "application/octet-stream";
}

// Simple HTTP response helpers
static void sendResponse(int clientSock, int st,
                         const char* contentType,
                         const char* body, size_t bodyLen) {
    char header[256];
    const char* statusText = (st == 200) ? "OK" :
                              (st == 404) ? "Not Found" :
                              (st == 500) ? "Internal Server Error" : "Error";

    int headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        st, statusText, contentType, bodyLen);

    zsock_send(clientSock, header, headerLen, 0);
    if (body && bodyLen > 0) {
        zsock_send(clientSock, body, bodyLen, 0);
    }
}

static void sendJSON(int clientSock, const char* json) {
    sendResponse(clientSock, 200, "application/json", json, strlen(json));
}

// API handler: GET /api/status
static void handleAPIStatus(int clientSock) {
    auto& wifi = WifiManager::instance();
    auto& gnss = GNSS::instance();
    auto& fpga = FPGA::instance();

    char buf[600];
    snprintf(buf, sizeof(buf),
        "{"
        "\"wifi\":{"
            "\"connected\":%s,"
            "\"ssid\":\"%s\","
            "\"ip\":\"%s\","
            "\"rssi\":%d"
        "},"
        "\"gnss\":{"
            "\"fix\":%s,"
            "\"satellites\":%d,"
            "\"latitude\":%.6f,"
            "\"longitude\":%.6f,"
            "\"altitude\":%.1f,"
            "\"time\":\"%s\","
            "\"grid\":\"%s\","
            "\"hdop\":%.2f,"
            "\"snr\":%.1f"
        "},"
        "\"fpga\":{"
            "\"initialized\":%s,"
            "\"transmitting\":%s,"
            "\"frequency\":%u"
        "},"
        "\"uptime\":%lld"
        "}",
        wifi.isConnected() ? "true" : "false",
        wifi.getSSID(),
        wifi.getIPAddress(),
        wifi.getRSSI(),
        gnss.hasFix() ? "true" : "false",
        gnss.satellites(),
        gnss.latitude(),
        gnss.longitude(),
        gnss.altitude(),
        gnss.timeString(),
        gnss.gridLocator(),
        (double)gnss.getHDOP(),
        (double)gnss.avgSNR(),
        fpga.isInitialized() ? "true" : "false",
        fpga.isTransmitting() ? "true" : "false",
        fpga.frequency(),
        k_uptime_get() / 1000
    );

    sendJSON(clientSock, buf);
}

// API handler: GET /api/version
static void handleAPIVersion(int clientSock) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"version\":\"%s\"}", APP_VERSION);
    sendJSON(clientSock, buf);
}

// Application configuration state (persisted to flash)
struct AppConfig {
    char callsign[16] = "N0CALL";
    char gridSquare[8] = "AA00";
    int powerDbm = 23;
    char mode[16] = "round-robin";
    int slotIntervalMin = 10;
    char bandList[128] = "";
    bool bandEnabled[10] = {false, false, false, false, true, false, false, false, false, false};
};

static AppConfig appConfig;

// Load configuration from NVS
static void loadConfigFromNVS() {
    const struct flash_area *fa;
    int rc;

    logger.inf("init", "Loading configuration from NVS...");

    rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
    if (rc) {
        logger.err("init", "Flash area open failed: %d (check app.overlay partitions)", rc);
        return;
    }

    fs.flash_device = fa->fa_dev;
    fs.offset = fa->fa_off;
    struct flash_pages_info info;
    rc = flash_get_page_info_by_offs(fa->fa_dev, fs.offset, &info);
    if (rc) {
        logger.err("init", "Flash get page info failed: %d at offset 0x%lx", rc, (long)fs.offset);
        flash_area_close(fa);
        return;
    }

    fs.sector_size = info.size;
    fs.sector_count = fa->fa_size / info.size;

    logger.inf("init", "NVS init: dev=%p, off=0x%lx, sec_sz=%u, sec_cnt=%u",
            fs.flash_device, (long)fs.offset, fs.sector_size, fs.sector_count);

    rc = nvs_mount(&fs);
    if (rc) {
        logger.err("init", "NVS mount failed: %d", rc);
        flash_area_close(fa);
        return;
    }

    rc = nvs_read(&fs, CONFIG_NVS_ID, &appConfig, sizeof(appConfig));
    if (rc == sizeof(appConfig)) {
        logger.inf("init", "Configuration loaded from flash: Callsign=%s Grid=%s", 
                appConfig.callsign, appConfig.gridSquare);
    } else if (rc < 0) {
        logger.wrn("init", "NVS read error: %d (expected %zu), using defaults", rc, sizeof(appConfig));
    } else {
        logger.inf("init", "No valid configuration found in flash (read %d, expected %zu), using defaults", 
                rc, sizeof(appConfig));
    }
    
    flash_area_close(fa);
}

// Save configuration to NVS
static void saveConfigToNVS() {
    logger.inf("config", "Saving configuration to NVS (%zu bytes)...", sizeof(appConfig));
    int rc = nvs_write(&fs, CONFIG_NVS_ID, &appConfig, sizeof(appConfig));
    if (rc < 0) {
        logger.err("config", "Failed to save config to NVS: %d", rc);
    } else if (rc == 0) {
        logger.inf("config", "Configuration already up to date in NVS");
    } else {
        logger.inf("config", "Configuration saved to flash (%d bytes)", rc);
    }
}

// API handler: GET /api/config
static void handleAPIConfigGet(int clientSock) {
    int pos = 0;

    pos += snprintf(reqBufPtr + pos, MAX_REQUEST_SIZE - pos,
        "{"
        "\"callsign\":\"%s\","
        "\"gridSquare\":\"%s\","
        "\"powerDbm\":%d,"
        "\"mode\":\"%s\","
        "\"slotIntervalMin\":%d,"
        "\"bandList\":\"%s\","
        "\"bands\":[",
        appConfig.callsign, appConfig.gridSquare, appConfig.powerDbm,
        appConfig.mode, appConfig.slotIntervalMin, appConfig.bandList
    );

    Band &bands{Band::get()};

    for (int i = 0; i < bands.nBands; i++) {
        pos += snprintf(reqBufPtr + pos, MAX_REQUEST_SIZE - pos,
            "{\"name\":\"%s\",\"freqHz\":%u,\"enabled\":%s}%s",
            bands.metadata[i].name, bands.metadata[i].hz,
            appConfig.bandEnabled[i] ? "true" : "false",
            (i < bands.nBands - 1) ? "," : ""
        );
    }

    pos += snprintf(reqBufPtr + pos, MAX_REQUEST_SIZE - pos, "]}");

    logger.inf("api", "Sending config JSON (%d bytes)", pos);
    sendJSON(clientSock, reqBufPtr);
}

// Simple JSON field extractor (helper for PUT handler)
static bool getJSONString(const char* json, const char* key, char* out, size_t maxLen) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char* ptr = strstr(json, searchKey);
    if (!ptr) return false;
    
    ptr += strlen(searchKey);
    while (*ptr && (*ptr == ' ' || *ptr == ':')) ptr++;
    if (*ptr != '\"') return false;
    ptr++;
    
    const char* end = strchr(ptr, '\"');
    if (!end) return false;
    size_t len = end - ptr;
    if (len >= maxLen) len = maxLen - 1;
    memcpy(out, ptr, len);
    out[len] = '\0';
    return true;
}

static bool getJSONInt(const char* json, const char* key, int* out) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char* ptr = strstr(json, searchKey);
    if (!ptr) return false;

    ptr += strlen(searchKey);
    while (*ptr && (*ptr == ' ' || *ptr == ':')) ptr++;
    
    *out = atoi(ptr);
    return true;
}

// API handler: PUT /api/config
static void handleAPIConfigPut(int clientSock, const char* body) {
    logger.inf("config", "Saving configuration update. Body len: %zu", strlen(body));
    
    if (strlen(body) > 0) {
        char dbgBody[64];
        snprintf(dbgBody, sizeof(dbgBody), "%.60s", body);
        logger.inf("config", "Body start: %s...", dbgBody);
    }

    getJSONString(body, "callsign", appConfig.callsign, sizeof(appConfig.callsign));
    getJSONString(body, "gridSquare", appConfig.gridSquare, sizeof(appConfig.gridSquare));
    getJSONInt(body, "powerDbm", &appConfig.powerDbm);
    getJSONString(body, "mode", appConfig.mode, sizeof(appConfig.mode));
    getJSONInt(body, "slotIntervalMin", &appConfig.slotIntervalMin);
    getJSONString(body, "bandList", appConfig.bandList, sizeof(appConfig.bandList));

    Band &bands{Band::get()};
    const char* bandsStart = strstr(body, "\"bands\":[");
    if (bandsStart) {
        for (int i = 0; i < bands.nBands; i++) {
            char bandSearch[64];
            snprintf(bandSearch, sizeof(bandSearch), "\"name\":\"%s\"", bands.metadata[i].name);
            const char* bandP = strstr(bandsStart, bandSearch);
            if (bandP) {
                const char* enabledP = strstr(bandP, "\"enabled\":");
                if (enabledP) {
                    const char* valP = enabledP + 10;
                    while (*valP == ' ' || *valP == ':') valP++;
                    appConfig.bandEnabled[i] = (strncmp(valP, "true", 4) == 0);
                }
            }
        }
    }

    saveConfigToNVS();
    sendJSON(clientSock, "{\"status\":\"ok\"}");
}

// API handler: POST /api/tx/trigger
static void handleAPITXTrigger(int clientSock) {
    logger.inf("api", "Manual TX trigger requested");
    sendJSON(clientSock, "{\"message\":\"TX triggered (stub)\"}");
}

// API handler: GET /api/files?path=/
static void handleAPIFilesList(int clientSock) {
    if (!FileSystem::instance().isMounted()) {
        sendJSON(clientSock, "{\"files\":[]}");
        return;
    }

    char buf[1024];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"files\":[");

    const char* mountPoint = FileSystem::instance().getMountPoint();
    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, mountPoint) == 0) {
        struct fs_dirent entry;
        bool first = true;
        while (fs_readdir(&dir, &entry) == 0 && entry.name[0]) {
            if (entry.type == FS_DIR_ENTRY_DIR) continue;
            if (!first) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "{\"name\":\"%s\",\"size\":%zu,\"isDirectory\":false}",
                entry.name, (size_t)entry.size);
            first = false;
        }
        fs_closedir(&dir);
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    sendJSON(clientSock, buf);
}

// API handler: GET /api/files/{filename}
static void handleAPIFileGet(int clientSock, const char* filename) {
    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", FileSystem::instance().getMountPoint(), filename);

    struct fs_file_t file;
    fs_file_t_init(&file);

    if (fs_open(&file, fullPath, FS_O_READ) < 0) {
        sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        return;
    }

    struct fs_dirent stat;
    if (fs_stat(fullPath, &stat) < 0) {
        fs_close(&file);
        sendResponse(clientSock, 500, "text/plain", "Stat Error", 10);
        return;
    }

    char header[256];
    int headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %zu\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Connection: close\r\n"
        "\r\n",
        (size_t)stat.size, filename);

    zsock_send(clientSock, header, headerLen, 0);

    ssize_t bytesRead;
    while ((bytesRead = fs_read(&file, reqBufPtr, MAX_REQUEST_SIZE)) > 0) {
        zsock_send(clientSock, reqBufPtr, bytesRead, 0);
    }

    fs_close(&file);
}

// API handler: PUT /api/files/{filename}
static void handleAPIFilePut(int clientSock, const char* filename, const char* body, size_t bodyLen) {
    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", FileSystem::instance().getMountPoint(), filename);

    struct fs_file_t file;
    fs_file_t_init(&file);

    if (fs_open(&file, fullPath, FS_O_CREATE | FS_O_WRITE) < 0) {
        sendResponse(clientSock, 500, "text/plain", "Create Error", 12);
        return;
    }

    fs_write(&file, body, bodyLen);
    fs_close(&file);

    logger.inf("api", "File written: %s (%zu bytes)", filename, bodyLen);
    sendJSON(clientSock, "{\"status\":\"ok\"}");
}

// API handler: DELETE /api/files/{filename}
static void handleAPIFileDelete(int clientSock, const char* filename) {
    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", FileSystem::instance().getMountPoint(), filename);

    if (fs_unlink(fullPath) < 0) {
        sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        return;
    }

    logger.inf("api", "File deleted: %s", filename);
    sendJSON(clientSock, "{\"status\":\"ok\"}");
}

// Fallback HTML
static const char* fallbackHtml =
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>WSPR-ease</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;padding:20px;max-width:600px;margin:0 auto;}"
    "h1{color:#2563eb;}"
    ".status{background:#f3f4f6;padding:15px;border-radius:8px;margin:20px 0;}"
    "a{color:#2563eb;}"
    "</style>"
    "</head><body>"
    "<h1>WSPR-ease</h1>"
    "<div class=\"status\">"
    "<p>Web UI files not yet installed on device.</p>"
    "</div>"
    "<h2>API Endpoints</h2>"
    "<ul>"
    "<li><a href=\"/api/status\">/api/status</a></li>"
    "<li><a href=\"/api/config\">/api/config</a></li>"
    "</ul>"
    "</body></html>";

// Serve static files from LittleFS
static void handleStatic(int clientSock, const char* path) {
    if (!FileSystem::instance().isMounted()) {
        sendResponse(clientSock, 200, "text/html", fallbackHtml, strlen(fallbackHtml));
        return;
    }

    char fullPath[256];
    const char* mountPoint = FileSystem::instance().getMountPoint();
    if (strcmp(path, "/") == 0) {
        snprintf(fullPath, sizeof(fullPath), "%s/index.html", mountPoint);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s%s", mountPoint, path);
    }

    struct fs_file_t file;
    fs_file_t_init(&file);

    int ret = fs_open(&file, fullPath, FS_O_READ);
    if (ret < 0) {
        logger.inf("static", "static GET: '%s'", fullPath);
        sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        return;
    }

    struct fs_dirent stat;
    ret = fs_stat(fullPath, &stat);
    if (ret < 0) {
        fs_close(&file);
        sendResponse(clientSock, 500, "text/plain", "Stat Error", 10);
        return;
    }

    const char* contentType = getContentType(fullPath);
    char header[256];
    int headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: max-age=3600\r\n"
        "\r\n",
        contentType, (size_t)stat.size);

    ssize_t sent = 0;
    while (sent < headerLen) {
        ssize_t n = zsock_send(clientSock, header + sent, headerLen - sent, 0);
        if (n <= 0) {
            logger.err("static", "Failed to send header");
            fs_close(&file);
            return;
        }
        sent += n;
    }

    ssize_t bytesRead;
    while ((bytesRead = fs_read(&file, reqBufPtr, MAX_REQUEST_SIZE)) > 0) {
        ssize_t chunkSent = 0;
        while (chunkSent < bytesRead) {
            ssize_t n = zsock_send(clientSock, (uint8_t*)reqBufPtr + chunkSent,
                                   bytesRead - chunkSent, 0);
            if (n <= 0) {
                fs_close(&file);
                return;
            }
            chunkSent += n;
        }
    }

    fs_close(&file);
}

// Find HTTP body start
static const char* findBody(const char* request) {
    const char* body = strstr(request, "\r\n\r\n");
    if (body) return body + 4;
    body = strstr(request, "\n\n");
    if (body) return body + 2;
    return nullptr;
}

// Parse HTTP request and route
static void handleRequest(int clientSock, const char* request, size_t requestLen) {
    char method[8] = {0};
    char path[128] = {0};

    if (sscanf(request, "%7s %127s", method, path) != 2) {
        sendResponse(clientSock, 400, "text/plain", "Bad Request", 11);
        return;
    }

    logger.inf("api", "HTTP %s %s", method, path);

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/status") == 0) handleAPIStatus(clientSock);
        else if (strcmp(path, "/api/version") == 0) handleAPIVersion(clientSock);
        else if (strcmp(path, "/api/config") == 0) handleAPIConfigGet(clientSock);
        else if (strncmp(path, "/api/files", 10) == 0) {
            if (path[10] == '?' || path[10] == '\0') handleAPIFilesList(clientSock);
            else if (path[10] == '/') handleAPIFileGet(clientSock, path + 11);
            else sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        } else handleStatic(clientSock, path);
    } else if (strcmp(method, "PUT") == 0) {
        if (strcmp(path, "/api/config") == 0) {
            const char* body = findBody(request);
            handleAPIConfigPut(clientSock, body ? body : "");
        } else if (strncmp(path, "/api/files/", 11) == 0) {
            const char* body = findBody(request);
            size_t bodyLen = body ? (requestLen - (body - request)) : 0;
            handleAPIFilePut(clientSock, path + 11, body ? body : "", bodyLen);
        } else sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/tx/trigger") == 0) handleAPITXTrigger(clientSock);
        else sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
    } else if (strcmp(method, "DELETE") == 0) {
        if (strncmp(path, "/api/files/", 11) == 0) handleAPIFileDelete(clientSock, path + 11);
        else sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
    } else if (strcmp(method, "OPTIONS") == 0) {
        char header[] = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        zsock_send(clientSock, header, strlen(header), 0);
    } else {
        sendResponse(clientSock, 405, "text/plain", "Method Not Allowed", 18);
    }
}

// Server thread function
static void serverThreadFn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    logger.inf("init", "HTTP server thread started, port %d", HTTP_PORT);

    while (serverRunning) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientSock = zsock_accept(serverSock, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSock < 0) continue;

        struct zsock_timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        zsock_setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int ret = zsock_recv(clientSock, reqBufPtr, MAX_REQUEST_SIZE - 1, 0);
        if (ret > 0) {
            int totalLen = ret;
            reqBufPtr[totalLen] = '\0';
            if (strncmp(reqBufPtr, "PUT", 3) == 0 || strncmp(reqBufPtr, "POST", 4) == 0) {
                const char* clP = strstr(reqBufPtr, "Content-Length:");
                if (clP) {
                    int contentLen = atoi(clP + 15);
                    const char* bodyStart = findBody(reqBufPtr);
                    int curBodyLen = bodyStart ? (totalLen - (bodyStart - reqBufPtr)) : 0;
                    while (curBodyLen < contentLen && totalLen < MAX_REQUEST_SIZE - 1) {
                        ret = zsock_recv(clientSock, reqBufPtr + totalLen, MAX_REQUEST_SIZE - 1 - totalLen, 0);
                        if (ret <= 0) break;
                        totalLen += ret;
                        curBodyLen += ret;
                        reqBufPtr[totalLen] = '\0';
                    }
                }
            }
            handleRequest(clientSock, reqBufPtr, totalLen);
        }
        zsock_close(clientSock);
    }
    logger.inf("init", "HTTP server thread exiting");
}

int WebServer::init() {
    logger.inf("init", "Initializing web server");
    loadConfigFromNVS();
    return 0;
}

int WebServer::start(uint16_t port) {
    logger.inf("init", "Starting web server on port %d", port);
    if (!serverStackPtr) {
        serverStackPtr = (k_thread_stack_t *)k_aligned_alloc(ARCH_STACK_PTR_ALIGN, K_THREAD_STACK_LEN(SERVER_STACK_SIZE));
        if (!serverStackPtr) return -ENOMEM;
    }
    if (!reqBufPtr) {
        reqBufPtr = (char *)k_malloc(MAX_REQUEST_SIZE);
        if (!reqBufPtr) return -ENOMEM;
    }
    serverSock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock < 0) return -errno;
    int opt = 1;
    zsock_setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (zsock_bind(serverSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        zsock_close(serverSock);
        serverSock = -1;
        return -errno;
    }
    if (zsock_listen(serverSock, 8) < 0) {
        zsock_close(serverSock);
        serverSock = -1;
        return -errno;
    }
    serverRunning = true;
    k_thread_create(&serverThread, serverStackPtr, SERVER_STACK_SIZE, serverThreadFn, NULL, NULL, NULL, K_PRIO_COOP(10), 0, K_NO_WAIT);
    k_thread_name_set(&serverThread, "httpServer");
    running = true;
    return 0;
}

void WebServer::stop() {
    if (!running) return;
    logger.inf("init", "Stopping web server");
    serverRunning = false;
    if (serverSock >= 0) {
        zsock_close(serverSock);
        serverSock = -1;
    }
    k_thread_join(&serverThread, K_SECONDS(5));
    if (serverStackPtr) { k_free(serverStackPtr); serverStackPtr = nullptr; }
    if (reqBufPtr) { k_free(reqBufPtr); reqBufPtr = nullptr; }
    running = false;
}

} // namespace wspr
