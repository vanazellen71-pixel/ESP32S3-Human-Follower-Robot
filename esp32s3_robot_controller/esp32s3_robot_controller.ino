#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* cameraIp = "192.168.1.200";

float Kp = 0.012;
float Ki = 0.0005;
float Kd = 0.003;

float integral = 0.0;
float lastError = 0.0;
float targetCenterX = 160.0;
float frameCenterX = 160.0;

const int IN1 = 12;
const int IN2 = 13;
const int PWMA = 14;
const int IN3 = 27;
const int IN4 = 26;
const int PWMB = 25;

const int TRIG_PIN = 4;
const int ECHO_PIN = 5;

bool humanDetected = false;
float targetArea = 0.0;

void initMotors() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(PWMA, OUTPUT);
  pinMode(PWMB, OUTPUT);
  stopMotors();
}

void setMotorPWM(int leftSpeed, int rightSpeed) {
  if (leftSpeed >= 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    leftSpeed = -leftSpeed;
  }

  if (rightSpeed >= 0) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
  } else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
    rightSpeed = -rightSpeed;
  }

  analogWrite(PWMA, constrain(leftSpeed, 0, 255));
  analogWrite(PWMB, constrain(rightSpeed, 0, 255));
}

void stopMotors() {
  setMotorPWM(0, 0);
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
}

float getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 999.0;

  float distanceCM = duration * 0.0343 / 2.0;
  return distanceCM;
}

String requestCameraStatus() {
  HTTPClient http;
  String url = "http://" + String(cameraIp) + "/status";
  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();
  return payload;
}

bool parseTarget(String payload) {
  if (payload.length() == 0) return false;

  int idxHuman = payload.indexOf("\"humanDetected\"");
  if (idxHuman < 0) return false;

  int idxCenterX = payload.indexOf("\"centerX\"");
  int idxArea = payload.indexOf("\"area\"");

  if (idxCenterX < 0 || idxArea < 0) return false;

  String humanValue = payload.substring(idxHuman + strlen("\"humanDetected\"") + 1);
  String centerXValue = payload.substring(idxCenterX + strlen("\"centerX\"") + 1);
  String areaValue = payload.substring(idxArea + strlen("\"area\"") + 1);

  humanValue.trim();
  centerXValue.trim();
  areaValue.trim();

  int humanEnd = humanValue.indexOf(',');
  int centerEnd = centerXValue.indexOf(',');
  int areaEnd = areaValue.indexOf('}');

  if (humanEnd > 0) humanValue = humanValue.substring(0, humanEnd);
  if (centerEnd > 0) centerXValue = centerXValue.substring(0, centerEnd);
  if (areaEnd > 0) areaValue = areaValue.substring(0, areaEnd);

  humanValue.replace(":", "");
  centerXValue.replace(":", "");
  areaValue.replace(":", "");

  humanDetected = humanValue.toInt() == 1;
  targetCenterX = centerXValue.toFloat();
  targetArea = areaValue.toFloat();

  return humanDetected;
}

float computeSteering(float error) {
  integral += error;
  float derivative = error - lastError;
  float output = Kp * error + Ki * integral + Kd * derivative;
  lastError = error;
  return constrain(output, -1.0, 1.0);
}

void chatbotStatus(String state) {
  Serial.println("Chatbot: " + state);
}

void followerLogic() {
  float obstacleDist = getDistanceCM();

  if (obstacleDist < 20.0) {
    chatbotStatus("Obstacle ahead. Avoiding.");
    Serial.println("Obstacle ahead! Avoiding...");
    setMotorPWM(-120, -120);
    delay(250);
    setMotorPWM(-120, 120);
    delay(500);
    stopMotors();
    return;
  }

  if (!humanDetected) {
    chatbotStatus("Target not found. Searching.");
    Serial.println("No human detected -> stop and search");
    stopMotors();
    return;
  }

  chatbotStatus("Human detected. Following target.");

  float error = targetCenterX - frameCenterX;
  float steering = computeSteering(error);

  float forward = 0.0;

  if (targetArea < 0.12) {
    forward = 0.60;
  } else if (targetArea > 0.30) {
    forward = -0.35;
  } else {
    forward = 0.0;
  }

  float leftMotor = (forward * 255.0) + (steering * 180.0);
  float rightMotor = (forward * 255.0) - (steering * 180.0);

  leftMotor = constrain(leftMotor, -255, 255);
  rightMotor = constrain(rightMotor, -255, 255);

  setMotorPWM((int)leftMotor, (int)rightMotor);
  Serial.printf("error=%.2f steering=%.2f area=%.4f left=%d right=%d\n",
                error, steering, targetArea, (int)leftMotor, (int)rightMotor);
}

void initUltrasonic() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
}

void setup() {
  Serial.begin(115200);
  initMotors();
  initUltrasonic();
  connectWiFi();
  stopMotors();
  Serial.println("Robot follower ready...");
}

void loop() {
  String payload = requestCameraStatus();

  if (payload.length() > 0) {
    parseTarget(payload);
  } else {
    humanDetected = false;
  }

  followerLogic();
  delay(100);
}
