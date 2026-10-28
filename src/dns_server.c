#include "dns_server.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "DNS_SERVER";
static TaskHandle_t s_dns_task = NULL;

#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;
#pragma pack(pop)

static void dns_server_task(void *pvParameters) {
    uint8_t buffer[256];
    struct sockaddr_in serv_addr, client_addr;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(53);

    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    // Set 2-second receive timeout so it never blocks forever
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Captive Portal DNS Server listening on port 53 (redirects to 192.168.4.1)");

    while (1) {
        socklen_t client_len = sizeof(client_addr);
        int len = recvfrom(sock, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < (int)sizeof(dns_header_t)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        dns_header_t *hdr = (dns_header_t *)buffer;
        hdr->flags = htons(0x8180); // Standard response, no error
        hdr->ancount = hdr->qdcount; // 1 answer

        // Scan past question domain name
        int idx = sizeof(dns_header_t);
        while (idx < len && buffer[idx] != 0) {
            idx += (buffer[idx] + 1);
        }
        idx += 5; // 0x00 term + 2 bytes QTYPE + 2 bytes QCLASS

        if (idx <= len && (idx + 16) <= (int)sizeof(buffer)) {
            // Write Type A Answer record pointing to 192.168.4.1
            buffer[idx++] = 0xc0; // Name pointer
            buffer[idx++] = 0x0c;
            buffer[idx++] = 0x00; // Type A
            buffer[idx++] = 0x01;
            buffer[idx++] = 0x00; // Class IN
            buffer[idx++] = 0x01;
            buffer[idx++] = 0x00; // TTL: 60 sec
            buffer[idx++] = 0x00;
            buffer[idx++] = 0x00;
            buffer[idx++] = 0x3c;
            buffer[idx++] = 0x00; // Data length: 4
            buffer[idx++] = 0x04;
            buffer[idx++] = 192;  // 192.168.4.1
            buffer[idx++] = 168;
            buffer[idx++] = 4;
            buffer[idx++] = 1;

            sendto(sock, buffer, idx, 0, (struct sockaddr *)&client_addr, client_len);
        }
    }
}

void dns_server_start(void) {
    if (s_dns_task == NULL) {
        xTaskCreate(dns_server_task, "DnsTask", 3072, NULL, 3, &s_dns_task);
    }
}
