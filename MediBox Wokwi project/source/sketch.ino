// EN2853 - Embedded Systems: Programming Assignment 2
// Index No.  : 210088L
// Name       : Cooray P.L.L.K.
//------------------------------------------------------------------------------

// The red LED lights up if temperature or humidity are not in acceptable ranges.
// The blue LED turns on along with the alarm.

// Include required libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// Define OLED screen parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Define pin assignments
#define BUZZER 5
#define LED_1 15
#define LED_2 2
#define PB_CANCEL 34
#define PB_OK 32
#define PB_UP 33
#define PB_DOWN 35
#define DHTPIN 19

#define LDR_left A0
#define LDR_right A3
#define SERVMO 18

// required variables for light intensity
float GAMMA = 0.75;
const float RL10 = 50;
float MIN_ANGLE = 30;
float lux = 0;
String highestLuxLDR;

// Define NTP server and time offset
#define NTP_SERVER    "pool.ntp.org" 
float UTC_OFFSET = 0;
#define UTC_OFFSET_DST 0
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Wifi and mqtt clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// to store light and temperature data to send through MQTT
char tempArr[10];
char lightArr[10];


bool isScheduledON = false;
unsigned long scheduledOnTime;

// Objects initialization
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;

// servo moter initializing position
int pos = 0;
Servo servo;

// Initialize variables for time, alarms, and modes
int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;

bool alarm_enabled = true;
int num_alarms = 3;
int alarm_hours[] = {0, 0, 0};
int alarm_minutes[] = {0, 0, 0};
bool alarm_triggered[] = {false, false, false};

// Initialize variables for musical notes
int num_notes = 8;
int C = 262;
int D = 294;
int E = 330;
int F = 349;
int G = 392;
int A = 440;
int B = 494;
int C_H = 523;
int notes[] = {C, D, E, F, G, A, B, C_H};

// Initialize variables for menu navigation
int current_mode = 0;
int max_mode = 5;
String modes[] = {"1 - Set Time", "2 - Set Alarm 1", "3 - Set Alarm 2", "4 - Set Alarm 3", "5 - Disable All Alarms"};


void setup() {
  /* Setup function - runs once at startup*/
  // Set pin modes
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(PB_CANCEL, INPUT);
  pinMode(PB_OK, INPUT);
  pinMode(PB_UP, INPUT);
  pinMode(PB_DOWN, INPUT);

  // Initialize DHT sensor
  dhtSensor.setup(DHTPIN, DHTesp::DHT22);

  servo.attach(SERVMO, 500, 2400);

  // Initialize OLED display
  Serial.begin(9600);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); 
  }
  display.display();
  delay(1000); 

  // Connect to WiFi
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    display.clearDisplay();
    print_line("Connecting to WIFI", 0, 0, 2);
  }
  // Setting up MQTT
  mqttClient.setServer("test.mosquitto.org", 1883);
  mqttClient.setCallback(recieveCallback);

  display.clearDisplay();
  print_line("Connected to WIFI", 0, 0, 2);

  timeClient.begin();
  timeClient.setTimeOffset(UTC_OFFSET*3600);
  // Configure NTP time synchronization
  configTime(UTC_OFFSET * 3600, UTC_OFFSET_DST, NTP_SERVER);

  // Display welcome message
  display.clearDisplay();
  print_line("Welcome to Medibox!", 2, 0, 2); 
  delay(1000);
  display.clearDisplay();

}

void loop() {
  /*Main program loop*/

  // connect with mqtt broker
  if(!mqttClient.connected()){
    connectToBroker();
  }
  mqttClient.loop();
  // publish topics with data values to the  mqtt broker
  mqttClient.publish("LK-LIGHT-INTENSITY", lightArr);
  mqttClient.publish("LK-TEMPERATURE-VALUE", tempArr);
  

  // Update time and check for alarms
  update_time_with_check_alarms();

  // Check for button press to enter menu
  if (digitalRead(PB_OK) == LOW) {
    delay(50);
    display.clearDisplay();
    print_line("MENU", 40, 20, 2);
    delay(900);
    display.clearDisplay();
    go_to_menu();
  }

  // Check temperature and humidity
  check_temp();
  /*newly added function calls*/

  readLDR() ;
  servMot();
  check_schedule();
}



void recieveCallback(char* topic, byte* payload, unsigned int length) {
  /*This function is the callback handler for MQTT message reception. It processes incoming MQTT messages,
extracts relevant data from the payload, and updates global variables accordingly.*/

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char payloadCharAr[length];
  for (int i = 0; i < length; i++){
    Serial.print((char)payload[i]);
    payloadCharAr[i] = (char)payload[i];
  }
  Serial.print("\n");
 
  // receive minimum angle via mqtt
  if (strcmp(topic, "LK-MIN-ANGLE") == 0){
    MIN_ANGLE = atoi(payloadCharAr);
  }
  // receive control factor via mqtt
  if (strcmp(topic, "LK-CONTROL-FACTOR") == 0) {
    GAMMA = atof(payloadCharAr);
  }
  // receive main switch on/off status via mqtt
  if (strcmp(topic, "LK-ON/OFF-ESP") == 0) {
    buzzerOn(payloadCharAr[0] == 't');
  }
  // receive scheduled time via mqtt
  if(strcmp(topic, "LK-ON/OFF-SCH") == 0){
    if(payloadCharAr[0] =='N'){
      isScheduledON = false;
    }else{
      isScheduledON = true;
      scheduledOnTime = atol(payloadCharAr);
    }
  }

}

void connectToBroker() {
  /*This function attempts to establish a connection with the MQTT broker. It enters a loop
until the connection is successfully established.*/
  while(!mqttClient.connected()){
    Serial.println("Attempting MQTT connection");
    if(mqttClient.connect("ESP32-1egfiaufafifwf")){
      Serial.println("MQTT Connected");
      mqttClient.subscribe("LK-MIN-ANGLE");
      mqttClient.subscribe("LK-CONTROL-FACTOR");
      mqttClient.subscribe("LK-ON/OFF-ESP");
      mqttClient.subscribe("LK-SCH-TIME-ON");
    }else{
      Serial.println("failed to connect");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void readLDR() {
  /* read values of two ldr sensores and calculate the light intensity*/
  int analogVal_1 = analogRead(LDR_left);
  int analogVal_2 = analogRead(LDR_right);
  
  if (analogVal_1 < analogVal_2){
     //calculate light intensity in 0-1 range
    float voltage = analogVal_1 / 1024. * 5;
    float resistance = 2000 * voltage / (1 - voltage / 5);
    float maxlux = pow(RL10 * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
    lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA))/maxlux;
    String(lux).toCharArray(lightArr,10);
    highestLuxLDR = "LDR_left";

  } else{
    //calculate light intensity in 0-1 range
    float voltage = analogVal_2 / 1024. * 5;
    float resistance = 2000 * voltage / (1 - voltage / 5);
    float maxlux = pow(RL10 * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
    lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA))/maxlux;
    String(lux).toCharArray(lightArr,10);
    highestLuxLDR = "LDR_Right";

  }
   //Serial.println(lightArr);
  mqttClient.publish("HighestLux_LDR", highestLuxLDR.c_str());


}

void servMot(){
  /*calculate the position of servo motor*/
  int D;
  float maxVal = 180;
  if (highestLuxLDR == "LDR_Left"){
    D = 1.5;
    pos = min(MIN_ANGLE * D + (180 - MIN_ANGLE) * lux * GAMMA,maxVal);
  }else{
    D = 0.5;
    pos = min(MIN_ANGLE * D + (180 - MIN_ANGLE) * lux * GAMMA,maxVal);
  }
  servo.write(pos);
}

void buzzerOn(bool on){
  if(on){
    tone(BUZZER, C);
  }
  else{
    noTone(BUZZER);
  }
}

unsigned long getTime(){
  timeClient.update();
  return timeClient.getEpochTime();
}

void check_schedule() {
  // set the buzzer to tone when scheduled time comes
  if (isScheduledON){
    unsigned long currentTime = getTime();
    if (currentTime > scheduledOnTime) {
      buzzerOn(true);
      isScheduledON = false;
      mqttClient.publish("ON/OFF-ESP", "1");
      mqttClient.publish("ON/OFF-SCH", "0");
      Serial.println("Scheduled ON");
      Serial.println(currentTime);
      Serial.println(scheduledOnTime);
    }
  }
}

/*-------------------End of Assignment 02----------------------*/

void print_line(String text, int column, int row, int text_size) {
  /*Function to print a line of text on the OLED display*/
  display.setTextSize(text_size);		
  display.setTextColor(SSD1306_WHITE);	
  display.setCursor(column, row);		
  display.println(text);
  display.display();
  
}

void print_time_now(void) {
  /*Function to print current time on OLED display*/
  display.clearDisplay();
  print_line(String(hours), 30, 0, 2);
  print_line(":", 50, 0, 2);
  print_line(String(minutes), 60, 0, 2);
  print_line(":", 80, 0, 2);
  print_line(String(seconds), 90, 0, 2);
}

void update_time() {
  /*Function to update time from NTP server*/

  // Declare a struct tm variable to hold time information
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  hours = atoi(timeHour);

  char timeMinute[3];
  strftime(timeMinute, 3, "%M", &timeinfo);
  minutes = atoi(timeMinute);

  char timeSecond[3];
  strftime(timeSecond, 3, "%S", &timeinfo);
  seconds = atoi(timeSecond);

  char timeDay[3];
  strftime(timeDay, 3, "%d", &timeinfo);
  days = atoi(timeDay);

}

void ring_alarm() {
  /*Function to ring alarm*/

  // Display alarm message and turn on LED_1
  display.clearDisplay();
  print_line("Medicine  Time!", 0, 0, 2);
  digitalWrite(LED_1, HIGH);

  // Play alarm tone
  bool temp = false;
  while (digitalRead(PB_CANCEL) == HIGH && temp == false) {

    for (int i = 0; i < num_notes; i++) {
      if (digitalRead(PB_CANCEL) == LOW) {
        delay(200);
        temp = true;
        break;
      }
      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
      delay(2);
    }
  }
  digitalWrite(LED_1, LOW);
  display.clearDisplay();

}

int wait_for_button_press() {
  /*Function to wait for button press*/

  while (true) {
    if (digitalRead(PB_UP) == LOW) {
      delay(200);
      return PB_UP;
    }
    else if (digitalRead(PB_DOWN) == LOW) {
      delay(200);
      return PB_DOWN;
    }
    else if (digitalRead(PB_OK) == LOW) {
      delay(200);
      return PB_OK;
    }
    else if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      return PB_CANCEL;
    }
    update_time();
  }
}

void go_to_menu() {
  /* Function to navigate through the menu*/

  while (digitalRead(PB_CANCEL) == HIGH) {
    display.clearDisplay();
    // Display current mode in the menu
    print_line(modes[current_mode], 0, 0, 2);

    // Wait for button press and navigate through menu options
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      current_mode += 1;
      current_mode = current_mode % max_mode;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_mode - 1;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      run_mode(current_mode);
    }
  }
}

void run_mode(int mode) {
  /*Function to execute selected mode from the menu*/
  if (mode == 0) {
    set_time();
  }
  else if (mode == 1 || mode == 2 || mode == 3) {
    set_alarm(mode - 1);
  }
  else if (mode == 4) {
    alarm_enabled = false;
    display.clearDisplay();
    print_line("Alarms Are Disabled", 0, 0, 2);
    delay(2000);
  }

}

void update_time_with_check_alarms() {
  /*Function to update time and check for alarms*/

  update_time();
  print_time_now();

  if (alarm_enabled == true) {
    // Check each alarm
    for (int i = 0; i < num_alarms; i++) {
      // If the alarm is triggered, ring the alarm
      if (alarm_triggered[i] == false && alarm_hours[i] == hours && alarm_minutes[i] == minutes) {
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}



void set_alarm(int alarm) {
  /*Function to set an alarm*/

  display.clearDisplay();
  int temp_hour = alarm_hours[alarm];
  
  // Loop for setting the hour with button presses
  while (true) {
    display.clearDisplay();
    print_line("Enter hour:" + String(temp_hour), 0, 0, 2);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      // Increase hour if up button pressed
      delay(200);
      temp_hour += 1;
      temp_hour = temp_hour % 24; // Keep within 0-23 range
    }
    else if (pressed == PB_DOWN) {
      // Decrease hour if down button pressed
      delay(200);
      temp_hour -= 1;
      if (temp_hour <= 0) {
        temp_hour = 23;
      }
    }
    else if (pressed == PB_OK) {
      // Set the alarm hour and exit the loop if OK button pressed
      delay(200);
      alarm_hours[alarm] = temp_hour;
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
    // Set the alarm hour and exit the loop if OK button pressed
    display.clearDisplay();
    print_line(String(temp_hour), 0, 50, 2);

  }

  int temp_minute = alarm_minutes[alarm];
  display.clearDisplay();

  // Loop for setting the minute with button presses
  while (true) {
    display.clearDisplay();
    print_line("Enter minute :" + String(temp_minute), 0, 0, 2);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      temp_minute += 1;
      temp_minute = temp_minute % 60; // Keep within 0-59 range
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      temp_minute -= 1;
      if (temp_minute <= 0) {
        temp_minute = 59;
      }

    }
    else if (pressed == PB_OK) {
      // Set the alarm minute and exit the loop
      delay(200);
      alarm_minutes[alarm] = temp_minute;
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
    display.clearDisplay();
    print_line(String(temp_hour), 0, 50, 2);
    print_line(":", 30, 50, 2);
    print_line(String(temp_minute), 40, 50, 2);
  }
  // Display confirmation message after setting the alarm
  display.clearDisplay();
  print_line("Alarm is set:", 0, 0, 2);
  delay(200);
  print_line(String(alarm_hours[alarm]), 0, 50, 2);
  print_line(String(":"), 30, 50, 2);
  print_line(String(alarm_minutes[alarm]), 40, 50, 2);
  delay(2000);
}


void set_time() {
  /*Function to set the device time*/
  display.clearDisplay();
  int temp_hour = UTC_OFFSET;

  // Loop for setting the hour with button presses
  while (true) {
    display.clearDisplay();
    print_line("Enter UTC Offset hour:" + String(temp_hour), 0, 0, 2);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) { 
      // Increase hour if upbutton pressed
      delay(200);
      temp_hour += 1;
      temp_hour = temp_hour % 12;
    }
    else if (pressed == PB_DOWN) {
      // Decrease hour if down button pressed
      delay(200);
      temp_hour -= 1;
      if (temp_hour < -12) {
        temp_hour = 12;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
    // Print the temporary hour value on the display
    display.clearDisplay();
    print_line(String(temp_hour), 0, 50, 2);
  }

  int temp_minute = 0;
  display.clearDisplay();

  // Loop for setting the minute with button presses
  while (true) {
    display.clearDisplay();
    print_line("Enter UTC Offset minute :" + String(temp_minute), 0, 0, 2);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      // Increase minute if up button pressed
      delay(200);
      temp_minute += 5;
      temp_minute = temp_minute % 60;
    }
    else if (pressed == PB_DOWN) {
      // Decrease minute if down button pressed
      delay(200);
      temp_minute -= 5;
      if (temp_minute < 0) {
        temp_minute = 55;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      UTC_OFFSET = temp_hour + temp_minute / 60;
      timeClient.setTimeOffset(UTC_OFFSET*3600);
      configTime(UTC_OFFSET * 3600, UTC_OFFSET_DST, NTP_SERVER);
      delay(200);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
    // Print the temporary minute value on the display
    display.clearDisplay();
    print_line(String(temp_hour), 0, 50, 2);
    print_line(":", 30, 50, 2);
    print_line(String(temp_minute), 40, 50, 2);
  }
  // Display confirmation message after setting UTC offset
  display.clearDisplay();
  print_line("UTC OFFSET is set", 0, 0, 2);
  delay(2000);

}


void check_temp() {
  /* Function to check temperature and humidity
  Healthy Temperature : 26 ◦C ≤ Temperature ≤ 32 ◦C
  Healthy Humidity : 60% ≤ Humidity ≤ 80% */
  
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  if (data.temperature > 32) {
    print_line("TEMP HIGH", 0, 40, 1);
    digitalWrite(LED_2, HIGH);
  }
  else if (data.temperature < 26) {
    print_line("TEMP LOW", 0, 40, 1);
    digitalWrite(LED_2, HIGH);
  }
  else {
    digitalWrite(LED_2, LOW);
  }
  if (data.humidity > 80) {
    print_line("HUMIDITY HIGH", 0, 50, 1);
    digitalWrite(LED_2, HIGH);
  }
  else if (data.humidity < 60) {
    print_line("HUMIDITY LOW", 0, 50, 1);
    digitalWrite(LED_2, HIGH);
  }
  else {
    digitalWrite(LED_2, LOW);
  }
  String(data.temperature,2).toCharArray(tempArr, 10);
}