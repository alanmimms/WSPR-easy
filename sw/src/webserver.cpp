/*
 * HTTP Web Server Implementation for WSPR-ease
 * Simple socket-based server for maximum portability
 */

#include "webserver.hpp"
#include "wifi_manager.hpp"
#include "gnss.hpp"
#include "fpga.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

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
#define MAX_REQUEST_SIZE 1024
#define MAX_RESPONSE_SIZE 2048
#define WEBROOT "/lfs"

static K_THREAD_STACK_DEFINE(server_stack, 8192);
static struct k_thread server_thread;

static int server_sock = -1;
static bool server_running = false;
static bool littlefs_mounted = false;


// LittleFS mount configuration
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_storage);
static struct fs_mount_t lfs_mount = {
    .type = FS_LITTLEFS,
    .mnt_point = WEBROOT,
    .fs_data = &lfs_storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(littlefs_partition),
};

// Get content type from file extension
static const char* get_content_type(const char* path) {
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
static void send_response(int client_sock, int status_code,
                         const char* content_type,
                         const char* body, size_t body_len) {
    char header[256];
    const char* status_text = (status_code == 200) ? "OK" :
                              (status_code == 404) ? "Not Found" :
                              (status_code == 500) ? "Internal Server Error" : "Error";

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);

    zsock_send(client_sock, header, header_len, 0);
    if (body && body_len > 0) {
        zsock_send(client_sock, body, body_len, 0);
    }
}

static void send_json(int client_sock, const char* json) {
    send_response(client_sock, 200, "application/json", json, strlen(json));
}

// API handler: GET /api/status
static void handle_api_status(int client_sock) {
    auto& wifi = WifiManager::instance();
    auto& gnss = Gnss::instance();
    auto& fpga = Fpga::instance();

    char buf[512];
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
            "\"time\":\"%s\""
        "},"
        "\"fpga\":{"
            "\"initialized\":%s,"
            "\"transmitting\":%s,"
            "\"frequency\":%u"
        "},"
        "\"uptime\":%lld"
        "}",
        wifi.is_connected() ? "true" : "false",
        wifi.ssid(),
        wifi.ip_address(),
        wifi.rssi(),
        gnss.has_fix() ? "true" : "false",
        gnss.satellites(),
        gnss.latitude(),
        gnss.longitude(),
        gnss.time_string(),
        fpga.is_initialized() ? "true" : "false",
        fpga.is_transmitting() ? "true" : "false",
        fpga.frequency(),
        k_uptime_get() / 1000
    );

    send_json(client_sock, buf);
}

// API handler: GET /api/version
static void handle_api_version(int client_sock) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"version\":\"%s\"}", APP_VERSION);
    send_json(client_sock, buf);
}

// API handler: GET /api/config
static void handle_api_config_get(int client_sock) {
    // TODO: Load from NVS - for now return a complete config matching web UI expectations
    const char* config_json =
        "{"
        "\"callsign\":\"N0CALL\","
        "\"gridSquare\":\"AA00\","
        "\"powerDbm\":23,"
        "\"mode\":\"round-robin\","
        "\"slotIntervalMin\":10,"
        "\"bandList\":\"\","
        "\"bands\":["
            "{\"name\":\"80m\",\"freqHz\":3570100,\"enabled\":false},"
            "{\"name\":\"40m\",\"freqHz\":7040100,\"enabled\":true},"
            "{\"name\":\"30m\",\"freqHz\":10140200,\"enabled\":true},"
            "{\"name\":\"20m\",\"freqHz\":14097100,\"enabled\":true},"
            "{\"name\":\"17m\",\"freqHz\":18106100,\"enabled\":false},"
            "{\"name\":\"15m\",\"freqHz\":21096100,\"enabled\":false},"
            "{\"name\":\"12m\",\"freqHz\":24926100,\"enabled\":false},"
            "{\"name\":\"10m\",\"freqHz\":28126100,\"enabled\":false}"
        "]"
        "}";

    send_json(client_sock, config_json);
}

// API handler: PUT /api/config
static void handle_api_config_put(int client_sock, const char* body) {
    // TODO: Parse JSON and save to NVS
    LOG_INF("Config update received: %.100s...", body);
    send_json(client_sock, "{\"status\":\"ok\"}");
}

// API handler: POST /api/tx/trigger
static void handle_api_tx_trigger(int client_sock) {
    // TODO: Actually trigger transmission via Fpga::instance()
    LOG_INF("Manual TX trigger requested");
    send_json(client_sock, "{\"message\":\"TX triggered (stub)\"}");
}

// API handler: GET /api/files?path=/
static void handle_api_files_list(int client_sock) {
    if (!littlefs_mounted) {
        send_json(client_sock, "{\"files\":[]}");
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
    send_json(client_sock, buf);
}

// API handler: GET /api/files/{filename}
static void handle_api_file_get(int client_sock, const char* filename) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", WEBROOT, filename);

    struct fs_file_t file;
    fs_file_t_init(&file);

    if (fs_open(&file, full_path, FS_O_READ) < 0) {
        send_response(client_sock, 404, "text/plain", "Not Found", 9);
        return;
    }

    struct fs_dirent stat;
    if (fs_stat(full_path, &stat) < 0) {
        fs_close(&file);
        send_response(client_sock, 500, "text/plain", "Stat Error", 10);
        return;
    }

    // Send header
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %zu\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Connection: close\r\n"
        "\r\n",
        (size_t)stat.size, filename);

    zsock_send(client_sock, header, header_len, 0);

    // Send file data
    uint8_t file_buf[512];
    ssize_t bytes_read;
    while ((bytes_read = fs_read(&file, file_buf, sizeof(file_buf))) > 0) {
        zsock_send(client_sock, file_buf, bytes_read, 0);
    }

    fs_close(&file);
}

// API handler: PUT /api/files/{filename}
static void handle_api_file_put(int client_sock, const char* filename, const char* body, size_t body_len) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", WEBROOT, filename);

    struct fs_file_t file;
    fs_file_t_init(&file);

    if (fs_open(&file, full_path, FS_O_CREATE | FS_O_WRITE) < 0) {
        send_response(client_sock, 500, "text/plain", "Create Error", 12);
        return;
    }

    fs_write(&file, body, body_len);
    fs_close(&file);

    LOG_INF("File written: %s (%zu bytes)", filename, body_len);
    send_json(client_sock, "{\"status\":\"ok\"}");
}

// API handler: DELETE /api/files/{filename}
static void handle_api_file_delete(int client_sock, const char* filename) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", WEBROOT, filename);

    if (fs_unlink(full_path) < 0) {
        send_response(client_sock, 404, "text/plain", "Not Found", 9);
        return;
    }

    LOG_INF("File deleted: %s", filename);
    send_json(client_sock, "{\"status\":\"ok\"}");
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
static void handle_static(int client_sock, const char* path) {
    if (!littlefs_mounted) {
        send_response(client_sock, 200, "text/html", fallback_html, strlen(fallback_html));
        return;
    }

    // Build full path, default to index.html
    char full_path[256];
    if (strcmp(path, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "%s/index.html", WEBROOT);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s", WEBROOT, path);
    }

    // Try to open file
    struct fs_file_t file;
    fs_file_t_init(&file);

    int ret = fs_open(&file, full_path, FS_O_READ);
    if (ret < 0) {
        LOG_WRN("File not found: %s", full_path);
        send_response(client_sock, 404, "text/plain", "Not Found", 9);
        return;
    }

    // Get file size
    struct fs_dirent stat;
    ret = fs_stat(full_path, &stat);
    if (ret < 0) {
        fs_close(&file);
        send_response(client_sock, 500, "text/plain", "Stat Error", 10);
        return;
    }

    // Send header
    const char* content_type = get_content_type(full_path);
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: max-age=3600\r\n"
        "\r\n",
        content_type, (size_t)stat.size);

    // Send header - ensure all bytes sent
    ssize_t sent = 0;
    while (sent < header_len) {
        ssize_t n = zsock_send(client_sock, header + sent, header_len - sent, 0);
        if (n <= 0) {
            LOG_ERR("Failed to send header");
            fs_close(&file);
            return;
        }
        sent += n;
    }

    // Send file in chunks - ensure all bytes sent
    uint8_t file_buf[1024];
    ssize_t bytes_read;
    size_t total_sent = 0;
    while ((bytes_read = fs_read(&file, file_buf, sizeof(file_buf))) > 0) {
        ssize_t chunk_sent = 0;
        while (chunk_sent < bytes_read) {
            ssize_t n = zsock_send(client_sock, file_buf + chunk_sent,
                                   bytes_read - chunk_sent, 0);
            if (n <= 0) {
                LOG_ERR("Failed to send file data at offset %zu", total_sent);
                fs_close(&file);
                return;
            }
            chunk_sent += n;
        }
        total_sent += chunk_sent;
    }

    fs_close(&file);
    LOG_DBG("Sent %zu bytes for %s", total_sent, full_path);
}

// Find HTTP body start (after \r\n\r\n)
static const char* find_body(const char* request) {
    const char* body = strstr(request, "\r\n\r\n");
    return body ? body + 4 : nullptr;
}

// Parse HTTP request and route to handlers
static void handle_request(int client_sock, const char* request, size_t request_len) {
    // Parse method and path (path limited to 128 chars, full_path needs room for WEBROOT prefix)
    char method[8] = {0};
    char path[128] = {0};

    if (sscanf(request, "%7s %127s", method, path) != 2) {
        send_response(client_sock, 400, "text/plain", "Bad Request", 11);
        return;
    }

    LOG_INF("HTTP %s %s", method, path);

    // Route request
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/status") == 0) {
            handle_api_status(client_sock);
        } else if (strcmp(path, "/api/version") == 0) {
            handle_api_version(client_sock);
        } else if (strcmp(path, "/api/config") == 0) {
            handle_api_config_get(client_sock);
        } else if (strncmp(path, "/api/files", 10) == 0) {
            // Check if it's a file request or list request
            if (path[10] == '?' || path[10] == '\0') {
                // /api/files or /api/files?path=/
                handle_api_files_list(client_sock);
            } else if (path[10] == '/') {
                // /api/files/{filename}
                handle_api_file_get(client_sock, path + 11);
            } else {
                send_response(client_sock, 404, "text/plain", "Not Found", 9);
            }
        } else {
            handle_static(client_sock, path);
        }
    } else if (strcmp(method, "PUT") == 0) {
        if (strcmp(path, "/api/config") == 0) {
            const char* body = find_body(request);
            handle_api_config_put(client_sock, body ? body : "");
        } else if (strncmp(path, "/api/files/", 11) == 0) {
            const char* body = find_body(request);
            size_t body_len = body ? (request_len - (body - request)) : 0;
            handle_api_file_put(client_sock, path + 11, body ? body : "", body_len);
        } else {
            send_response(client_sock, 404, "text/plain", "Not Found", 9);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/tx/trigger") == 0) {
            handle_api_tx_trigger(client_sock);
        } else {
            send_response(client_sock, 404, "text/plain", "Not Found", 9);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        if (strncmp(path, "/api/files/", 11) == 0) {
            handle_api_file_delete(client_sock, path + 11);
        } else {
            send_response(client_sock, 404, "text/plain", "Not Found", 9);
        }
    } else if (strcmp(method, "OPTIONS") == 0) {
        // CORS preflight - allow all methods
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n");
        zsock_send(client_sock, header, header_len, 0);
    } else {
        send_response(client_sock, 405, "text/plain", "Method Not Allowed", 18);
    }
}

// Server thread function
static void server_thread_fn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    char request_buf[MAX_REQUEST_SIZE];

    LOG_INF("HTTP server thread started, waiting for connections on sock %d", server_sock);

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_sock = zsock_accept(server_sock, (struct sockaddr*)&client_addr,
                                       &client_addr_len);
        if (client_sock < 0) {
            if (server_running) {
                LOG_ERR("accept() failed: %d", errno);
            }
            continue;
        }

        // Log client connection
        char client_ip[16];
        net_addr_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        LOG_INF("HTTP connection from %s", client_ip);

        // Set receive timeout (shorter to handle concurrent requests faster)
        struct zsock_timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        zsock_setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Read request
        int len = zsock_recv(client_sock, request_buf, sizeof(request_buf) - 1, 0);
        if (len > 0) {
            request_buf[len] = '\0';
            handle_request(client_sock, request_buf, len);
        } else {
            LOG_WRN("HTTP recv failed or empty: %d (errno=%d)", len, errno);
        }

        zsock_close(client_sock);
    }

    LOG_INF("HTTP server thread exiting");
}

int WebServer::mount_filesystem() {
    if (littlefs_mounted) {
        return 0;  // Already mounted
    }

    LOG_INF("Mounting LittleFS...");
    int ret = fs_mount(&lfs_mount);
    if (ret < 0) {
        LOG_WRN("LittleFS mount failed: %d (will use fallback page)", ret);
        littlefs_mounted = false;
        return ret;
    }

    LOG_INF("LittleFS mounted at %s", WEBROOT);
    littlefs_mounted = true;

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
    mount_filesystem();

    return 0;
}

int WebServer::start(uint16_t port) {
    LOG_INF("Starting web server on port %d", port);

    // Create socket
    server_sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }

    // Allow address reuse
    int opt = 1;
    zsock_setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to port
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (zsock_bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERR("Failed to bind: %d", errno);
        zsock_close(server_sock);
        server_sock = -1;
        return -errno;
    }
    LOG_INF("Socket bound to port %d", port);

    // Listen with larger backlog for concurrent browser requests
    if (zsock_listen(server_sock, 8) < 0) {
        LOG_ERR("Failed to listen: %d", errno);
        zsock_close(server_sock);
        server_sock = -1;
        return -errno;
    }
    LOG_INF("Socket listening on port %d", port);

    // Start server thread
    server_running = true;
    LOG_INF("Creating HTTP server thread...");
    k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack),
                    server_thread_fn, NULL, NULL, NULL,
                    K_PRIO_COOP(10), 0, K_NO_WAIT);
    LOG_INF("Thread created, setting name...");
    k_thread_name_set(&server_thread, "http_server");
    LOG_INF("Thread name set");

    running_ = true;
    LOG_INF("Web server started on port %d", port);
    return 0;
}

void WebServer::stop() {
    if (!running_) return;

    LOG_INF("Stopping web server");

    server_running = false;

    if (server_sock >= 0) {
        zsock_close(server_sock);
        server_sock = -1;
    }

    // Wait for thread to exit
    k_thread_join(&server_thread, K_SECONDS(5));

    running_ = false;
    LOG_INF("Web server stopped");
}

} // namespace wspr
