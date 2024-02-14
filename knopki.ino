// Software part for Jeopardy-like game project.
//
// Project docs, hardware and license:
// https://github.com/alexander-krotov/jeopardy_button

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <driver/dac.h>

#include <GyverPortal.h>
#include <EEPROM.h>

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

// 7-segment display:
//   F
//  E A
//   G
//  D B
//   C
#define SEG_A_LED 1
#define SEG_B_LED 5
#define SEG_C_LED 3
#define SEG_D_LED 6
#define SEG_E_LED 0
#define SEG_F_LED 2
#define SEG_G_LED 4
#define SEG_DP_LED 7

// Web interface
GyverPortal ui;

// 0-7 - 7-segment display, including dot
// 8-11 - key indicator leds
boolean registers [numOfRegisterPins];

// WiFi AP credentials.
const char *ssid = "knopki";
const char *password = "knopki73";

// System states
enum {
    STATE_INITIAL,
    STATE_RANDOM_WAIT,
    STATE_TIMER_STARTED,
    STATE_TIMER_ENDED,
    STATE_SHOW_PLAYER,
    STATE_SHOW_FALSE_START
} state;

// Coniguration parameters.
struct {
  // Timer 1
  int key_6_delay;    // INITIAL -> TIMER_STARTED
  int key_6_random;   // INITIAL -> TIMER_STARTED`
  int key_6_timer;    // TIMER_STARTED -> TIMER_ENDED

  // Timer 2
  int key_7_delay;    // INITIAL -> TIMER_STARTED
  int key_7_random;   // INITIAL -> TIMER_STARTED
  int key_7_timer;    // TIMER_STARTED -> TIMER_ENDED

  int timer_pause;    // TIMER_ENDED -> INITIAL
  int display_delay;  // SHOW_PLAYER, SHOW_FALSE_START -> INITIAL

  int signal_volume;  // Sound signal volume (0-8)

  int countdown_beep;  // Signal countdown last seconds in TIMER_STARTED
} keys_config = {
  0, 0, 7,
  0, 0, 20,
  3, 4,
  8,
  3
};

// Player number to print (1-4)
int player_number;

// Time when the last event happend
unsigned int last_event_time;

// Time to do the next display update
unsigned int next_update_time;

// Time we turn off the timer (set from key_6_delay or key_7_delay)
unsigned int timer_off_time;

// Time we wait in the timer (TIMER_STARTED->TIMER_ENDED)
unsigned int timer_wait_time;

// Time when we switch from RANDOM_WAIT to STARTED (RANDOM_WAIT->TIMER_STARTED)
unsigned int start_time;

// Display update needed flag.
bool update_display;

// Key press interrupt handler
void IRAM_ATTR ISR()
{
    switch (state) {
      case STATE_INITIAL:
         {
            int delay = 0; // Timer delay in ms.
            if (digitalRead(KEY_6_Pin)==LOW) {
              delay = keys_config.key_6_delay*1000+random(keys_config.key_6_random*1000)+1;
              timer_wait_time = keys_config.key_6_timer;
            } else if (digitalRead(KEY_7_Pin)==LOW) {
              delay = keys_config.key_7_delay*1000+random(keys_config.key_7_random*1000)+1;
              timer_wait_time = keys_config.key_7_timer;
            }
            if (delay != 0) {
              last_event_time = millis();
              start_time = last_event_time+delay-1;
              state = STATE_RANDOM_WAIT;
              update_display = true;
              break;
            }
         }
      case STATE_RANDOM_WAIT:
      case STATE_TIMER_STARTED:
      case STATE_TIMER_ENDED:
            player_number = 0;
            if (digitalRead(KEY_1_Pin) == LOW) {
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
    if (digitalRead(KEY_5_Pin) == LOW) {
      // Cleanup key pressed.
      state = STATE_INITIAL;
      update_display = true;
    }
}

// Update the display leds, according to the current system state
// and current time in milliseconds (parameter t).
// It uses global next_update_time to say it wants to be woken up
// next time, in case no state changes were made.
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
    {
      Serial.println("STATE_STARTED");
      int time_left = timer_off_time-t;
      if (time_left <=0) {
        state = STATE_TIMER_ENDED;
        timer_off_time = t+keys_config.timer_pause*1000;
        update_display = true;
        break;
      }

      // Show remaining time in the 7-segment display.
      // The segments will dissapear one by one, every second.
      setRegisterPin(SEG_E_LED, time_left>=7000);
      setRegisterPin(SEG_A_LED, time_left>=6000);
      setRegisterPin(SEG_D_LED, time_left>=5000);
      setRegisterPin(SEG_B_LED, time_left>=4000);
      setRegisterPin(SEG_F_LED, time_left>=3000);
      setRegisterPin(SEG_G_LED, time_left>=2000);
      setRegisterPin(SEG_C_LED, time_left>=1000);

      // Last three seconds - make a short beep every second.
      if (time_left<=keys_config.countdown_beep*1000) {
        beep_1();
      }
      next_update_time = t+1000;
      break;
    }
  case STATE_TIMER_ENDED:
      Serial.println("STATE_ENDED");
      clearRegisters();
      beep_1();
      beep_1();
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

        if (t < last_event_time+100) {
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

// Create a configuration form
void build()
{
  Serial.println("BUILD");

  GP.BUILD_BEGIN();
  GP.THEME(GP_DARK);
  GP.FORM_BEGIN("/");

  GP_MAKE_BLOCK_TAB(
    "Timer 1",
    GP_MAKE_BOX(GP.LABEL("Time:"); GP.NUMBER("key_6_timer", "", keys_config.key_6_timer););
    GP_MAKE_BOX(GP.LABEL("Fixed delay:"); GP.NUMBER("key_6_delay", "", keys_config.key_6_delay););
    GP_MAKE_BOX(GP.LABEL("Random delay:"); GP.NUMBER("key_6_random", "", keys_config.key_6_random););
  );
  GP_MAKE_BLOCK_TAB(
    "Timer 2",
    GP_MAKE_BOX(GP.LABEL("Time:"); GP.NUMBER("key_7_timer", "", keys_config.key_7_timer););
    GP_MAKE_BOX(GP.LABEL("Fixed delay:"); GP.NUMBER("key_7_delay", "", keys_config.key_7_delay););
    GP_MAKE_BOX(GP.LABEL("Random delay:"); GP.NUMBER("key_7_random", "", keys_config.key_7_random););
  );
  GP_MAKE_BOX(GP.LABEL("Timer pause:"); GP.NUMBER("timer_pause", "", keys_config.timer_pause););
  GP_MAKE_BOX(GP.LABEL("Display time:"); GP.NUMBER("display_delay", "", keys_config.display_delay););
  GP_MAKE_BOX(GP.LABEL("Signal volume:"); GP.NUMBER("signal_volume", "", keys_config.signal_volume););
  GP_MAKE_BOX(GP.LABEL("Countdown beep"); GP.NUMBER("countdown_beep", "", keys_config.countdown_beep););

  GP.SUBMIT("UPDATE");

  GP.FORM_END();
  GP.BUILD_END();
}

// Read the configuration from a form
void action(GyverPortal& p)
{
  Serial.println("ACTION");

  if (p.form("/")) {
    int n;

    // Reed the new values, and check them for sanity.
    n = ui.getInt("key_6_timer");
    if (n>=0 && n<=20) {
      keys_config.key_6_timer = n;
    }

    n = ui.getInt("key_7_timer");
    if (n>=0 && n<=20) {
      keys_config.key_7_timer = n;
    }

    n = ui.getInt("key_6_delay");
    if (n>=0 && n<=20) {
      keys_config.key_6_delay = n;
    }

    n = ui.getInt("key_7_delay");
    if (n>=0 && n<=20) {
      keys_config.key_7_delay = n;
    }

    n = ui.getInt("key_6_random");
    if (n>=0 && n<=20) {
      keys_config.key_6_random = n;
    }

    n = ui.getInt("key_7_random");
    if (n>=0 && n<=20) {
      keys_config.key_7_random = n;
    }

    n = ui.getInt("timer_pause");
    if (n>=1 && n<=20) {
      keys_config.timer_pause = n;
    }

    n = ui.getInt("display_delay");
    if (n>=1 && n<=20) {
      keys_config.display_delay = n;
    }

    n = ui.getInt("signal_volume");
    if (n>=0 && n<=8) {
      keys_config.signal_volume = n;
    }
    n = ui.getInt("countdown_beep");
    if (n>=0 && n<=20) {
      keys_config.countdown_beep = n;
    }

    // Save the config to eeprom
    EEPROM.put(0, keys_config);
    EEPROM.commit();
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
  /// server.begin();

  // start server portal
  ui.attachBuild(build);
  ui.attach(action);
  ui.start();

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

  // Read the saved config values
  EEPROM.begin(100);
  EEPROM.get(0, keys_config);

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

// Send one full sine wave to the speaker.
void beep_signal()
{
  for (int i=0; i<100; i++) {
    dac_output_voltage(DAC_CHANNEL_2, sineLookupTable[i]*keys_config.signal_volume/8);
  }
  // Important so set the voltage to 0 in the end, to avoid extra current
  // remaining through the speaker.
}

// Send 200 sine waves to the speaker. It makaes a short beep sound.
void beep_1()
{
  for (int i=0; i<200; i++) {
    beep_signal();
  }
}

void loop()
{
  // Current time
  unsigned int t = millis();

  ui.tick();

  // Update the display if needed.
  if (update_display || t > next_update_time) {
    Serial.printf("loop %d %d\n", update_display, t);
    display_updater(t);

    // Send updated display to the shift registers
    writeRegisters();
    update_display = false;
  }
}

// Clear the shift registers, all the LEDs get blank.
void clearRegisters()
{
  for (int i=0; i<8; i++){
     registers[i] = HIGH;
  }
  for (int i=8; i<16; i++){
     registers[i] = LOW;
  }
}

// Write the registers two sequential 74hc565
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
