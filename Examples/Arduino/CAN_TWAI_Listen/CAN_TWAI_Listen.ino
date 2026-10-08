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
#define TWAI_BITRATE            500000
#define TWAI_FDBITRATE          5000000

// Message IDs
#define TWAI_DATA_ID            0x100
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

    Serial.println("===================TWAI Example Starting...===================");

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
            .sp_permill = 875,
            .ssp_permill = 0,
        },
        .data_timing = {
            .bitrate = TWAI_FDBITRATE,
            .sp_permill = 625,
            .ssp_permill = 0,
        },
        .flags = {
            .enable_listen_only = true,
        }
    };

    // Create TWAI node
    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node));
    Serial.printf("TWAI node created\n");

    // Configure acceptance filter
    // twai_mask_filter_config_t data_filter = {
    //     .id = TWAI_DATA_ID,
    //     .mask = 0x7F0,      // Match high 7 bits of the ID, ignore low 4 bits
    //     .is_ext = false,    // Receive only standard ID
    // };
    // ESP_ERROR_CHECK(twai_node_config_mask_filter(node, 0, &data_filter));
    // Serial.printf("Filter enabled for ID: 0x%03X Mask: 0x%03X\n", data_filter.id, data_filter.mask);

    // Register transmission completion callback
    twai_event_callbacks_t callbacks = {
        .on_rx_done = twai_rx_callback,
        .on_state_change = twai_on_state_change_callback,
        // .on_error = twai_on_error_callback,
    };
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(node, &callbacks, &twai_rx_ctx));

    // Enable TWAI node
    ESP_ERROR_CHECK(twai_node_enable(node));
    Serial.printf("TWAI started successfully\n");
}

void loop() {
    esp_err_t ret;
    uint32_t blink_rx_tick = 0;

    while (1) {
        uint32_t tick_ms = xTaskGetTickCount();

        if(blink_rx_tick > 0 && tick_ms >= blink_rx_tick)
        {
            blink_rx_tick = 0;
            rgb.g = 0;
            rgb.r = 0xff;
            led.setColor(0,rgb);
            led.show();
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
