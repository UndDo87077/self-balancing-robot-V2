

#include <Wire.h>
#include "I2Cdev.h"
#include <ODriveUART.h>
#include "MPU6050_6Axis_MotionApps20.h"

// ====== ODrive Setup ======
HardwareSerial& odriveSerial1 = Serial1;
HardwareSerial& odriveSerial2 = Serial2;
ODriveUART odriveM1(odriveSerial1);
ODriveUART odriveM2(odriveSerial2);

// ====== MPU Setup ======
MPU6050 mpu;
bool dmpReady = false;
byte mpuIntStatus;
unsigned int packetSize;
unsigned int fifoCount;
byte fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
float ypr[3];
volatile bool mpuInterrupt = false;
void dmpDataReady() { mpuInterrupt = true; }

// ====== LQR Verstärkungen ======
// Zustandsvektor: [Winkel_Licht, Winkel_Gyro, Geschwindigkeitsfehler, Positionsfehler]
float K1 = 0.21; 
float K3 = 0.035; 
float K4 = 0.22; 
float K5 = 0.555;

// ====== Neue Variablen ======
float position = 0;
unsigned long lastMicros = 0;
float v_soll = 0;

// Kalman-Filter für Winkelabweichung
float theta_prev = 0.0;
float theta_kal = 0.0;
float P = 1.0;
const float Q = 0.01;  // Prozessrauschen
const float R = 0.3;   // Messrauschen

// Kalman-Filter für Postionsabweichung
float position_prev = 0;
float v_pos = 0.0;       // Positionsgeschwindigkeit
float v_pos_kal = 0.0;   // Gefilterte Positionsgeschwindigkeit
float P_v = 1.0;
const float Q_v = 0.01;
const float R_v = 0.3;

void setup() {
  Serial.begin(250000);
  Wire.begin();
  mpu.initialize();

  // MPU Kalibrierwerte (manuell ermittelt)
  mpu.setXGyroOffset(100);
  mpu.setYGyroOffset(119);
  mpu.setZGyroOffset(92);
  mpu.setZAccelOffset(1644);

  // MPU DMP Initialisierung
  if (mpu.dmpInitialize() == 0) {
    mpu.setDMPEnabled(true);
    attachInterrupt(digitalPinToInterrupt(0), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    Serial.println("DMP Init failed!");
  }

  // ODrive Setup
  odriveSerial1.begin(115200);
  odriveSerial2.begin(115200);
  initializeOdrive(odriveM1, odriveSerial1);
  initializeOdrive(odriveM2, odriveSerial2);
}

void initializeOdrive(ODriveUART& odrive, HardwareSerial& serial) {
  delay(100);
  serial.println("w axis0.controller.config.control_mode 1"); // Torque Control
  delay(100);
  while (odrive.getState() == AXIS_STATE_UNDEFINED) {
    delay(100);
  }
  while (odrive.getState() != AXIS_STATE_CLOSED_LOOP_CONTROL) {
    odrive.clearErrors();
    odrive.setState(AXIS_STATE_CLOSED_LOOP_CONTROL);
    delay(10);
  }
}

void loop() {
  if (!dmpReady) return;

  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();
  fifoCount = mpu.getFIFOCount();

  if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
    mpu.resetFIFO();
    Serial.println(F("FIFO overflow!"));
  } else if (mpuIntStatus & 0x02) {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;

    // ====== MPU-Daten auslesen ======
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    float theta = (ypr[1] * 180 / M_PI) - 7.2;            // Pitch mit Offsett

    // ====== Geschwindigkeit aus Motoren ======
    float v_motor1 = odriveM1.getFeedback().vel;
    float v_motor2 = odriveM2.getFeedback().vel;
    float velocity = (v_motor1 - v_motor2) / 2.0; // Mittelwert 

    // ====== Zykluszeit bestimmen ======
    unsigned long now = micros();
    float dt = (now - lastMicros) / 1e6;
    if (dt <= 0) dt = 1e-3;  
    lastMicros = now;

    // ====== Postion ermitteln ======
    position += velocity * dt;

    // ====== Kalman Filter ========
    float theta_raw_kal = (theta - theta_prev) / dt;

    // Prediction step
    P = P + Q;

    // Kalman gain
    float K = P / (P + R);

    // Update step
    theta_kal = theta_kal + K * (theta_raw_kal - theta_kal);
    P = (1 - K) * P;

    // Merke alten theta für nächsten Schritt
    theta_prev = theta;

    // ====== Positionsgeschwindigkeit bestimmen ======
    v_pos = (position - position_prev) / dt;
    position_prev = position;

    // ====== Kalman-Filter für v_pos ======
    P_v = P_v + Q_v;
    float K_v = P_v / (P_v + R_v);
    v_pos_kal = v_pos_kal + K_v * (v_pos - v_pos_kal);
    P_v = (1 - K_v) * P_v;

    // ====== LQR-Steuersignal ======
    float u = -(K1 * theta + K3 * theta_kal+ K4 * -position + K5 * -v_pos_kal);

    // ====== Reglerbegrenzung ======
    u = constrain(u, -5, 5);

    // ====== Motoren ansteuern ======
    odriveM1.setTorque(u);
    odriveM2.setTorque(-u);
    // ====== Debug-Ausgabe (optional) ======
    
    Serial.print("θ: "); Serial.print(theta, 4);
    Serial.print(" | θ_kal: "); Serial.print(theta_kal, 3);
    Serial.print(" | v: "); Serial.print(velocity, 2);
    Serial.print("| θ: "); Serial.print(position, 2);
    Serial.print(" | u: "); Serial.println(u, 2);

    //Serial.print("Raw Pitch (deg): ");
    //Serial.println(ypr[1] * 180 / M_PI);
  
  }
}
