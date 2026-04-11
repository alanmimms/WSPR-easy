/*
 * Captive Portal DNS Server Implementation
 * Responds to all DNS A record queries with the AP IP
 */

#include "captiveDNS.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>

#include <cstring>

LOG_MODULE_REGISTER(captiveDNS, LOG_LEVEL_DBG);

namespace wspr {

#define DNS_PORT 53
#define DNS_BUF_SIZE 512

// Extract domain name from DNS query for logging
static void extractDNSName(const uint8_t* qname, char* out, size_t outLen) {
    size_t pos = 0;
    const uint8_t* ptr = qname;

    while (*ptr != 0 && pos < outLen - 1) {
        uint8_t len = *ptr++;
        if (pos > 0 && pos < outLen - 1) {
            out[pos++] = '.';
        }
        while (len-- > 0 && pos < outLen - 1) {
            out[pos++] = *ptr++;
        }
    }
    out[pos] = '\0';
}

static K_THREAD_STACK_DEFINE(dnsStack, 2048);
static struct k_thread dnsThread;

static int dnsSock = -1;
static bool dnsRunning = false;
static uint32_t dnsRedirectIP = 0;

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
static int buildDNSResponse(uint8_t* request, int reqLen,
                               uint8_t* response, int maxLen) {
    if (reqLen < (int)sizeof(dns_header) + 5) {
        return -1;
    }

    // Copy request header
    struct dns_header* req_hdr = (struct dns_header*)request;
    struct dns_header* resp_hdr = (struct dns_header*)response;

    memcpy(response, request, reqLen);

    // Modify header for response
    resp_hdr->flags = htons(0x8180);  // QR=1, AA=1, RD=1, RA=1
    resp_hdr->ancount = req_hdr->qdcount;  // Answer each question
    resp_hdr->nscount = 0;
    resp_hdr->arcount = 0;

    int offset = reqLen;

    // Parse question section to find where it ends
    int qdcount = ntohs(req_hdr->qdcount);
    uint8_t* qptr = request + sizeof(dns_header);

    for (int q = 0; q < qdcount && offset < maxLen - 16; q++) {
        // Skip over the QNAME
        while (*qptr != 0 && qptr < request + reqLen) {
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
        response[offset++] = (dnsRedirectIP >> 0) & 0xFF;
        response[offset++] = (dnsRedirectIP >> 8) & 0xFF;
        response[offset++] = (dnsRedirectIP >> 16) & 0xFF;
        response[offset++] = (dnsRedirectIP >> 24) & 0xFF;
    }

    return offset;
}

static void dnsThreadFn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint8_t rxBuf[DNS_BUF_SIZE];
    uint8_t txBuf[DNS_BUF_SIZE];

    LOG_INF("Captive DNS server thread started");

    while (dnsRunning) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        int len = zsock_recvfrom(dnsSock, rxBuf, sizeof(rxBuf), 0,
                                  (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (len < 0) {
            if (dnsRunning) {
                LOG_ERR("DNS recvfrom error: %d", errno);
            }
            continue;
        }

        // Log incoming query
        char clientIP[16];
        net_addr_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

        if (len >= (int)sizeof(dns_header) + 1) {
            char qname[64];
            extractDNSName(rxBuf + sizeof(dns_header), qname, sizeof(qname));
            LOG_INF("DNS query from %s: %s (%d bytes)", clientIP, qname, len);
        } else {
            LOG_INF("DNS query from %s (%d bytes, too short)", clientIP, len);
        }

        // Build response
        int respLen = buildDNSResponse(rxBuf, len, txBuf, sizeof(txBuf));
        if (respLen > 0) {
            LOG_DBG("DNS response: %d bytes -> 192.168.4.1", respLen);
            int sent = zsock_sendto(dnsSock, txBuf, respLen, 0,
                         (struct sockaddr*)&clientAddr, clientAddrLen);
            if (sent < 0) {
                LOG_ERR("DNS sendto failed: %d", errno);
            }
        } else {
            LOG_WRN("DNS failed to build response");
        }
    }

    LOG_INF("Captive DNS server thread exiting");
}

CaptiveDNS& CaptiveDNS::instance() {
    static CaptiveDNS inst;
    return inst;
}

int CaptiveDNS::start(const char* redirectIP) {
    if (running) {
        LOG_WRN("Captive DNS already running");
        return 0;
    }

    LOG_INF("Starting captive DNS server, redirecting to %s", redirectIP);

    // Parse IP address
    struct in_addr addr;
    if (net_addr_pton(AF_INET, redirectIP, &addr) < 0) {
        LOG_ERR("Invalid redirect IP: %s", redirectIP);
        return -EINVAL;
    }
    dnsRedirectIP = addr.s_addr;

    // Create UDP socket
    dnsSock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dnsSock < 0) {
        LOG_ERR("Failed to create DNS socket: %d", errno);
        return -errno;
    }

    // Bind to DNS port
    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DNS_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    if (zsock_bind(dnsSock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind DNS socket: %d", errno);
        zsock_close(dnsSock);
        dnsSock = -1;
        return -errno;
    }

    // Start DNS thread
    dnsRunning = true;
    k_thread_create(&dnsThread, dnsStack, K_THREAD_STACK_SIZEOF(dnsStack),
                    dnsThreadFn, NULL, NULL, NULL,
                    K_PRIO_COOP(11), 0, K_NO_WAIT);
    k_thread_name_set(&dnsThread, "captiveDNS");

    running = true;
    LOG_INF("Captive DNS server started on port %d", DNS_PORT);
    return 0;
}

void CaptiveDNS::stop() {
    if (!running) {
        return;
    }

    LOG_INF("Stopping captive DNS server");

    dnsRunning = false;

    if (dnsSock >= 0) {
        zsock_close(dnsSock);
        dnsSock = -1;
    }

    k_thread_join(&dnsThread, K_SECONDS(2));

    running = false;
    LOG_INF("Captive DNS server stopped");
}

} // namespace wspr
