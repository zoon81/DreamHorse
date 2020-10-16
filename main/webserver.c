#include "webserver.h"
#include "motion_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>

static const char *TAG = "HTTP.C";
extern xQueueHandle motion_contorller_task_queue;

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    }
    else if (strcmp("/echo", req->uri) == 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

esp_err_t homepage_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reading file");
    FILE *fp = fopen("/spiffs/index.html", "rb");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    fseek(fp, 0L, SEEK_END);
    size_t file_size = ftell(fp);
    ESP_LOGI(TAG, "File size : %d", file_size);
    fseek(fp, 0L, SEEK_SET);
    char *buffer;
    buffer = malloc(file_size + 1);
    if (buffer == NULL)
    {
        ESP_LOGI(TAG, "Failed to allocate memory fur file buffer");
        return ESP_FAIL;
    }
    fread(buffer, 1, file_size, fp);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)buffer, file_size);
    fclose(fp);
    free(buffer);
    return ESP_OK;
}

esp_err_t motion_handler(httpd_req_t *req){
    char *buf;
    char direction[30];
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "direction", direction, sizeof(direction)) == ESP_OK)
            {
                ESP_LOGI(TAG, "direction URL query value => |%s| len: %d", direction,strlen(direction));
                if(!strcmp("forward",direction)){
                    // Go forward
                    uint8_t ui_task_cmd = QUEUE_REQUEST_FOWARD;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("backward",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_BACKWARD;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("left",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_LEFT;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("rigth",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_RIGTH;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("stop",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_STOP;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("h_up",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_HEAD_UP;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("h_left",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_HEAD_LEFT;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("h_middle",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_HEAD_LR_MIDDLE;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("h_rigth",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_HEAD_RIGTH;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                } else if(!strcmp("h_down",direction)){
                    uint8_t ui_task_cmd = QUEUE_REQUEST_HEAD_DOWN;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                }  else {
                    // STOP
                    uint8_t ui_task_cmd = QUEUE_REQUEST_STOP;
                    xQueueSend(motion_contorller_task_queue, &ui_task_cmd, NULL);
                }
            }
        }
        free(buf);
    }
    httpd_resp_send(req,HTTPD_204,strlen(HTTPD_204));
    return ESP_OK;
}


httpd_uri_t homepage = 
{
    .uri = "/homepage",
    .method = HTTP_GET,
    .handler = homepage_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = 0
};

httpd_uri_t motion = 
{
    .uri = "/motion\*",
    .method = HTTP_GET,
    .handler = motion_handler,
    .user_ctx = 0
};

httpd_handle_t start_webserver(void)
{

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &homepage);
        httpd_register_uri_handler(server, &motion);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}