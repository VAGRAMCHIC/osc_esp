#include "http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "HTTP";
static float probe_multiplier = 11.0f;  // значение по умолчанию для делителя 11:1

void set_probe_multiplier(float multiplier) {
    probe_multiplier = multiplier;
    ESP_LOGI(TAG, "Probe multiplier set to %.3f", probe_multiplier);
}

float get_probe_multiplier(void) {
    return probe_multiplier;
}

// Обработчик POST /api/probe
static esp_err_t probe_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mult = cJSON_GetObjectItem(root, "multiplier");
    if (mult && cJSON_IsNumber(mult)) {
        set_probe_multiplier((float)mult->valuedouble);
        httpd_resp_set_type(req, "application/json");
        const char *resp = "{\"status\":\"ok\"}";
        httpd_resp_send(req, resp, strlen(resp));
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'multiplier' field");
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// Обработчик GET /api/probe (для чтения текущего коэффициента)
static esp_err_t probe_get_handler(httpd_req_t *req) {
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"multiplier\": %.3f}", get_probe_multiplier());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// Регистрация URI
static const httpd_uri_t post_probe = {
    .uri       = "/api/probe",
    .method    = HTTP_POST,
    .handler   = probe_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t get_probe = {
    .uri       = "/api/probe",
    .method    = HTTP_GET,
    .handler   = probe_get_handler,
    .user_ctx  = NULL
};

void http_server_start(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &post_probe);
        httpd_register_uri_handler(server, &get_probe);
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}
