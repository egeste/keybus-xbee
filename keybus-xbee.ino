#include "config.h"
#include <SoftwareSerial.h>

SoftwareSerial radio(RADIO_RX_PIN, RADIO_TX_PIN);

/******************************************************************************
* Code below this line adapted from markkimsal/homesecurity
******************************************************************************/

int  msg_len_status = 45;
char alarm_buf[3][30];
char guibuf[100];
char gcbuf[30];
int  guidx = 0;
int  gidx = 0;

// Used to read bits on F7 message
int const BIT_MASK_BYTE1_BEEP = 0x03;
int const BIT_MASK_BYTE2_READY = 0x10;
int const BIT_MASK_BYTE2_ARMED_HOME = 0x80;
int const BIT_MASK_BYTE3_AC_POWER = 0x08;
int const BIT_MASK_BYTE3_CHIME_MODE = 0x20;
int const BIT_MASK_BYTE3_ARMED_AWAY = 0x04;

extern unsigned long low_time = 0;
bool mid_msg = false;
bool mid_ack = false;

void print_hex(int v, int num_places) {
  int mask=0, n, num_nibbles, digit;

  for (n=1; n<=num_places; n++) {
    mask = (mask << 1) | 0x0001;
  }
  v = v & mask; // truncate v to specified number of places

  num_nibbles = num_places / 4;
  if ((num_places % 4) != 0) {
    ++num_nibbles;
  }

  do {
    digit = ((v >> (num_nibbles-1) * 4)) & 0x0f;
    radio.print(digit, HEX);
  } while(--num_nibbles);
}

void read_chars(int ct, char buf[], int *idx, int limit) {
  char c;
  int  x=0;
  int idxval = *idx;
  while (x < ct) {
    if (Serial.available()) {
      c = Serial.read();
      if (idxval >= limit) {
        radio.print("Buffer overflow: ");
        radio.print(idxval, DEC);
        radio.print(" : ");
        radio.print(limit, DEC);
        radio.println();
        *idx = idxval;
        return;
      }
      buf[ idxval ] = c;
      idxval++;
      x++;
    }
  }
  *idx = idxval;
}

void read_chars_dyn(char buf[], int *idx, int limit) {
  char c;
  int  ct = 1;
  int  x=0;
  int  idxval = *idx;

  while (!Serial.available()) {
    // Waiting...
  }

  c = Serial.read();
  buf[ idxval ] = c;
  idxval++;

  ct = (int)c;

  while (x < ct) {
    if (Serial.available()) {
      c = Serial.read();
      if (idxval >= limit) {
        radio.print("Dyn Buffer overflow: ");
        radio.println(idxval, DEC);
        radio.println();
        *idx = idxval;
        return;
      }
      buf[ idxval ] = c;
      idxval++;
      x++;
    }
  }
  *idx = idxval;
}

void on_status(char cbuf[], int *idx) {
  radio.println("BEGIN_STATUS");

  radio.print("STATUS_HEADERS: ");
  for (int x = 1; x < 7 ; x++) {
    print_hex(cbuf[x], 8);
    radio.print(",");
  }
  radio.println();

  //7th byte is incremental counter
  radio.print("STATUS_COUNT: ");
  print_hex(cbuf[7], 8);
  radio.println();

  radio.print("STATUS_MESSAGE: ");
  for (int x = 8; x < *idx ; x++) {
    print_hex(cbuf[x], 8);
    radio.print(",");
  }
  radio.println();

  //F2 messages with less than 16 bytes don't seem to have
  // any important information
  if (19 > (int) cbuf[1]) {
    memset(cbuf, 0, sizeof(cbuf));
    *idx = 0;
    return;
  }

  //19th spot is 01 for disarmed, 02 for armed
  //short armed = (0x02 & cbuf[19]) && !(cbuf[19] & 0x01);
  short armed = 0x02 & cbuf[19];

  //20th spot is away / stay
  // this bit is really confusing
  // it clearly switches to 2 when you set away mode
  // but it is also 0x02 when an alarm is canceled,
  // but not cleared - even if you are in stay mode.
  short away = 0x02 & cbuf[20];

  //21st spot is for bypass
  // short bypass = 0x02 & cbuf[21];

  //22nd spot is for alarm types
  //1 is no alarm
  //2 is ignore faults (like exit delay)
  //4 is a alarm
  //6 is a fault that does not cause an alarm
  short exit_delay = (cbuf[22] & 0x02);
  short fault = (cbuf[22] & 0x04);

  radio.print("STATUS_ARMED: ");
  radio.println(armed ? "yes" : "no");

  radio.print("STATUS_MODE: ");
  radio.println(away ? "away" : "stay");

  radio.print("STATUS_IGNORE_FAULTS: ");
  radio.println(exit_delay ? "yes" : "no");

  radio.print("STATUS_FAULTED: ");
  radio.println(fault ? "yes" : "no");

  radio.print("STATUS_ALARM: ");
  if (armed && fault && !exit_delay) {
    radio.println("yes");
  } else if (!armed && fault && away && !exit_delay) {
    radio.println("canceled");
  } else {
    radio.println("no");
  }

  radio.println("END_STATUS");

  memset(cbuf, 0, sizeof(cbuf));
  *idx = 0;
}

void on_display(char cbuf[], int *idx) {
  // first 4 bytes are addresses of intended keypads to display this message
  // 5th byte represent zone
  // 6th binary encoded data including beeps
  // 7th binary encoded data including status armed mode
  // 8th binary encoded data including ac power chime
  // 9th byte Programming mode = 0x01
  // 10th byte promt position in the display message of the expected input
  // 12-end is the body

  // Let the receiver know we're starting a message sequence
  radio.println("BEGIN_MESSAGE");

  for (int x = 1; x <= 11 ; x++) {
    switch (x) {
      case 1:
        radio.print("ADDR1: ");
        print_hex(cbuf[x], 8);
        radio.println();
        break;

      case 2:
        radio.print("ADDR2: ");
        print_hex(cbuf[x], 8);
        radio.println();
        break;

      case 3:
        radio.print("ADDR3: ");
        print_hex(cbuf[x], 8);
        radio.println();
        break;

      case 4:
        radio.print("ADDR4: ");
        print_hex(cbuf[x], 8);
        radio.println();
        break;

      case 5:
        radio.print("ZONE: ");
        print_hex(cbuf[x], 8);
        radio.println();
        break;

      case 6:
        if ((cbuf[x] & BIT_MASK_BYTE1_BEEP) > 0) {
          radio.print("BEEPS: ");
          print_hex(cbuf[x], 8);
          radio.println();
        }
        break;

      case 7:
        radio.print("ARMED_STAY: ");
        radio.println((cbuf[x] & BIT_MASK_BYTE2_ARMED_HOME) ? "true" : "false");
        radio.print("READY: ");
        radio.println((cbuf[x] & BIT_MASK_BYTE2_READY) ? "true" : "false");
        break;

      case 8:
        radio.print("CHIME_MODE: ");
        radio.println((cbuf[x] & BIT_MASK_BYTE3_CHIME_MODE) ? "on" : "off");
        radio.print("AC_POWER: ");
        radio.println((cbuf[x] & BIT_MASK_BYTE3_AC_POWER) ? "on" : "off");
        radio.print("ARMED_AWAY: ");
        radio.println(((cbuf[x] & BIT_MASK_BYTE3_ARMED_AWAY) > 0) ? "true" : "false");
        break;

      case 9:
        if (cbuf[x] == 0x01) {
          radio.print("PROGRAMMING MODE: ");
          print_hex(cbuf[x], 8);
          radio.println();
        }
        break;

      case 10:
        if (cbuf[x] != 0x00) {
          radio.print("PROMPT POS: ");
          radio.print((int)cbuf[x]);
          print_hex(cbuf[x], 8);
          radio.println();
        }
        break;

      default:
        radio.print("UNHANDLED: ");
        print_hex(cbuf[x], 8);
        radio.println();
        break;
    }
  }

  // Send the message body
  radio.print("MESSAGE_BODY: ");
  for (int x = 12; x < *idx -1; x++) { radio.print (cbuf[x]); }
  radio.println();

  // Let the receiver know that the message sequence has ended
  radio.println("END_MESSAGE");

  memset(cbuf, 0, sizeof(cbuf));
  *idx = 0;
}

/******************************************************************************
* Code above this line adapted from markkimsal/homesecurity
******************************************************************************/

void setup() {
  radio.begin(9600);
  Serial.begin(4800, SERIAL_8E1);
}

void loop() {
  char x = Serial.read();
  if (!x) { return; }

  if ((int) x == 0xFFFFFFF7) {
    guibuf[guidx] = x; guidx++;
    read_chars(msg_len_status - 1, guibuf, &guidx, 100);
    on_display(guibuf, &guidx);
    return;
  }

  if ((int) x == 0xFFFFFFF2) {
    gcbuf[gidx] = x; gidx++;
    read_chars_dyn(gcbuf, &gidx, 30);
    on_status(gcbuf, &gidx);
    return;
  }
}
