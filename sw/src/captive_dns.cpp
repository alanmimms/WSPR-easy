/*
 * Captive Portal DNS Server Implementation
 * Responds to all DNS A record queries with the AP IP
 */

#include "captive_dns.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>

#include <cstring>

LOG_MODULE_REGISTER(captive_dns, LOG_LEVEL_DBG);

namespace wspr {

#define DNS_PORT 53
#define DNS_BUF_SIZE 512

// Extract domain name from DNS query for logging
static void extract_dns_name(const uint8_t* qname, char* out, size_t out_len) {
    size_t pos = 0;
    const uint8_t* ptr = qname;

    while (*ptr != 0 && pos < out_len - 1) {
        uint8_t len = *ptr++;
        if (pos > 0 && pos < out_len - 1) {
            out[pos++] = '.';
        }
        while (len-- > 0 && pos < out_len - 1) {
            out[pos++] = *ptr++;
        }
    }
    out[pos] = '\0';
}

static K_THREAD_STACK_DEFINE(dns_stack, 2048);
static struct k_thread dns_thread;

static int dns_sock = -1;
static bool dns_running = false;
static uint32_t dns_redirect_ip = 0;

// DNS header structure
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

// Build a DNS response that redirects all queries to our IP
static int build_dns_response(uint8_t* request, int req_len,
                               uint8_t* response, int max_len) {
    if (req_len < (int)sizeof(dns_header) + 5) {
        return -1;
    }

    // Copy request header
    struct dns_header* req_hdr = (struct dns_header*)request;
    struct dns_header* resp_hdr = (struct dns_header*)response;

    memcpy(response, request, req_len);

    // Modify header for response
    resp_hdr->flags = htons(0x8180);  // QR=1, AA=1, RD=1, RA=1
    resp_hdr->ancount = req_hdr->qdcount;  // Answer each question
    resp_hdr->nscount = 0;
    resp_hdr->arcount = 0;

    int offset = req_len;

    // Parse question section to find where it ends
    int qdcount = ntohs(req_hdr->qdcount);
    uint8_t* qptr = request + sizeof(dns_header);

    for (int q = 0; q < qdcount && offset < max_len - 16; q++) {
        // Skip the QNAME (pointer to question name)
        uint8_t* name_start = qptr;

        // Skip over the name
        while (*qptr != 0 && qptr < request + req_len) {
            qptr += (*qptr) + 1;
        }
        qptr++;  // Skip null terminator

        // Get QTYPE and QCLASS
        uint16_t qtype = ntohs(*(uint16_t*)qptr);
        qptr += 2;
        qptr += 2;  // Skip QCLASS

        // Only respond to A record queries (type 1)
        if (qtype != 1) {
            continue;
        }

        // Add answer: pointer to name + type + class + TTL + rdlength + IP
        // Name pointer (0xC0 0x0C = pointer to offset 12, the question name)
        response[offset++] = 0xC0;
        response[offset++] = 0x0C;

        // Type: A (1)
        response[offset++] = 0x00;
        response[offset++] = 0x01;

        // Class: IN (1)
        response[offset++] = 0x00;
        response[offset++] = 0x01;

        // TTL: 60 seconds
        response[offset++] = 0x00;
        response[offset++] = 0x00;
        response[offset++] = 0x00;
        response[offset++] = 0x3C;

        // RDLENGTH: 4 bytes (IPv4)
        response[offset++] = 0x00;
        response[offset++] = 0x04;

        // RDATA: IP address (already in network byte order)
        response[offset++] = (dns_redirect_ip >> 0) & 0xFF;
        response[offset++] = (dns_redirect_ip >> 8) & 0xFF;
        response[offset++] = (dns_redirect_ip >> 16) & 0xFF;
        response[offset++] = (dns_redirect_ip >> 24) & 0xFF;
    }

    return offset;
}

static void dns_thread_fn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint8_t rx_buf[DNS_BUF_SIZE];
    uint8_t tx_buf[DNS_BUF_SIZE];

    LOG_INF("Captive DNS server thread started");

    while (dns_running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int len = zsock_recvfrom(dns_sock, rx_buf, sizeof(rx_buf), 0,
                                  (struct sockaddr*)&client_addr, &client_addr_len);
        if (len < 0) {
            if (dns_running) {
                LOG_ERR("DNS recvfrom error: %d", errno);
            }
            continue;
        }

        // Log incoming query
        char client_ip[16];
        net_addr_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        if (len >= (int)sizeof(dns_header) + 1) {
            char qname[64];
            extract_dns_name(rx_buf + sizeof(dns_header), qname, sizeof(qname));
            LOG_INF("DNS query from %s: %s (%d bytes)", client_ip, qname, len);
        } else {
            LOG_INF("DNS query from %s (%d bytes, too short)", client_ip, len);
        }

        // Build response
        int resp_len = build_dns_response(rx_buf, len, tx_buf, sizeof(tx_buf));
        if (resp_len > 0) {
            LOG_DBG("DNS response: %d bytes -> 192.168.4.1", resp_len);
            int sent = zsock_sendto(dns_sock, tx_buf, resp_len, 0,
                         (struct sockaddr*)&client_addr, client_addr_len);
            if (sent < 0) {
                LOG_ERR("DNS sendto failed: %d", errno);
            }
        } else {
            LOG_WRN("DNS failed to build response");
        }
    }

    LOG_INF("Captive DNS server thread exiting");
}

CaptiveDns& CaptiveDns::instance() {
    static CaptiveDns inst;
    return inst;
}

int CaptiveDns::start(const char* redirect_ip) {
    if (running_) {
        LOG_WRN("Captive DNS already running");
        return 0;
    }

    LOG_INF("Starting captive DNS server, redirecting to %s", redirect_ip);

    // Parse IP address
    struct in_addr addr;
    if (net_addr_pton(AF_INET, redirect_ip, &addr) < 0) {
        LOG_ERR("Invalid redirect IP: %s", redirect_ip);
        return -EINVAL;
    }
    dns_redirect_ip = addr.s_addr;
    redirect_ip_ = addr.s_addr;

    // Create UDP socket
    dns_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_sock < 0) {
        LOG_ERR("Failed to create DNS socket: %d", errno);
        return -errno;
    }

    // Bind to DNS port
    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DNS_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    if (zsock_bind(dns_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind DNS socket: %d", errno);
        zsock_close(dns_sock);
        dns_sock = -1;
        return -errno;
    }

    // Start DNS thread
    dns_running = true;
    k_thread_create(&dns_thread, dns_stack, K_THREAD_STACK_SIZEOF(dns_stack),
                    dns_thread_fn, NULL, NULL, NULL,
                    K_PRIO_COOP(11), 0, K_NO_WAIT);
    k_thread_name_set(&dns_thread, "captive_dns");

    running_ = true;
    LOG_INF("Captive DNS server started on port %d", DNS_PORT);
    return 0;
}

void CaptiveDns::stop() {
    if (!running_) {
        return;
    }

    LOG_INF("Stopping captive DNS server");

    dns_running = false;

    if (dns_sock >= 0) {
        zsock_close(dns_sock);
        dns_sock = -1;
    }

    k_thread_join(&dns_thread, K_SECONDS(2));

    running_ = false;
    LOG_INF("Captive DNS server stopped");
}

} // namespace wspr
