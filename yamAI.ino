#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FirebaseESP32.h>
#include <WiFi.h>
#include "DHT.h"
#include "yam_model.h"
#include "yam_types.h"
#include "secrets.h"


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ---- Sensor pins ----
#define DHTPIN 3
#define MQ135_PIN 1
#define HEATER_RELAY_PIN 4
#define HUMIDIFIER_RELAY_PIN 0


// constant
#define RL_VALUE 22.0
#define ADC_VREF 3.3
#define ADC_RESOLUTION 4095.0
#define RO_CLEAN_AIR_FACTOR 3.6
#define CO2_CURVE_A 116.6020682
#define CO2_CURVE_B -2.769034857
float RZERO = 76.63; // <-- replace with YOUR sensor's calibrated value
String comment = "Good Enviroment";

// ---- Relay pins ----


// ---- Target band the heater/humidifier try to restore (from the same
//      agronomic thresholds used to label the training data) ----

#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27,16,2);

#define TARGET_TEMP_MIN 26.0
#define TARGET_RH_MIN   44.0


// ---- Timing ----
const unsigned long READ_INTERVAL_MS = 5000;
unsigned long lastRead = 0;
int d=0;

float readSensorResistance() {
  int adcVal = analogRead(MQ135_PIN);
  float voltage = (adcVal / ADC_RESOLUTION) * ADC_VREF;
  if (voltage <= 0.01) voltage = 0.01;
  // return RL_VALUE * (ADC_VREF - voltage) / voltage;
  return adcVal;
}

float readGasPPM() {
  float rs = readSensorResistance();
  float ratio = rs / RZERO;
  // return CO2_CURVE_A * pow(ratio, CO2_CURVE_B);
  return rs;
}


YamDiagnosis diagnoseYam(int predictedClass, float probs[YAM_N_CLASSES],
                          float tempC, float rh, float gasPPM) {
  YamDiagnosis dx;
  dx.diseaseValue = DISEASE_NONE;
  dx.pestValue = PEST_NONE;
  dx.threatLevel = THREAT_NONE;

  float confidence = probs[predictedClass];

  switch (predictedClass) {
    case 0: // Optimal
      dx.threatLevel = THREAT_NONE;
      break;

    case 1: { // Cold / Dehydration stress - correctable, capped below HIGH
      bool severe = (tempC < TARGET_TEMP_MIN - 5.0) || (rh < TARGET_RH_MIN - 15.0);
      dx.threatLevel = severe ? THREAT_MID : THREAT_LOW;
      break;
    }

    case 2: { // Rot risk
      // Sub-rule: very high RH + strong off-gassing suggests active
      // bacterial/wet rot; more moderate readings suggest dry rot.
      if (gasPPM > 900.0 && rh > 88.0) {
        dx.diseaseValue = DISEASE_WET_SOFT_ROT;
      } else {
        dx.diseaseValue = DISEASE_DRY_ROT;
      }
      if (confidence > 0.75) dx.threatLevel = THREAT_HIGH;
      else if (confidence > 0.50) dx.threatLevel = THREAT_MID;
      else dx.threatLevel = THREAT_LOW;
      break;
    }

    case 3: { // Pest risk
      // Sub-rule: warm + comparatively drier favors boring pests
      // (weevil/beetle); warm + very humid favors mealybug. Weak signal —
      // physical inspection is the real confirmation here.
      if (tempC > 27.0 && rh < 75.0) {
        dx.pestValue = PEST_WEEVIL_BEETLE;
      } else {
        dx.pestValue = PEST_MEALYBUG;
      }
      if (confidence > 0.75) dx.threatLevel = THREAT_HIGH;
      else if (confidence > 0.50) dx.threatLevel = THREAT_MID;
      else dx.threatLevel = THREAT_LOW;
      break;
    }
  }

  return dx;
}

// Build the 6-feature vector the model expects, standardize it, run
// softmax regression, return predicted class index (0-3).
int predictClass(float tempC, float rh, float gasPPM, float outProbs[YAM_N_CLASSES]) {
  float raw[YAM_N_FEATURES];
  raw[0] = tempC;
  raw[1] = rh;
  raw[2] = gasPPM;
  raw[3] = (tempC - 16.0f) * (tempC - 16.0f);   // temp_dev_sq
  raw[4] = (rh - 75.0f) * (rh - 75.0f);         // humidity_dev_sq
  raw[5] = tempC * rh;                          // temp_humidity_interaction

  float x[YAM_N_FEATURES];
  for (int i = 0; i < YAM_N_FEATURES; i++) {
    x[i] = (raw[i] - YAM_FEATURE_MEAN[i]) / YAM_FEATURE_STD[i];
  }

  float logits[YAM_N_CLASSES];
  for (int c = 0; c < YAM_N_CLASSES; c++) {
    float z = YAM_BIAS[c];
    for (int i = 0; i < YAM_N_FEATURES; i++) {
      z += YAM_WEIGHTS[c][i] * x[i];
    }
    logits[c] = z;
  }

  // softmax
  float maxLogit = logits[0];
  for (int c = 1; c < YAM_N_CLASSES; c++) if (logits[c] > maxLogit) maxLogit = logits[c];
  float sumExp = 0;
  for (int c = 0; c < YAM_N_CLASSES; c++) {
    outProbs[c] = exp(logits[c] - maxLogit);
    sumExp += outProbs[c];
  }
  int bestClass = 0;
  for (int c = 0; c < YAM_N_CLASSES; c++) {
    outProbs[c] /= sumExp;
    if (outProbs[c] > outProbs[bestClass]) bestClass = c;
  }
  return bestClass;
}

void Onhum()
{
  digitalWrite(HUMIDIFIER_RELAY_PIN,LOW);
  delay(400);
  digitalWrite(HUMIDIFIER_RELAY_PIN,HIGH);
}

void Offhum()
{
  digitalWrite(HUMIDIFIER_RELAY_PIN,LOW);
  delay(400);
  digitalWrite(HUMIDIFIER_RELAY_PIN,HIGH);
  delay(1000);
  digitalWrite(HUMIDIFIER_RELAY_PIN,LOW);
  delay(400);
  digitalWrite(HUMIDIFIER_RELAY_PIN,HIGH);
}

void applyRelayLogic(int predictedClass, const YamDiagnosis &dx, float tempC, float rh) {
  bool heaterOn = false;
  bool humidifierOn = false;

  if (predictedClass == 1) {
    // Cold/Dehydration -> correct toward target band regardless of threat tier
    heaterOn = (tempC < TARGET_TEMP_MIN);
    humidifierOn = (rh < TARGET_RH_MIN);
  } else if (predictedClass == 2 || predictedClass == 3) {
    // Rot or Pest risk -> never add heat/moisture, that feeds the problem
    heaterOn = false;
    humidifierOn = false;
    if (dx.threatLevel >= THREAT_MID) {
      Serial.print("!! ALERT ["); Serial.print(THREAT_NAMES[dx.threatLevel]);
      Serial.println("] — inspect tubers physically and improve ventilation.");
      comment = "!! ALERT [" + String(THREAT_NAMES[dx.threatLevel]) + "] — inspect tubers physically and improve ventilation.";
    }
  }
  // predictedClass == 0 (Optimal): both stay off, handled by defaults above
  if (rh >= 62) {heaterOn = true;}
  else if(rh <= 50){heaterOn = false;}
  digitalWrite(HEATER_RELAY_PIN, heaterOn ? HIGH : LOW);
  if(humidifierOn && d==0) { Onhum(); d=1;}
  else if (d==1 && !humidifierOn) { Offhum(); d=0;}

  Serial.print("Heater: "); Serial.print(heaterOn ? "ON" : "OFF");
  Serial.print("  Humidifier: "); Serial.println(humidifierOn ? "ON" : "OFF");
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin(9,8);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  analogReadResolution(12);

  pinMode(HEATER_RELAY_PIN, OUTPUT);
  pinMode(HUMIDIFIER_RELAY_PIN, OUTPUT);
  digitalWrite(HEATER_RELAY_PIN, LOW);
  digitalWrite(HUMIDIFIER_RELAY_PIN, HIGH);

  Serial.println("Yam storage AI controller starting...");
  Serial.println("Let the MQ135 warm up 20-30+ min before trusting readings.");

  lcd.setCursor(2, 0);
  lcd.print("AI Based Yam");
  lcd.setCursor(1, 1);
  lcd.print("Storage System");
  delay(2000);
  lcd.clear();
  lcd.print("Connecting...");
  lcd.setCursor(4,1);
  lcd.print("to WiFi..");
  delay(1000);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(4,0);
  lcd.print("Connected");
  delay(2000);

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(2048);

}

void loop() {
  // if (millis() - lastRead < READ_INTERVAL_MS) return;
  // lastRead = millis();

  int humidity = dht.readHumidity();
  int tempC = dht.readTemperature();
  int gasPPM = readGasPPM();

  if (isnan(humidity) || isnan(tempC)) {
    Serial.println("Failed to read DHT11 — skipping this cycle.");
    return;
  }
  
  lcd.clear();
  lcd.print("Temp:"); lcd.print(tempC); lcd.print("c  "); lcd.print("RH:"); lcd.print(humidity);
  lcd.setCursor(0,1); lcd.print("Gas:"); lcd.print(gasPPM); lcd.print("ppm");
  delay(1500);



  float probs[YAM_N_CLASSES];
  int predicted = predictClass(tempC, humidity, gasPPM, probs);
  // ---- The 3 output variables you asked for ----
  YamDiagnosis dx = diagnoseYam(predicted, probs, tempC, humidity, gasPPM);
  int diseaseValue = dx.diseaseValue;   // index into DISEASE_NAMES[]
  int pestValue = dx.pestValue;         // index into PEST_NAMES[]
  int threatLevel = dx.threatLevel;     // index into THREAT_NAMES[] (0-3)

  Serial.println("----------------------------------------");
  Serial.print("Temp: "); Serial.print(tempC); Serial.print(" C  ");
  Serial.print("RH: "); Serial.print(humidity); Serial.print(" %  ");
  Serial.print("Gas: "); Serial.print(gasPPM); Serial.println(" ppm");

  Serial.print("ML class: "); Serial.print(YAM_CLASS_NAMES[predicted]);
  Serial.print("  (confidence "); Serial.print(probs[predicted] * 100.0); Serial.println("%)");
  int conf= probs[predicted] * 100.0;
  String ML = String(YAM_CLASS_NAMES[predicted]) +  "(confidence "+ String(conf) + "%)";

  Serial.print("diseaseValue: "); Serial.print(diseaseValue);
  Serial.print(" ("); Serial.print(DISEASE_NAMES[diseaseValue]); Serial.println(")");
  Serial.print("pestValue: "); Serial.print(pestValue);
  Serial.print(" ("); Serial.print(PEST_NAMES[pestValue]); Serial.println(")");
  Serial.print("threatLevel: "); Serial.print(threatLevel);
  Serial.print(" ("); Serial.print(THREAT_NAMES[threatLevel]); Serial.println(")");

  
  lcd.clear();
  lcd.print("Disease:"); lcd.print(DISEASE_NAMES[diseaseValue]);
  lcd.setCursor(0, 1);
  lcd.print("Pest:"); lcd.print(PEST_NAMES[pestValue]);
  delay(1000);

  applyRelayLogic(predicted, dx, tempC, humidity);


  Firebase.setInt(fbdo, "/yamAI/temp", tempC);
  Firebase.setInt(fbdo, "/yamAI/humid", humidity);
  Firebase.setInt(fbdo,"/yamAI/gas",gasPPM);
  Firebase.setString(fbdo, "/yamAI/Disease", DISEASE_NAMES[diseaseValue]);
  Firebase.setString(fbdo,"/yamAI/Pest", PEST_NAMES[pestValue]);
  Firebase.setString(fbdo,"/yamAI/threat", THREAT_NAMES[threatLevel]);
  Firebase.setString(fbdo,"/yamAI/comment", comment);
  Firebase.setString(fbdo,"/yamAI/ML", ML);
}
