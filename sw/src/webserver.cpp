/*
 * HTTP Web Server Implementation for WSPR-ease
 * Simple socket-based server for maximum portability
 */

#include "webserver.hpp"
#include "wifiManager.hpp"
#include "gnss.hpp"
#include "fpga.hpp"
#include "band.hpp"

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

#define HTTP_PORT 80
#define MAX_REQUEST_SIZE 4096
#define MAX_RESPONSE_SIZE 4096
#define WEBROOT "/lfs"

static K_THREAD_STACK_DEFINE(serverStack, 16384);
static struct k_thread serverThread;

static int serverSock = -1;
static bool serverRunning = false;
static bool littlefsMounted = false;

// Global request buffer to save stack space
static char reqBuf[MAX_REQUEST_SIZE];

static struct nvs_fs fs;
#define CONFIG_NVS_ID 1

// Forward declarations
static void loadConfigFromNVS();
static void saveConfigToNVS();


// LittleFS mount configuration
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfsStorage);
static struct fs_mount_t lfsMount = {
    .type = FS_LITTLEFS,
    .mnt_point = WEBROOT,
    .fs_data = &lfsStorage,
    .storage_dev = (void *)FIXED_PARTITION_ID(lfs_partition),
};

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
// NO POINTERS ALLOWED HERE
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

    rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
    if (rc) {
        LOG_ERR("Flash area open failed: %d", rc);
        return;
    }

    fs.flash_device = fa->fa_dev;
    fs.offset = fa->fa_off;
    struct flash_pages_info info;
    rc = flash_get_page_info_by_offs(fa->fa_dev, fs.offset, &info);
    if (rc) {
        LOG_ERR("Flash get page info failed: %d", rc);
        flash_area_close(fa);
        return;
    }

    fs.sector_size = info.size;
    fs.sector_count = fa->fa_size / info.size;

    rc = nvs_mount(&fs);
    if (rc) {
        LOG_ERR("NVS mount failed: %d", rc);
        flash_area_close(fa);
        return;
    }

    rc = nvs_read(&fs, CONFIG_NVS_ID, &appConfig, sizeof(appConfig));
    if (rc == sizeof(appConfig)) {
        LOG_INF("Configuration loaded from flash: Callsign=%s Grid=%s", 
                appConfig.callsign, appConfig.gridSquare);
    } else {
        LOG_INF("No valid configuration found in flash (rc=%d), using defaults", rc);
    }
    
    flash_area_close(fa);
}

// Save configuration to NVS
static void saveConfigToNVS() {
    int rc = nvs_write(&fs, CONFIG_NVS_ID, &appConfig, sizeof(appConfig));
    if (rc < 0) {
        LOG_ERR("Failed to save config to NVS: %d", rc);
    } else {
        LOG_INF("Configuration saved to flash (%d bytes)", rc);
    }
}

// API handler: GET /api/config
static void handleAPIConfigGet(int clientSock) {
    char buf[2560]; 
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos,
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
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"name\":\"%s\",\"freqHz\":%u,\"enabled\":%s}%s",
            bands.metadata[i].name, bands.metadata[i].hz,
            appConfig.bandEnabled[i] ? "true" : "false",
            (i < 9) ? "," : ""
        );
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

    sendJSON(clientSock, buf);
}

// Simple JSON field extractor (helper for PUT handler)
static bool getJSONString(const char* json, const char* key, char* out, size_t maxLen) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char* ptr = strstr(json, searchKey);
    if (!ptr) return false;
    
    ptr += strlen(searchKey);
    // Skip optional whitespace and colon
    while (*ptr && (*ptr == ' ' || *ptr == ':')) ptr++;
    // Must start with quote
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
    // Skip optional whitespace and colon
    while (*ptr && (*ptr == ' ' || *ptr == ':')) ptr++;
    
    *out = atoi(ptr);
    return true;
}

// API handler: PUT /api/config
static void handleAPIConfigPut(int clientSock, const char* body) {
    LOG_INF("Saving configuration update. Body len: %zu", strlen(body));
    
    // Log body for debugging (safely)
    if (strlen(body) > 0) {
        char dbgBody[64];
        snprintf(dbgBody, sizeof(dbgBody), "%.60s", body);
        LOG_INF("Body start: %s...", dbgBody);
    }

    // Extract basic fields
    bool csFound = getJSONString(body, "callsign", appConfig.callsign, sizeof(appConfig.callsign));
    bool gsFound = getJSONString(body, "gridSquare", appConfig.gridSquare, sizeof(appConfig.gridSquare));
    getJSONInt(body, "powerDbm", &appConfig.powerDbm);
    getJSONString(body, "mode", appConfig.mode, sizeof(appConfig.mode));
    getJSONInt(body, "slotIntervalMin", &appConfig.slotIntervalMin);
    getJSONString(body, "bandList", appConfig.bandList, sizeof(appConfig.bandList));

    // Handle band enables (crude but effective)
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

    // Save updated config to flash
    saveConfigToNVS();
    
    LOG_INF("Config updated: Callsign=%s (%s) Grid=%s (%s)", 
            appConfig.callsign, csFound ? "found" : "not found",
            appConfig.gridSquare, gsFound ? "found" : "not found");

    sendJSON(clientSock, "{\"status\":\"ok\"}");
}

// API handler: POST /api/tx/trigger
static void handleAPITXTrigger(int clientSock) {
    // TODO: Actually trigger transmission via FPGA::instance()
    LOG_INF("Manual TX trigger requested");
    sendJSON(clientSock, "{\"message\":\"TX triggered (stub)\"}");
}

// API handler: GET /api/files?path=/
static void handleAPIFilesList(int clientSock) {
    if (!littlefsMounted) {
        sendJSON(clientSock, "{\"files\":[]}");
        return;
    }

    // List files in /lfs root (flat, no directories)
    char buf[1024];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"files\":[");

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, WEBROOT) == 0) {
        struct fs_dirent entry;
        bool first = true;
        while (fs_readdir(&dir, &entry) == 0 && entry.name[0]) {
            if (entry.type == FS_DIR_ENTRY_DIR) continue;  // Skip directories
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
    snprintf(fullPath, sizeof(fullPath), "%s/%s", WEBROOT, filename);

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

    // Send header
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

    // Send file data
    uint8_t fileBuf[512];
    ssize_t bytesRead;
    while ((bytesRead = fs_read(&file, fileBuf, sizeof(fileBuf))) > 0) {
        zsock_send(clientSock, fileBuf, bytesRead, 0);
    }

    fs_close(&file);
}

// API handler: PUT /api/files/{filename}
static void handleAPIFilePut(int clientSock, const char* filename, const char* body, size_t bodyLen) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", WEBROOT, filename);

    struct fs_file_t file;
    fs_file_t_init(&file);

    if (fs_open(&file, full_path, FS_O_CREATE | FS_O_WRITE) < 0) {
        sendResponse(clientSock, 500, "text/plain", "Create Error", 12);
        return;
    }

    fs_write(&file, body, bodyLen);
    fs_close(&file);

    LOG_INF("File written: %s (%zu bytes)", filename, bodyLen);
    sendJSON(clientSock, "{\"status\":\"ok\"}");
}

// API handler: DELETE /api/files/{filename}
static void handleAPIFileDelete(int clientSock, const char* filename) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", WEBROOT, filename);

    if (fs_unlink(full_path) < 0) {
        sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        return;
    }

    LOG_INF("File deleted: %s", filename);
    sendJSON(clientSock, "{\"status\":\"ok\"}");
}

// Fallback page when LittleFS has no files
static const char* fallback_html =
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
    "<p>Flash the LittleFS image with web files to enable full UI.</p>"
    "</div>"
    "<h2>API Endpoints</h2>"
    "<ul>"
    "<li><a href=\"/api/status\">/api/status</a> - System status</li>"
    "<li><a href=\"/api/config\">/api/config</a> - Configuration</li>"
    "</ul>"
    "</body></html>";

// Serve static files from LittleFS
static void handleStatic(int clientSock, const char* path) {
    if (!littlefsMounted) {
        sendResponse(clientSock, 200, "text/html", fallback_html, strlen(fallback_html));
        return;
    }

    // Build full path, default to index.html
    char fullPath[256];
    if (strcmp(path, "/") == 0) {
        snprintf(fullPath, sizeof(fullPath), "%s/index.html", WEBROOT);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s%s", WEBROOT, path);
    }

    // Try to open file
    struct fs_file_t file;
    fs_file_t_init(&file);

    int ret = fs_open(&file, fullPath, FS_O_READ);
    if (ret < 0) {
        LOG_WRN("File not found: %s", fullPath);
        sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        return;
    }

    // Get file size
    struct fs_dirent stat;
    ret = fs_stat(fullPath, &stat);
    if (ret < 0) {
        fs_close(&file);
        sendResponse(clientSock, 500, "text/plain", "Stat Error", 10);
        return;
    }

    // Send header
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

    // Send header - ensure all bytes sent
    ssize_t sent = 0;
    while (sent < headerLen) {
        ssize_t n = zsock_send(clientSock, header + sent, headerLen - sent, 0);
        if (n <= 0) {
            LOG_ERR("Failed to send header");
            fs_close(&file);
            return;
        }
        sent += n;
    }

    // Send file in chunks - ensure all bytes sent
    uint8_t fileBuf[1024];
    ssize_t bytesRead;
    size_t totalSent = 0;
    while ((bytesRead = fs_read(&file, fileBuf, sizeof(fileBuf))) > 0) {
        ssize_t chunkSent = 0;
        while (chunkSent < bytesRead) {
            ssize_t n = zsock_send(clientSock, fileBuf + chunkSent,
                                   bytesRead - chunkSent, 0);
            if (n <= 0) {
                LOG_ERR("Failed to send file data at offset %zu", totalSent);
                fs_close(&file);
                return;
            }
            chunkSent += n;
        }
        totalSent += chunkSent;
    }

    fs_close(&file);
    LOG_DBG("Sent %zu bytes for %s", totalSent, fullPath);
}

// Find HTTP body start (after \r\n\r\n or \n\n)
static const char* findBody(const char* request) {
    const char* body = strstr(request, "\r\n\r\n");
    if (body) return body + 4;
    body = strstr(request, "\n\n");
    if (body) return body + 2;
    return nullptr;
}

// Parse HTTP request and route to handlers
static void handleRequest(int clientSock, const char* request, size_t requestLen) {
    // Parse method and path (path limited to 128 chars, fullPath needs room for WEBROOT prefix)
    char method[8] = {0};
    char path[128] = {0};

    if (sscanf(request, "%7s %127s", method, path) != 2) {
        sendResponse(clientSock, 400, "text/plain", "Bad Request", 11);
        return;
    }

    LOG_DBG("HTTP %s %s", method, path);

    // Route request
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/status") == 0) {
            handleAPIStatus(clientSock);
        } else if (strcmp(path, "/api/version") == 0) {
            handleAPIVersion(clientSock);
        } else if (strcmp(path, "/api/config") == 0) {
            handleAPIConfigGet(clientSock);
        } else if (strncmp(path, "/api/files", 10) == 0) {
            // Check if it's a file request or list request
            if (path[10] == '?' || path[10] == '\0') {
                // /api/files or /api/files?path=/
                handleAPIFilesList(clientSock);
            } else if (path[10] == '/') {
                // /api/files/{filename}
                handleAPIFileGet(clientSock, path + 11);
            } else {
                sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
            }
        } else {
            handleStatic(clientSock, path);
        }
    } else if (strcmp(method, "PUT") == 0) {
        if (strcmp(path, "/api/config") == 0) {
            const char* body = findBody(request);
            handleAPIConfigPut(clientSock, body ? body : "");
        } else if (strncmp(path, "/api/files/", 11) == 0) {
            const char* body = findBody(request);
            size_t bodyLen = body ? (requestLen - (body - request)) : 0;
            handleAPIFilePut(clientSock, path + 11, body ? body : "", bodyLen);
        } else {
            sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/tx/trigger") == 0) {
            handleAPITXTrigger(clientSock);
        } else {
            sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        if (strncmp(path, "/api/files/", 11) == 0) {
            handleAPIFileDelete(clientSock, path + 11);
        } else {
            sendResponse(clientSock, 404, "text/plain", "Not Found", 9);
        }
    } else if (strcmp(method, "OPTIONS") == 0) {
        // CORS preflight - allow all methods
        char header[256];
        int headerLen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n");
        zsock_send(clientSock, header, headerLen, 0);
    } else {
        sendResponse(clientSock, 405, "text/plain", "Method Not Allowed", 18);
    }
}

// Server thread function
static void serverThreadFn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("HTTP server thread started, waiting for connections on port %d", HTTP_PORT);

    while (serverRunning) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        int clientSock = zsock_accept(serverSock, (struct sockaddr*)&clientAddr,
                                       &clientAddrLen);
        if (clientSock < 0) {
            if (serverRunning) {
                LOG_ERR("accept() failed: %d", errno);
            }
            continue;
        }

        // Log client connection
        char clientIP[16];
        net_addr_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
        LOG_DBG("HTTP connection from %s", clientIP);

        // Set receive timeout (shorter to handle concurrent requests faster)
        struct zsock_timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        zsock_setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Read request
        int totalLen = 0;
        int ret = zsock_recv(clientSock, reqBuf, sizeof(reqBuf) - 1, 0);
        if (ret > 0) {
            totalLen = ret;
            reqBuf[totalLen] = '\0';

            // Check if we need to read more (for PUT/POST with body)
            if (strncmp(reqBuf, "PUT", 3) == 0 || strncmp(reqBuf, "POST", 4) == 0) {
                const char* clP = strstr(reqBuf, "Content-Length:");
                if (clP) {
                    int contentLen = atoi(clP + 15);
                    const char* bodyStart = findBody(reqBuf);
                    int curBodyLen = 0;
                    if (bodyStart) {
                        curBodyLen = totalLen - (bodyStart - reqBuf);
                    }

                    LOG_INF("Expect body: %d bytes, already have: %d", contentLen, curBodyLen);

                    while (curBodyLen < contentLen && totalLen < (int)sizeof(reqBuf) - 1) {
                        ret = zsock_recv(clientSock, reqBuf + totalLen,
                                         sizeof(reqBuf) - 1 - totalLen, 0);
                        if (ret <= 0) break;
                        totalLen += ret;
                        curBodyLen += ret;
                        reqBuf[totalLen] = '\0';
                    }
                }
            }

            LOG_DBG("Received %d bytes total", totalLen);
            handleRequest(clientSock, reqBuf, totalLen);
        } else {
            LOG_WRN("HTTP recv failed or empty: %d (errno=%d)", ret, errno);
        }

        zsock_close(clientSock);
    }

    LOG_INF("HTTP server thread exiting");
}

int WebServer::mountFilesystem() {
    if (littlefsMounted) {
        return 0;  // Already mounted
    }

    LOG_INF("Mounting LittleFS...");
    int ret = fs_mount(&lfsMount);
    if (ret < 0) {
        LOG_WRN("LittleFS mount failed: %d (will use fallback page)", ret);
        littlefsMounted = false;
        return ret;
    }

    LOG_INF("LittleFS mounted at %s", WEBROOT);
    littlefsMounted = true;

    // List files for debugging
    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, WEBROOT) == 0) {
        struct fs_dirent entry;
        while (fs_readdir(&dir, &entry) == 0 && entry.name[0]) {
            LOG_INF("  %s (%zu bytes)", entry.name, (size_t)entry.size);
        }
        fs_closedir(&dir);
    }

    return 0;
}

int WebServer::init() {
    LOG_INF("Initializing web server");

    // Mount filesystem if not already done
    mountFilesystem();

    // Load configuration from flash
    loadConfigFromNVS();

    return 0;
}

int WebServer::start(uint16_t port) {
    LOG_INF("Starting web server on port %d", port);

    // Create socket
    serverSock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }

    // Allow address reuse
    int opt = 1;
    zsock_setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to port
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (zsock_bind(serverSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERR("Failed to bind: %d", errno);
        zsock_close(serverSock);
        serverSock = -1;
        return -errno;
    }
    LOG_INF("Socket bound to port %d", port);

    // Listen with larger backlog for concurrent browser requests
    if (zsock_listen(serverSock, 8) < 0) {
        LOG_ERR("Failed to listen: %d", errno);
        zsock_close(serverSock);
        serverSock = -1;
        return -errno;
    }
    LOG_INF("Socket listening on port %d", port);

    // Start server thread
    serverRunning = true;
    LOG_INF("Creating HTTP server thread...");
    k_thread_create(&serverThread, serverStack, K_THREAD_STACK_SIZEOF(serverStack),
                    serverThreadFn, NULL, NULL, NULL,
                    K_PRIO_COOP(10), 0, K_NO_WAIT);
    LOG_INF("Thread created, setting name...");
    k_thread_name_set(&serverThread, "httpServer");
    LOG_INF("Thread name set");

    running = true;
    LOG_INF("Web server started on port %d", port);
    return 0;
}

void WebServer::stop() {
    if (!running) return;

    LOG_INF("Stopping web server");

    serverRunning = false;

    if (serverSock >= 0) {
        zsock_close(serverSock);
        serverSock = -1;
    }

    // Wait for thread to exit
    k_thread_join(&serverThread, K_SECONDS(5));

    running = false;
    LOG_INF("Web server stopped");
}

} // namespace wspr
