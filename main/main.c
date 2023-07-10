#include <esp_system.h>
#include <nvs_flash.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_camera.h"

//for web
#include "connect_wifi.h"
#include "esp_http_server.h"


#include "esp_timer.h"



#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char *TAG = "esp32-cam-test";

static esp_err_t bsp_i2c_init();

#define BSP_CAMERA_XCLK (GPIO_NUM_1)
#define BSP_CAMERA_PCLK (GPIO_NUM_33)
#define BSP_CAMERA_VSYNC (GPIO_NUM_2)
#define BSP_CAMERA_HSYNC (GPIO_NUM_3)
#define BSP_CAMERA_D0 (GPIO_NUM_36) /*!< Labeled as: D2 */
#define BSP_CAMERA_D1 (GPIO_NUM_37) /*!< Labeled as: D3 */
#define BSP_CAMERA_D2 (GPIO_NUM_41) /*!< Labeled as: D4 */
#define BSP_CAMERA_D3 (GPIO_NUM_42) /*!< Labeled as: D5 */
#define BSP_CAMERA_D4 (GPIO_NUM_39) /*!< Labeled as: D6 */
#define BSP_CAMERA_D5 (GPIO_NUM_40) /*!< Labeled as: D7 */
#define BSP_CAMERA_D6 (GPIO_NUM_21) /*!< Labeled as: D8 */
#define BSP_CAMERA_D7 (GPIO_NUM_38) /*!< Labeled as: D9 */

#define BSP_I2C_SCL (GPIO_NUM_7)
#define BSP_I2C_SDA (GPIO_NUM_8)
#define BSP_I2C_NUM 1
#define CONFIG_BSP_I2C_CLK_SPEED_HZ 400000

#define BSP_CAMERA_DEFAULT_CONFIG               \
    {                                           \
        .pin_pwdn = GPIO_NUM_NC,                \
        .pin_reset = GPIO_NUM_NC,               \
        .pin_xclk = BSP_CAMERA_XCLK,            \
        .pin_sccb_sda = GPIO_NUM_NC,            \
        .pin_sccb_scl = GPIO_NUM_NC,            \
        .pin_d7 = BSP_CAMERA_D7,                \
        .pin_d6 = BSP_CAMERA_D6,                \
        .pin_d5 = BSP_CAMERA_D5,                \
        .pin_d4 = BSP_CAMERA_D4,                \
        .pin_d3 = BSP_CAMERA_D3,                \
        .pin_d2 = BSP_CAMERA_D2,                \
        .pin_d1 = BSP_CAMERA_D1,                \
        .pin_d0 = BSP_CAMERA_D0,                \
        .pin_vsync = BSP_CAMERA_VSYNC,          \
        .pin_href = BSP_CAMERA_HSYNC,           \
        .pin_pclk = BSP_CAMERA_PCLK,            \
        .xclk_freq_hz = 16000000,               \
        .ledc_timer = LEDC_TIMER_0,             \
        .ledc_channel = LEDC_CHANNEL_0,         \
        .pixel_format = PIXFORMAT_RGB565,       \
        .frame_size = FRAMESIZE_VGA,            \
        .jpeg_quality = 30,                     \
        .fb_count = 1,                          \
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,    \
        .sccb_i2c_port = BSP_I2C_NUM,           \
    }




    //ORIGINAL:
    /*
    #define BSP_CAMERA_DEFAULT_CONFIG         \
    {                                     \
        .pin_pwdn = GPIO_NUM_NC,          \
        .pin_reset = GPIO_NUM_NC,         \
        .pin_xclk = BSP_CAMERA_XCLK,      \
        .pin_sccb_sda = GPIO_NUM_NC,      \
        .pin_sccb_scl = GPIO_NUM_NC,      \
        .pin_d7 = BSP_CAMERA_D7,          \
        .pin_d6 = BSP_CAMERA_D6,          \
        .pin_d5 = BSP_CAMERA_D5,          \
        .pin_d4 = BSP_CAMERA_D4,          \
        .pin_d3 = BSP_CAMERA_D3,          \
        .pin_d2 = BSP_CAMERA_D2,          \
        .pin_d1 = BSP_CAMERA_D1,          \
        .pin_d0 = BSP_CAMERA_D0,          \
        .pin_vsync = BSP_CAMERA_VSYNC,    \
        .pin_href = BSP_CAMERA_HSYNC,     \
        .pin_pclk = BSP_CAMERA_PCLK,      \
        .xclk_freq_hz = 16000000,         \
        .ledc_timer = LEDC_TIMER_0,       \
        .ledc_channel = LEDC_CHANNEL_0,   \
        .pixel_format = PIXFORMAT_RGB565, \
        .frame_size = FRAMESIZE_QVGA,     \
        .jpeg_quality = 12,               \
        .fb_count = 2,                    \
        .sccb_i2c_port = BSP_I2C_NUM,     \
    }
    
    */

static esp_err_t init_camera2()
{

    bsp_i2c_init();
    const camera_config_t camera_config = BSP_CAMERA_DEFAULT_CONFIG;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }
    return ESP_OK;
}






esp_err_t jpg_stream_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t * _jpg_buf;
    char * part_buf[64];
    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        if(fb->format != PIXFORMAT_JPEG){
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            if(!jpeg_converted){
                ESP_LOGE(TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(fb->format != PIXFORMAT_JPEG){
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);
        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        ESP_LOGI(TAG, "MJPG: %luKB %lums (%.1ffps)",
            (uint32_t)(_jpg_buf_len/1024),
            (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    }

    last_frame = 0;
    return res;
}

httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = jpg_stream_httpd_handler,
    .user_ctx = NULL
    };


httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t stream_httpd  = NULL;

    if (httpd_start(&stream_httpd , &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd , &uri_get);
    }

    return stream_httpd;
}


void app_main()
{
    esp_err_t err;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    connect_wifi();

    if (wifi_connect_status)
    {
        err = init_camera2();
        if (err != ESP_OK)
        {
            printf("err: %s\n", esp_err_to_name(err));
            return;
        }
        setup_server();
        ESP_LOGI(TAG, "ESP32 CAM Web Server is up and running\n");
    }
    else
        ESP_LOGI(TAG, "Failed to connected with Wi-Fi, check your network Credentials\n");
}

// FUNCTIONS COPIED FROM KALUGA

static esp_err_t bsp_i2c_init(void)
{
    const i2c_config_t i2c_conf = {
        .mode = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BSP_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CONFIG_BSP_I2C_CLK_SPEED_HZ};
    ESP_ERROR_CHECK(i2c_param_config(BSP_I2C_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(BSP_I2C_NUM, i2c_conf.mode, 0, 0, 0));

    return ESP_OK;
}