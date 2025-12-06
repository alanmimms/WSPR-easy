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

#include <cstring>
#include <cstdio>

LOG_MODULE_REGISTER(webserver, LOG_LEVEL_INF);

namespace wspr {

#define HTTP_PORT 80
#define MAX_REQUEST_SIZE 1024
#define MAX_RESPONSE_SIZE 2048

static K_THREAD_STACK_DEFINE(server_stack, 4096);
static struct k_thread server_thread;

static int server_sock = -1;
static bool server_running = false;

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

// API handler: GET /api/config
static void handle_api_config(int client_sock) {
    // TODO: Load from NVS
    const char* config_json =
        "{"
        "\"callsign\":\"N0CALL\","
        "\"grid\":\"AA00\","
        "\"power\":10,"
        "\"bands\":[20,40],"
        "\"schedule\":{"
            "\"enabled\":false,"
            "\"interval\":10"
        "}"
        "}";

    send_json(client_sock, config_json);
}

// Serve static placeholder page
static void handle_static(int client_sock, const char* path) {
    const char* html =
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
        "<p>Upload web files to LittleFS to enable full UI.</p>"
        "</div>"
        "<h2>API Endpoints</h2>"
        "<ul>"
        "<li><a href=\"/api/status\">/api/status</a> - System status</li>"
        "<li><a href=\"/api/config\">/api/config</a> - Configuration</li>"
        "</ul>"
        "</body></html>";

    send_response(client_sock, 200, "text/html", html, strlen(html));
}

// Parse HTTP request and route to handlers
static void handle_request(int client_sock, const char* request) {
    // Parse method and path
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
        } else if (strcmp(path, "/api/config") == 0) {
            handle_api_config(client_sock);
        } else {
            handle_static(client_sock, path);
        }
    } else if (strcmp(method, "OPTIONS") == 0) {
        // CORS preflight
        send_response(client_sock, 200, "text/plain", "", 0);
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

    LOG_INF("HTTP server thread started");

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

        // Set receive timeout
        struct zsock_timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        zsock_setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Read request
        int len = zsock_recv(client_sock, request_buf, sizeof(request_buf) - 1, 0);
        if (len > 0) {
            request_buf[len] = '\0';
            handle_request(client_sock, request_buf);
        } else {
            LOG_WRN("HTTP recv failed or empty: %d (errno=%d)", len, errno);
        }

        zsock_close(client_sock);
    }

    LOG_INF("HTTP server thread exiting");
}

WebServer& WebServer::instance() {
    static WebServer inst;
    return inst;
}

int WebServer::init() {
    LOG_INF("Initializing web server");
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

    // Listen
    if (zsock_listen(server_sock, 4) < 0) {
        LOG_ERR("Failed to listen: %d", errno);
        zsock_close(server_sock);
        server_sock = -1;
        return -errno;
    }

    // Start server thread
    server_running = true;
    k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack),
                    server_thread_fn, NULL, NULL, NULL,
                    K_PRIO_COOP(10), 0, K_NO_WAIT);
    k_thread_name_set(&server_thread, "http_server");

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
