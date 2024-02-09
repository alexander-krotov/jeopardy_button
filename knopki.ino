#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <driver/dac.h>

#define LED_BUILTIN 2

#define number_of_74hc595s 2
#define numOfRegisterPins (number_of_74hc595s * 8)

#define SER_Pin 25
#define RCLK_Pin 33
#define SRCLK_Pin 32
#define SPEAKER_PIN 26

// Players key pins
#define KEY_1_Pin 15
#define KEY_2_Pin 4
#define KEY_3_Pin 16
#define KEY_4_Pin 17

// Control panel pins
#define KEY_5_Pin 5
#define KEY_6_Pin 18
#define KEY_7_Pin 19

// LEDs
#define KEY_1_LED 9
#define KEY_2_LED 10
#define KEY_3_LED 11
#define KEY_4_LED 12

#define KEY_RED_LED 14
#define KEY_GREEN_LED 13

#define SEG_A_LED 1
#define SEG_B_LED 5
#define SEG_C_LED 3
#define SEG_D_LED 6
#define SEG_E_LED 0
#define SEG_F_LED 2
#define SEG_G_LED 4
#define SEG_DP_LED 7

// 0-7 - 7-segment display, including dot
// 8-11 - key indicator leds
boolean registers [numOfRegisterPins];

// WiFi AP credentials.
const char *ssid = "knopki";
const char *password = "knopki73";

WiFiServer server(80);

enum {
    STATE_INITIAL,
    STATE_RANDOM_WAIT,
    STATE_TIMER_STARTED,
    STATE_TIMER_ENDED,
    STATE_SHOW_PLAYER,
    STATE_SHOW_FALSE_START
} state;

struct {
  int key_6_delay;
  int key_6_random;
  int key_6_timer;
  int key_7_delay;
  int key_7_random;
  int key_7_timer;

  int timer_pause;
  int display_delay;

  int signal_volume;
} keys_config = {
  0, 0, 7,
  0, 0, 20,
  3, 4,
  8
};

// Player number to print (1-4)
int player_number;

// Time when the last event happend
unsigned int last_event_time;

// Time we turn off the timer
unsigned int timer_off_time;

// Time we wait in the timer.
unsigned int timer_wait_time;

// Time when we switch from RANDOM_WAIT to STARTED
unsigned int start_time;

// Display update needed flag.
bool update_display;

// Time to do the next display update
unsigned int next_update_time;

void beep_1();
void clearRegisters();

// Key press interrupt handler
void IRAM_ATTR ISR()
{
    switch (state) {
      case STATE_INITIAL:
         {
            int delay = 0;
            if (digitalRead(KEY_6_Pin)==LOW) {
              delay = keys_config.key_6_delay*1000+random(keys_config.key_6_random*1000)+1;
              timer_wait_time = keys_config.key_6_timer;
            } else if (digitalRead(KEY_7_Pin)==LOW) {
              delay = keys_config.key_7_delay*1000+random(keys_config.key_7_random*1000)+1;
              timer_wait_time = keys_config.key_7_timer;
            }
            if (delay != 0) {
              last_event_time = millis();
              start_time = last_event_time+delay;
              state = STATE_RANDOM_WAIT;
              update_display = true;
              break;
            }
         }
      case STATE_RANDOM_WAIT:
      case STATE_TIMER_STARTED:
      case STATE_TIMER_ENDED:
            player_number = 0;
            if (digitalRead(KEY_1_Pin)==LOW) {
              player_number = 1;
            } else if (digitalRead(KEY_2_Pin) == LOW) {
              player_number = 2;
            } else if (digitalRead(KEY_3_Pin) == LOW) {
              player_number = 3;
            } else if (digitalRead(KEY_4_Pin) == LOW) {
              player_number = 4;
            }
            if (player_number != 0) {
              if (state == STATE_TIMER_STARTED) {
                state = STATE_SHOW_PLAYER;
              } else {
                state = STATE_SHOW_FALSE_START;
              }
              last_event_time = millis();
              timer_off_time = last_event_time+keys_config.display_delay*1000;
              update_display = true;
              break;
            }
    }
    if (digitalRead(KEY_5_Pin) == 0) {
      // Cleanup key pressed.
      state = STATE_INITIAL;
      update_display = true;
    }
}

void display_updater(int t)
{
  switch (state) {
  case STATE_INITIAL:
      Serial.printf("STATE_INITIAL %d\n", t);
      // Blank all the leds.
      clearRegisters();
      next_update_time = t+10000; // Never update
      break;
  case STATE_RANDOM_WAIT:
      Serial.println("STATE_RANDOM_WAIT");
      if (t < start_time) {
        next_update_time = start_time;
        break;
      }
      timer_off_time = t+timer_wait_time*1000;
      state = STATE_TIMER_STARTED;

  case STATE_TIMER_STARTED:
      Serial.println("STATE_STARTED");
      if (t>=timer_off_time) {
        state = STATE_TIMER_ENDED;
        timer_off_time = t+keys_config.timer_pause*1000;
        update_display = true;
        break;
      }

      setRegisterPin(0, t>timer_off_time+7000);
      setRegisterPin(1, t>timer_off_time+6000);
      setRegisterPin(2, t>timer_off_time+5000);
      setRegisterPin(3, t>timer_off_time+4000);
      setRegisterPin(4, t>timer_off_time+3000);
      setRegisterPin(5, t>timer_off_time+2000);
      setRegisterPin(6, t>timer_off_time+1000);
      setRegisterPin(7, true);
      next_update_time = t+1000;
      break;

  case STATE_TIMER_ENDED:
      Serial.println("STATE_ENDED");
      clearRegisters();
      if (t>=timer_off_time) {
        state = STATE_INITIAL;
        update_display = true;
        break;
      }
      setRegisterPin(SEG_DP_LED, HIGH);
      break;

  case STATE_SHOW_PLAYER:
  case STATE_SHOW_FALSE_START:
      clearRegisters();

      if (t >= timer_off_time) {
        state = STATE_INITIAL;
        break;
      }

      // Display the player number on LEDs (7segment and key leds).
      switch (player_number) {
      case 1:
        setRegisterPin(SEG_C_LED, HIGH);
        setRegisterPin(SEG_B_LED, HIGH);

        setRegisterPin(KEY_1_LED, HIGH);
        break;

      case 2:
        setRegisterPin(SEG_A_LED, HIGH);
        setRegisterPin(SEG_B_LED, HIGH);
        setRegisterPin(SEG_G_LED, HIGH);
        setRegisterPin(SEG_E_LED, HIGH);
        setRegisterPin(SEG_D_LED, HIGH);

        setRegisterPin(KEY_2_LED, 1);
        break;

      case 3:
        setRegisterPin(SEG_A_LED, HIGH);
        setRegisterPin(SEG_B_LED, HIGH);
        setRegisterPin(SEG_C_LED, HIGH);
        setRegisterPin(SEG_D_LED, HIGH);
        setRegisterPin(SEG_G_LED, HIGH);

        setRegisterPin(KEY_3_LED, HIGH);
        break;

      case 4:
        setRegisterPin(SEG_B_LED, HIGH);
        setRegisterPin(SEG_C_LED, HIGH);
        setRegisterPin(SEG_G_LED, HIGH);
        setRegisterPin(SEG_F_LED, HIGH);

        setRegisterPin(KEY_4_LED, HIGH);
        break;
      }
      if (state == STATE_SHOW_PLAYER) {
        // Turn on Green LED
        setRegisterPin(KEY_GREEN_LED, HIGH);
        next_update_time = timer_off_time;

        if (t<last_event_time+100) {
          beep_1();
        }
      } else {
        // Turn on Red LED
        setRegisterPin(KEY_RED_LED, HIGH);

        // Blink the 7-segment display
        if ((t-last_event_time) % 500 > 200) {
          for (int i=0; i<8; i++) {
            setRegisterPin(i, LOW);
          }
        }
        if (t<last_event_time+100 || t>last_event_time+200 && t<last_event_time+300) {
          beep_1();
        }
        next_update_time = t+100;
      }
  }
}

// Blink all the LEDs at startup
void welcome_display()
{
  clearRegisters();

  for (int i=0; i<16; i++) {
    setRegisterPin(i, HIGH);
    writeRegisters();
    delay(100);
  }

  beep_1();

  for (int i=0; i<16; i++) {
    setRegisterPin(i, LOW);
    writeRegisters();
    delay(100);
  }

  beep_1();
  delay(50);
  beep_1();

  clearRegisters();
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  Serial.println();
  Serial.println("Configuring access point...");

  if (!WiFi.softAP(ssid, password)) {
    log_e("Soft AP creation failed.");
    while(1);
  }

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.begin();

  Serial.println("Server started");

  pinMode(SER_Pin, OUTPUT);
  pinMode(RCLK_Pin, OUTPUT);
  pinMode(SRCLK_Pin, OUTPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  dac_output_enable(DAC_CHANNEL_2);
  clearRegisters();
  writeRegisters();

  pinMode(KEY_1_Pin, INPUT);
  pinMode(KEY_2_Pin, INPUT);
  pinMode(KEY_3_Pin, INPUT);
  pinMode(KEY_4_Pin, INPUT);
  pinMode(KEY_5_Pin, INPUT);
  pinMode(KEY_6_Pin, INPUT);
  pinMode(KEY_7_Pin, INPUT);

  attachInterrupt(KEY_1_Pin, ISR, FALLING);
  attachInterrupt(KEY_2_Pin, ISR, FALLING);
  attachInterrupt(KEY_3_Pin, ISR, FALLING);
  attachInterrupt(KEY_4_Pin, ISR, FALLING);
  attachInterrupt(KEY_5_Pin, ISR, FALLING);
  attachInterrupt(KEY_6_Pin, ISR, FALLING);
  attachInterrupt(KEY_7_Pin, ISR, FALLING);

  welcome_display();
}

const uint8_t sineLookupTable[] = {
0, 0, 1, 2, 4,
6, 9, 12, 16, 20, 24, 29, 35,
40, 46, 53, 59, 66, 73, 81, 88,
96, 104, 112, 119,
128, 136, 143, 151, 159, 167, 174, 182,
189, 196, 202, 209, 215, 220, 226, 231,
235, 239, 243, 246, 249, 251, 253, 254,
255, 255, 255, 254, 253, 251, 249, 246,
243, 239, 235, 231, 226, 220, 215, 209,
202, 196, 189, 182, 174, 167, 159, 151,
143, 136, 128, 119, 112, 104, 96, 88,
81, 73, 66, 59, 53, 46, 40, 35,
29, 24, 20, 16, 12, 9, 6, 4, 2, 1, 0
};

void beep_signal()
{
  for (int i=0; i<100; i++) {
    dac_output_voltage(DAC_CHANNEL_2, sineLookupTable[i]*keys_config.signal_volume/8);
  }
}

void beep_1()
{
  for (int i=0; i<200; i++) {
    beep_signal();
  }
}

void loop()
{
  unsigned int t = millis();
  serve_wifi_client();
  if (update_display || t > next_update_time) {
    Serial.printf("loop %d %d\n", update_display, t);
    display_updater(t);
    writeRegisters();
    update_display = false;
  }
}

void serve_wifi_client()
{
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("New Client.");          // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // the content of the HTTP response follows the header:
            client.print("Click <a href=\"/H\">here</a> to turn ON the LED.<br>");
            client.print("Click <a href=\"/L\">here</a> to turn OFF the LED.<br>");

            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Check to see if the client request was "GET /H" or "GET /L":
        if (currentLine.endsWith("GET /H")) {
          digitalWrite(LED_BUILTIN, HIGH);               // GET /H turns the LED on
        }
        if (currentLine.endsWith("GET /L")) {
          digitalWrite(LED_BUILTIN, LOW);                // GET /L turns the LED off
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }
}

void clearRegisters()
{
  for (int i=0; i<8; i++){
     registers[i] = HIGH;
  }
  for (int i=8; i<16; i++){
     registers[i] = LOW;
  }
}

void writeRegisters()
{
  // Write register after being set
  digitalWrite(RCLK_Pin, LOW);
  for (int i = numOfRegisterPins-1; i>=0; i--){
    digitalWrite(SRCLK_Pin, LOW);
    int val = registers[i];
    digitalWrite(SER_Pin, val);
    digitalWrite(SRCLK_Pin, HIGH);
  }
  digitalWrite(RCLK_Pin, HIGH);
}

void setRegisterPin(int index, int value)
{
  // First 8 pins (7-segment indicator) have reverse logic
  if (index<8) {
    value=!value;
  }
  registers[index] = value;
}
