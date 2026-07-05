#include <Arduino.h>
#include <micro_ros_arduino.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <geometry_msgs/msg/twist.h>

// =======================================================
// ===================== FLAGS ===========================
volatile bool microros_enable  = true;
volatile bool microros_running = false;

// =======================================================
// ===================== FAHRZEUGDATEN ===================
struct VehicleData {
    float position;
    float velocity;
    float theta;
};
volatile VehicleData vehicleData = {0.0f, 0.0f, 0.0f};

// =======================================================
// ===================== ROS OBJEKTE =====================
rcl_publisher_t    vehicle_pub;
rcl_subscription_t cmd_sub;

geometry_msgs__msg__Twist vehicle_msg;
geometry_msgs__msg__Twist cmd_msg;

// =======================================================
// ===================== TEENSY UART ====================
#define TEENSY_RX_SEND 15    // RX Pin zum Teensy senden
#define TEENSY_TX_SEND 2    // TX Pin vom Teensy empfangen
#define TEENSY_RX_RECV 18     // RX Pin vom Teensy empfangen
#define TEENSY_TX_RECV 19     // TX Pin zum Teensy senden

HardwareSerial SerialSend(2); // UART2 für Senden
HardwareSerial SerialRecv(1); // UART1 für Empfangen

struct TeensyState {
    float theta;
    float position;
    float velocity;
};

struct EspCommand {
    float theta_ref;
    float position_ref;
};

volatile TeensyState teensy_state = {0.0f, 0.0f, 0.0f};
volatile EspCommand  esp_cmd     = {0.0f, 0.0f};

// =======================================================
// ===================== CALLBACK ========================
void cmdCallback(const void * msgin)
{
    const geometry_msgs__msg__Twist * msg =
        (const geometry_msgs__msg__Twist *)msgin;

    EspCommand tmp;
    tmp.theta_ref    = msg->angular.z;
    tmp.position_ref = msg->linear.x;

    // atomar kopieren
    memcpy((void*)&esp_cmd, (void*)&tmp, sizeof(tmp));
}

// =======================================================
// ===================== MICRO-ROS TASK ==================
void MicroRosTask(void *pvParameters)
{
    while (!microros_enable) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool node_initialized = false;
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;

    while (!node_initialized)
    {
        set_microros_transports();

        if (rclc_support_init(&support, 0, NULL, &allocator) != RCL_RET_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        rcl_node_t node;
        if (rclc_node_init_default(&node, "vehicle_node", "", &support) != RCL_RET_OK) {
            rclc_support_fini(&support);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (rclc_publisher_init_default(
                &vehicle_pub,
                &node,
                ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
                "vehicle_data_raw") != RCL_RET_OK)
        {
            rcl_node_fini(&node);
            rclc_support_fini(&support);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (rclc_subscription_init_default(
                &cmd_sub,
                &node,
                ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
                "cmd_pub") != RCL_RET_OK)
        {
            rcl_publisher_fini(&vehicle_pub, &node);
            rcl_node_fini(&node);
            rclc_support_fini(&support);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        rclc_executor_t executor;
        rclc_executor_init(&executor, &support.context, 1, &allocator);
        rclc_executor_add_subscription(
            &executor,
            &cmd_sub,
            &cmd_msg,
            &cmdCallback,
            ON_NEW_DATA);

        node_initialized = true;
        microros_running = true;

        TickType_t lastWakeTime = xTaskGetTickCount();
        const TickType_t frequency = pdMS_TO_TICKS(20); // 50 Hz

        for (;;)
        {
            rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));

            TeensyState tmp;
            memcpy((void*)&tmp, (void*)&teensy_state, sizeof(tmp));

            vehicle_msg.linear.x  = tmp.position;
            vehicle_msg.linear.y  = tmp.velocity;
            vehicle_msg.angular.z = tmp.theta;

            rcl_publish(&vehicle_pub, &vehicle_msg, NULL);

            vTaskDelayUntil(&lastWakeTime, frequency);
        }
    }
}

// =======================================================
// ===================== UART TASK =======================
void UartTeensyTask(void *pvParameters)
{
    SerialSend.begin(921600, SERIAL_8N1, TEENSY_RX_SEND, TEENSY_TX_SEND);
    SerialRecv.begin(921600, SERIAL_8N1, TEENSY_RX_RECV, TEENSY_TX_RECV);

    TeensyState rx;
    EspCommand  tx;

    for (;;)
    {
        // ===== Empfangen =====
        while (SerialRecv.available() >= sizeof(rx))
        {
            SerialRecv.readBytes((uint8_t*)&rx, sizeof(rx));
            memcpy((void*)&teensy_state, (void*)&rx, sizeof(rx));
        }

        // ===== Senden =====
        memcpy((void*)&tx, (void*)&esp_cmd, sizeof(tx));
        SerialSend.write((uint8_t*)&tx, sizeof(tx));

        vTaskDelay(pdMS_TO_TICKS(2)); // ~500 Hz
    }
}

// =======================================================
// ===================== SETUP ===========================
void setup()
{
    Serial.begin(115200);
    delay(2000);

    // Micro-ROS Task auf Core 1
    xTaskCreatePinnedToCore(
        MicroRosTask,
        "MicroRosTask",
        8192,
        NULL,
        1,
        NULL,
        1
    );

    // UART Task auf Core 0
    xTaskCreatePinnedToCore(
        UartTeensyTask,
        "UartTeensyTask",
        8192,
        NULL,
        1,
        NULL,
        0
    );
}

// =======================================================
// ===================== LOOP ===========================
void loop()
{
    // leer – FreeRTOS übernimmt alles
}
