#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

#include "LedStrip.h"
#define WS2812_DIN_PIN 27
rgb_t rgb;
LedStrip led(WS2812_DIN_PIN,1);

#define TWAI_RX_GPIO     GPIO_NUM_24
#define TWAI_TX_GPIO     GPIO_NUM_23
#define TWAI_QUEUE_DEPTH        10
#define TWAI_BITRATE            1000000
#define TWAI_FDBITRATE          5000000

// Message IDs
#define TWAI_DATA_ID            0x100
#define TWAI_HEARTBEAT_ID       0x7FF
#define TWAI_DATA_LEN           1000
// Buffer for burst data handling
#define POLL_DEPTH              200

static const char *TAG = "twai";

typedef struct {
    twai_frame_t frame;
    uint8_t data[TWAIFD_FRAME_MAX_LEN];
} twai_data_t;

typedef struct {
    twai_data_t *rx_pool;
    SemaphoreHandle_t free_pool_semaphore;
    SemaphoreHandle_t rx_result_semaphore;
    int write_idx;
    int read_idx;
} twai_rx_ctx_t;

// Transmission completion callback
static IRAM_ATTR bool twai_tx_done_callback(twai_node_handle_t handle, const twai_tx_done_event_data_t *edata, void *user_ctx)
{
    if (!edata->is_tx_success) {
        Serial.printf("Failed to transmit message, ID: 0x%X\n", edata->done_tx_frame->header.id);
    }
    return false; // No task wake required
}

// Node state
static bool IRAM_ATTR twai_on_state_change_callback(twai_node_handle_t handle, const twai_state_change_event_data_t *edata, void *user_ctx)
{
    const char *twai_state_name[] = {"error_active", "error_warning", "error_passive", "bus_off"};
    Serial.printf("state changed: %s -> %s\n", twai_state_name[edata->old_sta], twai_state_name[edata->new_sta]);
    return false;
}

// Bus error callback
static IRAM_ATTR bool twai_on_error_callback(twai_node_handle_t handle, const twai_error_event_data_t *edata, void *user_ctx)
{
    Serial.printf("TWAI node error: 0x%x\n", edata->err_flags.val);
    return false; // No task wake required
}

// TWAI receive callback - store data and signal
static bool IRAM_ATTR twai_rx_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken;
    twai_rx_ctx_t *ctx = (twai_rx_ctx_t *)user_ctx;

    Serial.printf("TWAI node rx\n");

    if (xSemaphoreTakeFromISR(ctx->free_pool_semaphore, &woken) != pdTRUE) {
        Serial.printf("Pool full, dropping frame\n");
        return (woken == pdTRUE);
    }
    if (twai_node_receive_from_isr(handle, &ctx->rx_pool[ctx->write_idx].frame) == ESP_OK) {
        ctx->write_idx = (ctx->write_idx + 1) % POLL_DEPTH;
        xSemaphoreGiveFromISR(ctx->rx_result_semaphore, &woken);
    }
    return (woken == pdTRUE);
}

twai_node_handle_t node = NULL;
twai_rx_ctx_t twai_rx_ctx = {0};

void setup()
{
    Serial.begin(115200);
    
    Serial.println("ws2812 init");
    led.init();
    led.setBrightness(50);
    rgb.r = 0xff;
    led.setColor(0,rgb);
    led.show();

    delay(1000);

    Serial.println("===================CAN_TWAI Starting...===================");

    // Create semaphore for receive notification
    twai_rx_ctx.free_pool_semaphore = xSemaphoreCreateCounting(POLL_DEPTH, POLL_DEPTH);
    twai_rx_ctx.rx_result_semaphore = xSemaphoreCreateCounting(POLL_DEPTH, 0);
    assert(twai_rx_ctx.free_pool_semaphore != NULL);
    assert(twai_rx_ctx.rx_result_semaphore != NULL);

    twai_rx_ctx.rx_pool = (twai_data_t *)calloc(POLL_DEPTH, sizeof(twai_data_t));
    assert(twai_rx_ctx.rx_pool != NULL);
    for (int i = 0; i < POLL_DEPTH; i++) {
        twai_rx_ctx.rx_pool[i].frame.buffer = twai_rx_ctx.rx_pool[i].data;
        twai_rx_ctx.rx_pool[i].frame.buffer_len = sizeof(twai_rx_ctx.rx_pool[i].data);
    }
    Serial.printf("Buffer initialized: %d slots for burst data\n", POLL_DEPTH);

    // Configure TWAI node
    twai_onchip_node_config_t node_config = {
        .io_cfg = {
            .tx = TWAI_TX_GPIO,
            .rx = TWAI_RX_GPIO,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = TWAI_BITRATE,
        },
        .data_timing = {
            .bitrate = TWAI_FDBITRATE,
        },
        .fail_retry_cnt = 3,
        .tx_queue_depth = TWAI_QUEUE_DEPTH,
    };

    // Create TWAI node
    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node));
    Serial.printf("TWAI node created\n");

    // Configure acceptance filter
    twai_mask_filter_config_t data_filter = {
        .id = TWAI_DATA_ID,
        .mask = 0x7F0,      // Match high 7 bits of the ID, ignore low 4 bits
        .is_ext = false,    // Receive only standard ID
    };
    ESP_ERROR_CHECK(twai_node_config_mask_filter(node, 0, &data_filter));
    Serial.printf("Filter enabled for ID: 0x%03X Mask: 0x%03X\n", data_filter.id, data_filter.mask);

    // Register transmission completion callback
    twai_event_callbacks_t callbacks = {
        .on_tx_done = twai_tx_done_callback,
        .on_rx_done = twai_rx_callback,
        .on_state_change = twai_on_state_change_callback,
        // .on_error = twai_on_error_callback,
    };
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(node, &callbacks, &twai_rx_ctx));

    // Enable TWAI node
    ESP_ERROR_CHECK(twai_node_enable(node));
    Serial.printf("TWAI started successfully\n");
    Serial.printf("Sending messages with IDs: 0x%03X (data), 0x%03X (heartbeat)\n",  TWAI_DATA_ID, TWAI_HEARTBEAT_ID);
}

void loop() {
    esp_err_t ret;
    uint32_t blink_tx_tick = 0;
    uint32_t blink_rx_tick = 0;
    uint32_t heartbeat_tick = 0;

    while (1) {
        uint32_t tick_ms = xTaskGetTickCount();

        if(blink_tx_tick > 0 && tick_ms >= blink_tx_tick)
        {
            blink_tx_tick = 0;
            rgb.b = 0;
            rgb.r = 0xff;
            led.setColor(0,rgb);
            led.show();
        }

        if(blink_rx_tick > 0 && tick_ms >= blink_rx_tick)
        {
            blink_rx_tick = 0;
            rgb.g = 0;
            rgb.r = 0xff;
            led.setColor(0,rgb);
            led.show();
        }

        if (tick_ms >= heartbeat_tick)
        {
            heartbeat_tick = tick_ms + 1000;

            twai_node_status_t status;
            twai_node_get_info(node, &status, NULL);
            if (status.state == TWAI_ERROR_BUS_OFF) {
                ESP_LOGW(TAG, "Bus-off detected");
                break;
            }

            // Send heartbeat message
            uint64_t timestamp = esp_timer_get_time();
            twai_frame_t tx_frame;
            memset((uint8_t *)&tx_frame.header,0,sizeof(tx_frame.header));
            tx_frame.header.id = TWAI_HEARTBEAT_ID,
            tx_frame.header.brs = 0;
            tx_frame.header.fdf = 0;
            tx_frame.buffer = (uint8_t *) &timestamp,
            tx_frame.buffer_len = sizeof(timestamp),
            tx_frame.header.dlc = tx_frame.buffer_len;
            
            ret = twai_node_transmit(node, &tx_frame, 500);
            if (ret == ESP_OK) {
            Serial.printf("Sending heartbeat message: %lld\n", timestamp);
            twai_node_transmit_wait_all_done(node, 1000); // -1 means wait forever
            }

            rgb.b = 0xff;
            rgb.r = 0;
            led.setColor(0,rgb);
            led.show();
            blink_tx_tick = tick_ms + 100;

            // Send burst data messages every 10 seconds
            if ((timestamp / 1000000) % 10 == 0) {
                int num_frames = howmany(TWAI_DATA_LEN, TWAIFD_FRAME_MAX_LEN);
                twai_data_t *data = (twai_data_t *)calloc(num_frames, sizeof(twai_data_t));
                assert(data != NULL);
                Serial.printf("Sending packet of %d bytes in %d frames\n", TWAIFD_FRAME_MAX_LEN, num_frames);
                for (int i = 0; i < num_frames; i++) {
                    memset((uint8_t *)&data[i].frame.header, 0, sizeof(data[i].frame.header));
                    data[i].frame.header.id = TWAI_DATA_ID;
                    data[i].frame.header.brs = 1;
                    data[i].frame.header.fdf = 1;
                    data[i].frame.header.dlc = TWAIFD_FRAME_MAX_DLC;
                    data[i].frame.buffer = data[i].data;
                    data[i].frame.buffer_len = TWAIFD_FRAME_MAX_LEN;
                    memset(data[i].data, i, TWAIFD_FRAME_MAX_LEN);
                    twai_node_transmit(node, &data[i].frame, 500);
                }

                // Frames mounted, wait for all frames to be transmitted
                twai_node_transmit_wait_all_done(node, 1000);
                free(data);
            }
        }

        if (xSemaphoreTake(twai_rx_ctx.rx_result_semaphore, 0) == pdTRUE) {
            twai_frame_t *frame = &twai_rx_ctx.rx_pool[twai_rx_ctx.read_idx].frame;
            Serial.printf("%s RX: %x [%d] %x %x %x %x %x %x %x %x\n", TAG,\
                     frame->header.id, frame->header.dlc, frame->buffer[0], frame->buffer[1], frame->buffer[2], frame->buffer[3], frame->buffer[4], frame->buffer[5], frame->buffer[6], frame->buffer[7]);
            twai_rx_ctx.read_idx = (twai_rx_ctx.read_idx + 1) % POLL_DEPTH;
            xSemaphoreGive(twai_rx_ctx.free_pool_semaphore);
            
            rgb.g = 0xff;
            rgb.r = 0;
            led.setColor(0,rgb);
            led.show();
            blink_rx_tick = tick_ms + 100;
        }
    }

    // Cleanup
    vSemaphoreDelete(twai_rx_ctx.rx_result_semaphore);
    vSemaphoreDelete(twai_rx_ctx.free_pool_semaphore);
    free(twai_rx_ctx.rx_pool);
    ESP_ERROR_CHECK(twai_node_disable(node));
    ESP_ERROR_CHECK(twai_node_delete(node));

    Serial.printf("exit\n");

    while(1)
    {
        rgb.b = 0;
        rgb.g = 0;
        rgb.r = 0xFF;
        led.setColor(0,rgb);
        led.show();

        delay(100);

        rgb.b = 0;
        rgb.g = 0;
        rgb.r = 0;
        led.setColor(0,rgb);
        led.show();
    }
}
