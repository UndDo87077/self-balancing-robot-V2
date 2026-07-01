//Teensy 4.0

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
const int MPU_INT_PIN = 20;   // MPU6050 Interupt an Teensy Pin 20
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
float K1 = 0.4;
float K3 = 0.035;
float K4 = 0.55;
float K5 = 0.77;

// ====== Variablen ======
float position = 0;
unsigned long lastMicros = 0;
bool theta_initialized = false;
unsigned long imuFaultUntil = 0;

// Sollwerte
float target_angle = 0.0;
float target_position = 0.0;
float tmp_target_position = 0.0;

// Winkelrate
float theta_prev = 0.0;

// Kalman-Filter für Positionsgeschwindigkeit
float v_pos_kal = 0.0;
float P_v = 1.0;
const float Q_v = 0.01;
const float R_v = 0.3;

/* ENTFERNT: Kalman-Filter für Winkelabweichung (theta_kal)
   - DMP liefert bereits gefilterten Winkel
   - Kalman auf Ableitung erzeugte Phasenverzug → Aufschaukeln
float theta_kal = 0.0;
float P = 1.0;
const float Q = 0.01;
const float R = 0.3;
*/

/* ENTFERNT: Positionsgeschwindigkeit durch doppelte Ableitung
   - position_prev wurde genutzt um v_pos = (position - position_prev)/dt zu berechnen
   - Das ist doppelte Ableitung von verrauschten UART-Werten → Rauschen explodiert
float position_prev = 0;
float v_pos = 0.0;
*/

void setup() {
  Serial.begin(250000);
  Wire.begin();
  Wire.setClock(400000);
  mpu.initialize();
  mpu.setRate(4);  // ~200 Hz

  // MPU Kalibrierwerte
  mpu.setXGyroOffset(100);
  mpu.setYGyroOffset(119);
  mpu.setZGyroOffset(92);
  mpu.setZAccelOffset(1644);

  // MPU DMP Initialisierung
  if (mpu.dmpInitialize() == 0) {
    mpu.setDMPEnabled(true);
    pinMode(MPU_INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(MPU_INT_PIN), dmpDataReady, RISING);
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

  mpu.resetFIFO();
  mpuInterrupt = false;
  theta_initialized = false;
  lastMicros = micros();
}

void initializeOdrive(ODriveUART& odrive, HardwareSerial& serial) {
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
  if (!dmpReady) return; // MPU ready


  if (!mpuInterrupt) return; // Regelung läuft nur mit neuen MPU Daten
  mpuInterrupt = false;

  mpuIntStatus = mpu.getIntStatus();
  fifoCount = mpu.getFIFOCount();


  /*if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
    mpu.resetFIFO();
    Serial.println("FIFO OVERFLOW!");
    // FIX: return statt weitermachen
    // Vorher lief die Loop nach FIFO-Reset weiter mit alten fifoBuffer-Daten
    // → Regler bekam veralteten Winkel → Ruckeln
    return;*/
if ((mpuIntStatus & 0x10) || fifoCount >= 1024) {
  Serial.println("FIFO OVERFLOW!");

  mpu.resetFIFO();
  mpuInterrupt = false;
  theta_initialized = false;
  lastMicros = micros();

  odriveM1.setTorque(0);
  odriveM2.setTorque(0);

  return;

} else if (mpuIntStatus & 0x02) {
  if (fifoCount < packetSize) {
  return;
  }
  while (fifoCount >= packetSize) {
    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;
  }

  // ab hier MPU-Daten auslesen

    // ====== MPU-Daten auslesen ======
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    float q_norm = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;

if (q_norm < 0.8 || q_norm > 1.2) {
  Serial.print("BAD QUAT! norm = ");
  Serial.println(q_norm);

  mpu.resetFIFO();
  mpuInterrupt = false;
  theta_initialized = false;
  lastMicros = micros();

  odriveM1.setTorque(0);
  odriveM2.setTorque(0);

  return;
}
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    float theta = (ypr[1] * 180 / M_PI) - 2.83; // Pitch in Grad

    // ====== Geschwindigkeit aus Motoren ======
    float v_motor1 = odriveM1.getFeedback().vel;
    float v_motor2 = odriveM2.getFeedback().vel;
    float velocity = (v_motor1 - v_motor2) / 2.0;

    // ====== Zykluszeit bestimmen ======
    unsigned long now = micros();
    float dt = (now - lastMicros) / 1e6;
    if (dt <= 0) dt = 1e-3;
    lastMicros = now;

    // ====== Position ermitteln ======
    position += velocity * dt;

    // ====== Winkelrate direkt ableiten ======
    // FIX: Vorher wurde Kalman auf (theta - theta_prev)/dt angewendet
    // Das filterte die Winkelrate, nicht den Winkel selbst → falsche Einheit im LQR
    // Außerdem erzeugte der zusätzliche Filter Phasenverzug → Aufschaukeln
    /*float theta_dot = (theta - theta_prev) / dt;
    theta_prev = theta;*/
    if (!theta_initialized) {
  theta_prev = theta;
  theta_initialized = true;
  return;
}

float dtheta = theta - theta_prev;

// Schutz gegen Sprung über ±180°
if (dtheta > 180.0) dtheta -= 360.0;
if (dtheta < -180.0) dtheta += 360.0;

float theta_dot = dtheta / dt;
theta_prev = theta;

    // ====== Positionsgeschwindigkeit direkt aus velocity ======
    // FIX: Vorher: v_pos = (position - position_prev) / dt
    // Das ist doppelte Ableitung von verrauschten UART-Werten → Rauschen explodiert
    // Jetzt: velocity direkt verwenden, nur einmal filtern
    P_v = P_v + Q_v;
    float K_v = P_v / (P_v + R_v);
    v_pos_kal = v_pos_kal + K_v * (velocity - v_pos_kal);
    P_v = (1 - K_v) * P_v;

    // ====== LQR-Steuersignal ======
    float u = -(K1 * theta + K3 * theta_dot + K4 * -position + K5 * -v_pos_kal);
    u = constrain(u, -3, 3);

    // ====== Motoren ansteuern ======

    float u1 = u* 1.5;  //Ausgleich Motorengeschwindigkeit
    float u2 = -u;

    odriveM1.setTorque(u1);
    odriveM2.setTorque(u2);

    // ====== Debug-Ausgabe ======
    static int printCounter = 0;
    if (printCounter++ >= 10) {
      /*Serial.print("θ: ");      Serial.print(theta, 2);
        Serial.print(" | td: ");  Serial.print(theta_dot, 2);
        Serial.print(" | vk: ");  Serial.print(v_pos_kal, 2);
        Serial.print(" | u: ");   Serial.println(u, 3);
        */

      printCounter = 0;
    }
  }
}
