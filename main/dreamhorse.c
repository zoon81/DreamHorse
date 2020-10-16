#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include "esp_spiffs.h"
#include <driver/adc.h>

#include "driver/mcpwm.h"
#include "soc/mcpwm_periph.h"

#include "webserver.h"
#include "wifi.h"
#include "motion_controller.h"
#include "dreamhorse.h"

static const char *TAG="MAIN.C";
xQueueHandle motion_contorller_task_queue = NULL;

void mcpwm_example_gpio_initialize(void);
void brushed_motor_forward(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num , float duty_cycle);
void brushed_motor_backward(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num , float duty_cycle);
void brushed_motor_stop(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num);
void motion_forward(float duty);
void motion_backward(float duty);
void motion_left(float duty);
void motion_rigth(float duty);
void motion_backward(float duty);
void motion_stop();
uint8_t getHeadPosition();
void setHeadPosition(uint8_t position, uint8_t speed);
uint16_t getHeadLrAdc();
uint16_t getHeadUdAdc();

/**
 * @brief Configure MCPWM module for brushed dc motor
 */
static void mcpwm_example_brushed_motor_control(void *arg)
{
    //1. mcpwm gpio initialization
    mcpwm_example_gpio_initialize();

    //2. initial mcpwm configuration
    printf("Configuring Initial Parameters of mcpwm...\n");
    mcpwm_config_t pwm_config;
    pwm_config.frequency = 1000;    //frequency = 500Hz,
    pwm_config.cmpr_a = 0;    //duty cycle of PWMxA = 0
    pwm_config.cmpr_b = 0;    //duty cycle of PWMxb = 0
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);    //Configure PWM0A & PWM0B with above settings
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_1, &pwm_config);    //Configure PWM1A & PWM1B with above settings
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_2, &pwm_config);    //Configure PWM3A & PWM2B with above settings
    uint8_t queue_data;
    for(;;) {
        if(xQueueReceive(motion_contorller_task_queue, &queue_data, portMAX_DELAY)){
             printf("\nUI task ent handler loop queue data : %d\n", queue_data);
             switch (queue_data){
                 case QUEUE_REQUEST_FOWARD:
                    motion_forward(80.0);
                    break;
                case QUEUE_REQUEST_LEFT:
                    motion_left(80.0);
                    break;
                case QUEUE_REQUEST_RIGTH:
                    motion_rigth(80.0);
                    break;
                case QUEUE_REQUEST_STOP:
                    motion_stop();
                    break;
                case QUEUE_REQUEST_HEAD_DOWN:
                    setHeadPosition(HEAD_POS_DOWN, 70);
                    break;
                case QUEUE_REQUEST_HEAD_UP:
                    setHeadPosition(HEAD_POS_UP, 70);
                    break;
                case QUEUE_REQUEST_HEAD_RIGTH:
                    setHeadPosition(HEAD_POS_RIGTH, 70);
                    break;
                case QUEUE_REQUEST_HEAD_LEFT:
                    setHeadPosition(HEAD_POS_LEFT, 70);
                    break;
                case QUEUE_REQUEST_HEAD_LR_MIDDLE:
                    setHeadPosition(HEAD_POS_LR_MIDDLE, 70);
                    break;
                default:
                    break;
            }
        }
    }
}

void app_main()
{
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = false
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    static httpd_handle_t server = NULL;
    initialise_wifi(&server);
    // Position reading ADC setup
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_3,ADC_ATTEN_DB_11);
    // Legs Position sensor GPIO setup
    gpio_config_t gpio;
    gpio.mode = GPIO_MODE_INPUT;
    gpio.pull_up_en = 0;
    gpio.pin_bit_mask = (uint64_t) 1 << GPIO_LEGLEFT_POS | (uint64_t) 1 << GPIO_LEGRIGTH_POS;
    gpio.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&gpio);
    
    int val = adc1_get_raw(ADC1_CHANNEL_0);
    motion_contorller_task_queue = xQueueCreate(10, sizeof(uint8_t));
    xTaskCreate(mcpwm_example_brushed_motor_control, "mcpwm_examlpe_brushed_motor_control", 4096, NULL, 5, NULL);
    while(1){
        vTaskDelay(200 / portTICK_RATE_MS);
    }
}

// Wrapper funtions for motion controlling

void motion_forward(float duty){
    setHeadPosition(HEAD_POS_LR_MIDDLE | HEAD_POS_UP, 70);
    //Go
    brushed_motor_forward(MCPWM_UNIT_0, MCPWM_TIMER_2, duty);
}

void motion_left(float duty){
    setHeadPosition(HEAD_POS_LEFT, 70);
    brushed_motor_forward(MCPWM_UNIT_0, MCPWM_TIMER_2, duty);
}

void motion_rigth(float duty){
    setHeadPosition(HEAD_POS_RIGTH, 70);
    brushed_motor_forward(MCPWM_UNIT_0, MCPWM_TIMER_2, duty);
}

void motion_stop(){
    brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_1);
    // Stop at the next rigth direction, or the hourse fall
    uint8_t timeout = 0;
    while( (gpio_get_level(GPIO_LEGLEFT_POS) || gpio_get_level(GPIO_LEGRIGTH_POS)) && timeout <= MOTION_TIMEOUT_LEGS_100MS ){
        ESP_LOGE(TAG, "LL GPIO : %d, RL GPIO : %d", gpio_get_level(GPIO_LEGLEFT_POS), gpio_get_level(GPIO_LEGRIGTH_POS));
        vTaskDelay( pdMS_TO_TICKS(100) );
        timeout++;
    }
    brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_2);
    ESP_LOGE(TAG, "LL GPIO : %d, RL GPIO : %d", gpio_get_level(GPIO_LEGLEFT_POS), gpio_get_level(GPIO_LEGRIGTH_POS));
}

// Head positioning functions

void setHeadPosition(uint8_t position, uint8_t speed){
    uint8_t head_pos = getHeadPosition();
    if( !(head_pos & 0x07)){
        // Head stacked at unknown position, turn it leftmost
        brushed_motor_backward(MCPWM_UNIT_0, MCPWM_TIMER_1, 80);
        vTaskDelay( pdMS_TO_TICKS(2000) );
        brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_1);
    }
    // Try again
    head_pos = getHeadPosition();
    if( !(head_pos & 0x07)){
        // Failed, something wrong
        return;
    }

    if(position & HEAD_POS_LEFT){
        if(head_pos & HEAD_POS_RIGTH || head_pos & HEAD_POS_LR_MIDDLE){
            // Turn head left
            ESP_LOGI(TAG, "HEAD TURNING LEFT");
            brushed_motor_backward(MCPWM_UNIT_0, MCPWM_TIMER_1, speed);
            uint8_t timeout = 0;
            while( !(getHeadPosition() & HEAD_POS_LEFT) && timeout <= MOTION_TIMEOUT_HEAD_LR_200MS){
                vTaskDelay( pdMS_TO_TICKS(200) );
                timeout++;
            }
            brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_1);
        }
    }else if(position & HEAD_POS_RIGTH){
        if(head_pos & HEAD_POS_LEFT || head_pos & HEAD_POS_LR_MIDDLE){
            // Turn head rigth
            ESP_LOGI(TAG, "HEAD TURNING RIGTH");
            brushed_motor_forward(MCPWM_UNIT_0, MCPWM_TIMER_1, speed);
            uint8_t timeout = 0;
            while( !(getHeadPosition() & HEAD_POS_RIGTH) && timeout <= MOTION_TIMEOUT_HEAD_LR_200MS){
                vTaskDelay( pdMS_TO_TICKS(200) );
                timeout++;
            }
            brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_1);
        }
    } else if(position & HEAD_POS_LR_MIDDLE){
        if(head_pos & HEAD_POS_LEFT){
            // Turn rigth
            ESP_LOGI(TAG, "HEAD TURNING MIDDLE and go rigth");
            brushed_motor_forward(MCPWM_UNIT_0, MCPWM_TIMER_1, speed);
            uint8_t timeout = 0;
            while( !(getHeadPosition() & HEAD_POS_LR_MIDDLE) && timeout <= MOTION_TIMEOUT_HEAD_LR_200MS){
                vTaskDelay( pdMS_TO_TICKS(100) );
                timeout++;
            }
            brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_1);
        }
        if(head_pos & HEAD_POS_RIGTH){
            // Turn rigth
            ESP_LOGI(TAG, "HEAD TURNING MIDDLE and go left");
            brushed_motor_backward(MCPWM_UNIT_0, MCPWM_TIMER_1, speed);
            uint8_t timeout = 0;
            while( !(getHeadPosition() & HEAD_POS_LR_MIDDLE) && timeout <= MOTION_TIMEOUT_HEAD_LR_200MS){
                vTaskDelay( pdMS_TO_TICKS(100) );
                timeout++;
            }
            brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_1);
        }

    }
    if(position & HEAD_POS_DOWN){
        if( ! (head_pos & HEAD_POS_DOWN)){
            ESP_LOGI(TAG, "HEAD GOING DOWN");
            brushed_motor_forward(MCPWM_UNIT_0, MCPWM_TIMER_0, speed);
            uint8_t timeout = 0;
            while( !(getHeadPosition() & HEAD_POS_DOWN) && timeout <= MOTION_TIMEOUT_HEAD_LR_200MS){
                vTaskDelay( pdMS_TO_TICKS(200) );
                timeout++;
            }
            brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
        }
    } else if(position & HEAD_POS_UP){
        if( ! (head_pos & HEAD_POS_UP)){
            ESP_LOGI(TAG, "HEAD GOING UP");
            brushed_motor_backward(MCPWM_UNIT_0, MCPWM_TIMER_0, speed);
            uint8_t timeout = 0;
            while( !(getHeadPosition() & HEAD_POS_UP) && timeout <= MOTION_TIMEOUT_HEAD_LR_200MS){
                vTaskDelay( pdMS_TO_TICKS(200) );
                timeout++;
            }
            brushed_motor_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
        }

    } else if(position & HEAD_POS_UD_MIDDLE){

    }
}

uint8_t getHeadPosition(){
    uint8_t headpos = 0;
    uint16_t head_pos_lr = getHeadLrAdc();
    uint16_t head_pos_ud = getHeadUdAdc();
    // LeftRigth Direction
    ESP_LOGI(TAG, "HEAD ADC LR : %d, UD : %d", head_pos_lr, head_pos_ud);
    if(head_pos_lr >= (ADC_RAW_HEAD_LEFT - ADC_RAW_HEAD_LR_MARGIN) && head_pos_lr <= (ADC_RAW_HEAD_LEFT + ADC_RAW_HEAD_LR_MARGIN)){
        headpos |= HEAD_POS_LEFT;
        ESP_LOGI(TAG, "HEAD POS LEFT");
    } else if(head_pos_lr >= (ADC_RAW_HEAD_RIGTH - ADC_RAW_HEAD_LR_MARGIN) && head_pos_lr <= (ADC_RAW_HEAD_RIGTH + ADC_RAW_HEAD_LR_MARGIN)){
        headpos |= HEAD_POS_RIGTH;
        ESP_LOGI(TAG, "HEAD POS RIGTH");
    } else if(head_pos_lr >= (ADC_RAW_HEAD_MIDDLE - ADC_RAW_HEAD_MIDDLE_MARGIN) && head_pos_lr <= (ADC_RAW_HEAD_MIDDLE + ADC_RAW_HEAD_MIDDLE_MARGIN)){
        headpos |= HEAD_POS_LR_MIDDLE;
        ESP_LOGI(TAG, "HEAD POS LR MIDDLE");
    }
    // UpDown Direction
    if(head_pos_ud >= (ADC_RAW_HEAD_UP - ADC_RAW_HEAD_LR_MARGIN) && head_pos_ud <= (ADC_RAW_HEAD_UP + ADC_RAW_HEAD_LR_MARGIN)){
        headpos |= HEAD_POS_UP;
        ESP_LOGI(TAG, "HEAD POS UP");
    } else if(head_pos_ud >= (ADC_RAW_HEAD_DOWN - ADC_RAW_HEAD_LR_MARGIN) && head_pos_ud <= (ADC_RAW_HEAD_DOWN + ADC_RAW_HEAD_LR_MARGIN)){
        headpos |= HEAD_POS_DOWN;
        ESP_LOGI(TAG, "HEAD POS DOWN");
    }
    return headpos;
}

uint16_t getHeadLrAdc(){
    uint32_t adc_raw = 0;
    uint8_t samples = 32;
    while(samples--){
        adc_raw += adc1_get_raw(ADC1_CHANNEL_3);
    }
    return (uint16_t) (adc_raw / 32);
}

uint16_t getHeadUdAdc(){
    uint32_t adc_raw = 0;
    uint8_t samples = 32;
    while(samples--){
        adc_raw += adc1_get_raw(ADC1_CHANNEL_0);
    }
    return (uint16_t) (adc_raw / 32);
}

void mcpwm_example_gpio_initialize(void)
{
    printf("initializing mcpwm gpio...\n");
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, GPIO_MOTOR_HEADUPDOWN_IN1);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, GPIO_MOTOR_HEADUPDOWN_IN2);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1A, GPIO_MOTOR_HEADLEFTRIGTH_IN1);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1B, GPIO_MOTOR_HEADLEFTRIGTH_IN2);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2A, GPIO_MOTOR_FORWARDBACKWARD_IN1);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2B, GPIO_MOTOR_FORWARDBACKWARD_IN2);
}

/**
 * @brief motor moves in forward direction, with duty cycle = duty %
 */
void brushed_motor_forward(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num , float duty_cycle)
{
    mcpwm_set_signal_low(mcpwm_num, timer_num, MCPWM_OPR_B);
    mcpwm_set_duty(mcpwm_num, timer_num, MCPWM_OPR_A, duty_cycle);
    mcpwm_set_duty_type(mcpwm_num, timer_num, MCPWM_OPR_A, MCPWM_DUTY_MODE_0); //call this each time, if operator was previously in low/high state
}

/**
 * @brief motor moves in backward direction, with duty cycle = duty %
 */
void brushed_motor_backward(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num , float duty_cycle)
{
    mcpwm_set_signal_low(mcpwm_num, timer_num, MCPWM_OPR_A);
    mcpwm_set_duty(mcpwm_num, timer_num, MCPWM_OPR_B, duty_cycle);
    mcpwm_set_duty_type(mcpwm_num, timer_num, MCPWM_OPR_B, MCPWM_DUTY_MODE_0);  //call this each time, if operator was previously in low/high state
}

/**
 * @brief motor stop
 */
void brushed_motor_stop(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num)
{
    mcpwm_set_signal_low(mcpwm_num, timer_num, MCPWM_OPR_A);
    mcpwm_set_signal_low(mcpwm_num, timer_num, MCPWM_OPR_B);
}