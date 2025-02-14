#include "sensor_readout_task.h"

#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Distance Measurement Definitions
#define SOUND_SPEED_CM_PER_US 0.0343
#define TIMEOUT_THRESHOLD 50000

#define SENSOR1_TRIGGER_PIN P11_3
#define SENSOR1_ECHO_PIN    P11_4

#define SENSOR2_TRIGGER_PIN P11_7
#define SENSOR2_ECHO_PIN    P11_2

#define SENSOR3_TRIGGER_PIN P12_3
#define SENSOR3_ECHO_PIN    P0_5

#define SENSOR4_TRIGGER_PIN P11_6
#define SENSOR4_ECHO_PIN    P11_5

// RPM Measurement Definitions

#define SENSOR_PIN_1 P10_4
#define SENSOR_PIN_2 P10_0
#define SENSOR_PIN_3 P10_3
#define SENSOR_PIN_4 P10_2

int pulse_counts[4] = {0, 0, 0, 0};
cyhal_gpio_callback_data_t gpio_callbacks[4];

cyhal_timer_t timer_distance, timer_rpm; // Separate timers
QueueHandle_t distance_queue = NULL;
QueueHandle_t speed_queue = NULL;

// Function Prototypes
//void init_distance_timer(void);
void init_rpm_timer(void);
void send_trigger_pulse(cyhal_gpio_t trigger_pin);
float measure_distance(cyhal_gpio_t trigger_pin, cyhal_gpio_t echo_pin);
void init_sensor_pin(cyhal_gpio_t pin);
void update_pulse_count(int *pulse_count, int *prev_sensor_state, cyhal_gpio_t pin);
float calculate_rpm_speed(int pulse_count);
//void pulse_detected_callback(void *callback_arg, cyhal_gpio_event_t event);

void pulse_detected_callback(void *callback_arg, cyhal_gpio_event_t event) {
    // Increment the pulse count for the sensor that triggered the interrupt
    int sensor_index = (int)callback_arg; // Index passed as callback_arg
    pulse_counts[sensor_index]++;
}

void init_distance_timer(void) {
    cyhal_timer_cfg_t timer_cfg = {
        .compare_value = 0,
        .period = 0xFFFFFFFF,
        .direction = CYHAL_TIMER_DIR_UP,
        .is_compare = false,
        .is_continuous = true,
        .value = 0
    };
    cyhal_timer_init(&timer_distance, NC, NULL);
    cyhal_timer_configure(&timer_distance, &timer_cfg);
    cyhal_timer_start(&timer_distance);
}

void init_rpm_timer(void) {
    cyhal_timer_cfg_t timer_cfg = {
        .compare_value = 0,
        .period = 0xFFFFFFFF,
        .direction = CYHAL_TIMER_DIR_UP,
        .is_compare = false,
        .is_continuous = true,
        .value = 0
    };
    cyhal_timer_init(&timer_rpm, NC, NULL);
    cyhal_timer_configure(&timer_rpm, &timer_cfg);
    cyhal_timer_start(&timer_rpm);
}

void send_trigger_pulse(cyhal_gpio_t trigger_pin) {
    cyhal_gpio_write(trigger_pin, 1);
    cyhal_system_delay_us(10);
    cyhal_gpio_write(trigger_pin, 0);
}

float measure_distance(cyhal_gpio_t trigger_pin, cyhal_gpio_t echo_pin) {
    cyhal_timer_reset(&timer_distance);
    while (cyhal_gpio_read(echo_pin) == 1);
    send_trigger_pulse(trigger_pin);
    while (cyhal_gpio_read(echo_pin) == 0) {
        if (cyhal_timer_read(&timer_distance) > TIMEOUT_THRESHOLD) return -1.0;
    }
    uint32_t start_time = cyhal_timer_read(&timer_distance);
    while (cyhal_gpio_read(echo_pin) == 1) {
        if (cyhal_timer_read(&timer_distance) - start_time > TIMEOUT_THRESHOLD) return -1.0;
    }
    uint32_t pulse_duration = cyhal_timer_read(&timer_distance) - start_time;
    return (pulse_duration / 2.0) * SOUND_SPEED_CM_PER_US;
}

void init_gpio_interrupts(cyhal_gpio_t pins[]) {
	for (int i = 0; i < 4; i++) {
		cyhal_gpio_init(pins[i], CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, false);

		gpio_callbacks[i].callback = pulse_detected_callback;
		gpio_callbacks[i].callback_arg = (void *)i; // Pass sensor index as argument

		cyhal_gpio_register_callback(pins[i], &gpio_callbacks[i]);

		cyhal_gpio_enable_event(pins[i], CYHAL_GPIO_IRQ_RISE, CYHAL_ISR_PRIORITY_DEFAULT, true);
	}
}
/*
void init_sensor_pin(cyhal_gpio_t pin) {
    cyhal_gpio_init(pin, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, false);
}
*/
void update_pulse_count(int *pulse_count, int *prev_sensor_state, cyhal_gpio_t pin) {
    int sensor_state = cyhal_gpio_read(pin);
    if (sensor_state == 1 && *prev_sensor_state == 0) {
    	(*pulse_count)++;
        printf("Pulse detected on pin %d. Total pulses: %d\n", pin, *pulse_count);
    }
    *prev_sensor_state = sensor_state;
}
/*
void calculate_rpm_speed(int pulse_count, int *rpm, float *speed) {
    *rpm = pulse_count * 0.05 * (60000 / 500);
    *speed = (0.03 * 2 * 3.14 * (*rpm)) / 60;
}
*/
float calculate_rpm_speed(int pulse_count) {
    float rpm = pulse_count * 0.05 * (60000 / 1000);
    float speed = (0.03 * 2 * 3.14 * (rpm)) / 60;
    return rpm;
}

void sensor_readout_task(void *params) {
	/*
    cybsp_init();
    __enable_irq();
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, 115200);
	*/

    cyhal_gpio_t trigger_pins[4] = { SENSOR1_TRIGGER_PIN, SENSOR2_TRIGGER_PIN, SENSOR3_TRIGGER_PIN, SENSOR4_TRIGGER_PIN };
    cyhal_gpio_t echo_pins[4] = { SENSOR1_ECHO_PIN, SENSOR2_ECHO_PIN, SENSOR3_ECHO_PIN, SENSOR4_ECHO_PIN };
    cyhal_gpio_t rpm_pins[4] = { SENSOR_PIN_1, SENSOR_PIN_2, SENSOR_PIN_3, SENSOR_PIN_4 };

    init_distance_timer();
	init_rpm_timer();
	init_gpio_interrupts(rpm_pins);

    //int pulse_counts[4] = {0, 0, 0, 0};
    //int prev_states[4] = {0, 0, 0, 0};

    // Initialize GPIOs for distance sensors and RPM sensors
    for (int i = 0; i < 4; i++) {
        cyhal_gpio_init(trigger_pins[i], CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, false);
        cyhal_gpio_init(echo_pins[i], CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, false);
        //init_sensor_pin(rpm_pins[i]);
    }

    if (distance_queue == NULL) {
		distance_queue = xQueueCreate(10, sizeof(DistanceData_t));
	}
    if (speed_queue == NULL) {
    	speed_queue = xQueueCreate(10, sizeof(SpeedData_t));
	}

    while (true) {

    	//DISTANCES
    	float distance0 = measure_distance(trigger_pins[0], echo_pins[0]);
    	float distance1 = measure_distance(trigger_pins[1], echo_pins[1]);
    	float distance2 = measure_distance(trigger_pins[2], echo_pins[2]);
    	float distance3 = measure_distance(trigger_pins[3], echo_pins[3]);

    	// RPM PROCESSING --NEW
		uint32_t elapsed_time_ms = cyhal_timer_read(&timer_rpm) / 1000;
		if (elapsed_time_ms >= 1000) { // Example: 1-second interval
			float speeds[4];
			taskENTER_CRITICAL();
			for (int i = 0; i < 4; i++) {
				speeds[i] = calculate_rpm_speed(pulse_counts[i]);
				pulse_counts[i] = 0; // Reset after reading
			}
			taskEXIT_CRITICAL();
			cyhal_timer_reset(&timer_rpm); // Reset the timer for the next interval

			// Send or process speed data
			SpeedData_t speedData =
						{ .speed0 = speeds[0],
						  .speed1 = speeds[1],
						  .speed2 = speeds[2],
						  .speed3 = speeds[3]
						};
			xQueueSend(speed_queue, &speedData, pdMS_TO_TICKS(0));
		}

    	/*
    	//RPM
    	for (int i = 0; i < 4; i++) {
    		update_pulse_count(&pulse_counts[i], &prev_states[i], rpm_pins[i]);
		}

		// Check if 5 seconds have elapsed for RPM calculation
    	float speed0;
    	float speed1;
    	float speed2;
    	float speed3;

		if (cyhal_timer_read(&timer_rpm) / 1000 >= 500) {

			speed0 = calculate_rpm_speed(pulse_counts[0]);
			pulse_counts[0] = 0;
			speed1 = calculate_rpm_speed(pulse_counts[1]);
			pulse_counts[1] = 0;
			speed2 = calculate_rpm_speed(pulse_counts[2]);
			pulse_counts[2] = 0;
			speed3 = calculate_rpm_speed(pulse_counts[3]);
			pulse_counts[3] = 0;

			//printf("Sensor %d, Speed: %f m/s\r\n", 0, distance0);
			//printf("Sensor %d, Speed: %f m/s\r\n", 1, distance1);
			//printf("Sensor %d, Speed: %f m/s\r\n", 2, distance2);
			//printf("Sensor %d, Speed: %f m/s\r\n", 3, distance3);

			cyhal_timer_reset(&timer_rpm); // Reset only the RPM timer
		}
		*/
    	DistanceData_t distanceData =
			{ .distance0 = distance0,
		   	  .distance1 = distance1,
			  .distance2 = distance2,
			  .distance3 = distance3
			};


		if (xQueueSend(distance_queue, &distanceData, pdMS_TO_TICKS(0)) != pdPASS) {
			printf("Failed to send distanceData to queue\n");
    	}
		/*
    	if (xQueueSend(speed_queue, &speedData, pdMS_TO_TICKS(0)) != pdPASS) {
			printf("Failed to send speedData to queue\n");
		}
		*/
    	/*
    	printf("Sensor 0 Distance: %.2f cm\n", data.distance0);
    	printf("Sensor 1 Distance: %.2f cm\n", data.distance1);
    	printf("Sensor 2 Distance: %.2f cm\n", data.distance2);
    	printf("Sensor 3 Distance: %.2f cm\n", data.distance3);
    	*/
    	vTaskDelay(pdMS_TO_TICKS(20));

        /*
        // Update pulse counts for each RPM sensor
        for (int i = 0; i < 4; i++) {
            //update_pulse_count(&pulse_counts[i], &prev_states[i], rpm_pins[i]);
        }

        // Check if 5 seconds have elapsed for RPM calculation

        if (cyhal_timer_read(&timer_rpm) / 1000 >= 5000) {
            for (int i = 0; i < 4; i++) {
                int rpm;
                float speed;
                calculate_rpm_speed(pulse_counts[i], &rpm, &speed);
                printf("Sensor %d RPM: %d, Speed: %f m/s\r\n", i + 1, rpm, speed);
                pulse_counts[i] = 0;
            }
            cyhal_timer_reset(&timer_rpm); // Reset only the RPM timer
        }
        */
    }
}


