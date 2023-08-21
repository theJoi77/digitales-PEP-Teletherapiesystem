#include <Adafruit_NeoPixel.h>
#include  "DS3231.h"
#include <SPI.h>                   // SPI Interface for SD-Card
#include "SdFat.h"
#include "sdios.h"
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <avr/wdt.h>

// new Board
#define SW_1  2            // left Key
#define SW_2  3            // right Key
#define BT_PN 4
#define BT_RX 5
#define BT_TX 6
#define PIX_ON 7           // Pin for power the WS2812 LED
#define PIX_PIN 8
#define BUZZ  9            // speaker
#define RELAIS_PIN A1      // self holding relay
#define MPXV A2
#define CS_PIN 10

#define PIX_COUNT 5
#define INTERVAL_BEEP 10

/// old Board
//#define SW_1  2            // left Key
//#define SW_2  3            // right Key
//#define BUZZ  8            // speaker
//#define RELAIS_PIN 4      // self holding relay
//
//#define MPXV A0
//#define PIX_PIN 8
//
//#define PIX_COUNT 5
#define DEVICENR 5

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIX_COUNT, PIX_PIN, NEO_GRB + NEO_KHZ800);
SdFat sd;
DS3231  rtc(SDA, SCL);
Time  t;
File myFile;
int raw = 0;
float zero;
int readSensorInterval = 10;
float writeValueInterval = 50;
float trshForStart = 3;
bool readSensorFlagTimerInterval = false;
bool writeValueFlagTimerInterval = false;

uint32_t red = strip.Color(255, 0, 0);
uint32_t blue = strip.Color(0, 0, 255);
uint32_t lightgreen = strip.Color(0, 30, 0);
uint32_t magenta = strip.Color(170, 125, 181);
uint32_t cyan = strip.Color(125, 162, 181);
uint32_t yellow = strip.Color(255, 255, 0);
uint32_t orange = strip.Color(255, 85, 0);

uint32_t currentColor;
uint32_t timeOut = 0;
float valuEmmH2O;
int ringPosWrite = 0;
int ringPosRead = 0;
int ring[10];
int meanValue;
byte mode = 0;
byte row = 0;
bool recordState = false;
bool writeState = true;
bool pauseState = false;
bool endState = false;
bool recordFlag = false;
bool runState = true;
bool beep = false;
bool sw1lng, sw2shrt, sw2lng;
bool gaugechg = false;

String rowPaused = "";
String rowStop = "";
String inputString = "";

int stepPix = 1;

// the setup function runs once when you press reset or power the board
void setup() {
  attachInterrupt(0, switch1, CHANGE);
  //attachInterrupt(1, switch2, CHANGE);
  pinMode(SW_1, INPUT_PULLUP);          // left button (power on)
  // pinMode(SW_2, INPUT_PULLUP);          // right button
  pinMode(MPXV, INPUT);
  Serial.begin(115200);

  pinMode(RELAIS_PIN, OUTPUT);
  digitalWrite(RELAIS_PIN, HIGH);

  strip.begin();
  strip.clear();
  strip.setBrightness(200);
  strip.show();

  pinMode(PIX_ON, OUTPUT);
  pinMode(BUZZ, OUTPUT);
  digitalWrite(PIX_ON, HIGH);

  rtc.begin();
  if (!sd.begin(10)) {
    //  To Do: when sd fails ?
  }
  setTimer2();
  setWDTR();
  strip.fill(red, 0, 5);
  strip.show();
 
  t = rtc.getTime();
  myFile = sd.open("boot.txt", FILE_WRITE);
  if (myFile) myFile.println("test");
  myFile.close();
  if (sd.exists("boot.txt")) sd.remove("boot.txt");
  else while (1);

 

  // -------------- set preassure value base
  for (int i = 0; i < 10; i++) {
    raw = raw + analogRead(MPXV);
    delay(2);
  }
  raw = raw / 10;
  zero = ((float(raw) / 1023.0 - 0.02) / 0.02);

  writeValueInterval = getInterval();
  beep = true;
}

// the loop function runs over and over again forever
void loop() {
  if (mode == 0) {
    mode++;

    if (Serial.available() > 0) {
      char inChar = (char)Serial.read();
      inputString += inChar;
      if (inChar == '\n') {
        inputString = "";
        mode = 2;

        Serial.print("connected!");
        Serial.println(DEVICENR);

      }

    }
  }
  if (mode == 1) {
    if (writeState) currentColor = magenta;
    if (!recordFlag) currentColor = lightgreen;
    if (pauseState) currentColor = yellow;
    if (endState) currentColor = blue;
    if (readSensorFlagTimerInterval) { /// reading Sensor
      readSensorFlagTimerInterval = false;
      if (timeOut >= 30000) {
        if (!recordFlag)  runState = false;
        if ((writeState && recordFlag) || (pauseState)) endState = true;


      } else {
        //Serial.println(timeOut);
        timeOut ++;
      }



      if (ringPosWrite >= 10) ringPosWrite = 0; // store analog value in array
      ring[ringPosWrite] = analogRead(MPXV);
      ringPosRead = ringPosWrite;
      ringPosWrite++;                           // next array position
      for (int rp = 0; rp < 10; rp++) {         // read array values and do a FIRish thing.
        meanValue = meanValue + ring[ringPosRead];
        ringPosRead++;
        if (ringPosRead >= 10) ringPosRead = 0;
      }
      meanValue = meanValue / 11 ;
      // formula from MPXV Datasheet caluculate mmH20
      valuEmmH2O = ((float(meanValue) / 1023. - 0.02) / 0.02) - zero;
      if (valuEmmH2O > trshForStart) {
        recordFlag = true;
        writeState = true;
        timeOut = 0;
        if (pauseState) {
          pauseState = false;
        }
      }
      // LED stuff display value and status
      int pix;
      if (valuEmmH2O >= 0) {
        
        pix = valuEmmH2O / 5.0;                               // 5mm H20 per led
        pix = constrain(pix, 0, 6);
        //strip.clear();

        if (pix < 1 ) {
          strip.fill(currentColor, 0, 5); // background color depence  mode
          gaugechg = true;

          //trip.show();
        }
        else {
          //strip.clear();
          strip.fill(currentColor, 0, 5);
          if (pix >= 5) strip.fill(red, 0, 5);   // warning preasure is to hight
          else strip.fill(blue, 0, pix);        // preasure gage color
          gaugechg = true;
        }
        //strip.show();
      }
      if (valuEmmH2O < -1 && recordFlag) {
        strip.fill(strip.Color(50, 100, 100), 0, 5); // display inhalation
        gaugechg = true;
        //strip.show();
      }

      if (gaugechg) {
        gaugechg = false;
        strip.show();
      }
    } // end reading Sensor and display value
    if (recordFlag) {
      if (writeValueFlagTimerInterval) {
        writeValueFlagTimerInterval = false;

        if (writeState) {
          if (valuEmmH2O < 1 && valuEmmH2O > -1) valuEmmH2O = 0; // smoothing near zero
          switch (row) {
              
            case 0: // first time after writeflag is set write Header to file
              {
                write2File(false, false, createDataRow(0, 0, false, true));
                row++;
                break;
              }
            case 1:// first time after write flag is set or return from pause write timestamp
              {
                write2File(false, false, createDataRow(valuEmmH2O, 3, true, false));
                row++;
                break;
              }
            case 2: // normal write state every row with number and value
              {
                write2File(false, false, createDataRow(valuEmmH2O, 3, false, false));
                break;
              }
            case 3: // after pause write "Pause" instead of number
              {
                write2File(false, false, rowPaused); // write Time;Pause
                row = 1;
                break;
              }
            case 4:
              {
                rowPaused = createDataRow(0, 1, true, false); // store Time of pause
                rowStop = createDataRow(0, 3, true, false); // store Time in case of shutting down in pause mo
                writeState = false;
                pauseState = true;
                row = 3;
                break;
              }
          }
        }// end of write state
        if (endState) {
          mode = 3;
          endState = false;
          if  (sd.exists(write2File(false, true, ""))) {
            if (!sd.exists("logfiles.csv"))resetSN();
            write2File(true, false, getSN() + ";" + write2File(false, false, rowStop) + createDataRow(0, 2, true, true));
            setSN();
            strip.clear();
            beep = true;
            strip.fill(lightgreen, 0, 5);
            strip.show();

          }
          else {
            strip.clear();
            strip.fill(red, 0, 5);
            strip.show();
          }

          runState = false;
        }
      }
    } else mode = 0;
  }  // end of mode 1
  if (mode == 2) {
    strip.clear();
    strip.fill(orange, 0, stepPix);
    strip.show();
    while (Serial.available()) {
      char inChar = (char)Serial.read();
      inputString += inChar;
      if (inChar == '\n') {
        stepPix++;
        if (stepPix > 5) {
          stepPix = 1;
        }
        switch (inputString[0]) {
          case '0':         //print Filecontent of ":x"
            {
              wdt_disable();
              if (getFile(inputString.substring(inputString.indexOf(":") + 1, inputString.indexOf("\n")))) {
                Serial.println(F("ok!0"));
              }
              else Serial.println(F("error!0"));
              wdt_enable(WDTO_2S);yy
              break;
            }
          case '1':         //print Time","Date
            {
              if (inputString[1] == ':') {
                rtc.setTime(inputString.substring(2, 4).toInt(), inputString.substring(4, 6).toInt(), 0);
                rtc.setDate(inputString.substring(6, 8).toInt(), inputString.substring(8, 10).toInt(), inputString.substring(10, 14).toInt());
              }
              t = rtc.getTime();
              Serial.print(F("Time!"));
              Serial.print(rtc.getTimeStr());
              Serial.print(",");
              Serial.println(rtc.getDateStr1(true));
              Serial.println(F("ok!1"));
              break;
            }
          case '3': //
            {
              switch (inputString[2]) {
                case'0':
                  {
                    if (!resetSN()) {
                      Serial.println(F("error!3"));
                      break;
                    }
                    break;
                  }
                case '+':
                  {
                    if (!setSN()) {
                      Serial.println(F("error!3"));
                      break;
                    }
                    break;
                  }
              }
              Serial.print("SN!");
              Serial.println(getSN());
              Serial.println(F("ok!3"));
              break;
            }

          case '6': //
            {
              Serial.println(F("ok!6"));
              runState = false;
            }
            break;
          case '7': //
            {
              if (inputString[1] == ':') {
                int temp = inputString.substring(inputString.indexOf(":") + 1, inputString.indexOf("\n")).toInt();
                byte tempB = temp;

                if (!setID(tempB)) {
                  Serial.println(F("error!7"));
                  break;
                }
              }
              Serial.println("ID!" + getID());
              Serial.println(F("ok!7"));
              break;
            }

          case '9': //
            {
              wdt_disable();
              uint32_t volFree = sd.vol()->freeClusterCount();
              float fs = 0.000512 * volFree * sd.vol()->blocksPerCluster();
              wdt_enable(WDTO_2S);
              Serial.print("FreeSpace!");
              Serial.println(fs);
              Serial.println(F("ok!9"));
              break;
            }

          case 'B': //
            {
              if (inputString[1] == ':') {
                byte temp = inputString.substring(inputString.indexOf(":") + 1, inputString.indexOf("\n")).toFloat() * 100;
                if (!setInterval(temp)) {
                  Serial.println(F("error!B"));
                  break;
                }
              }
              Serial.print("IntVal!");
              Serial.println(getInterval() / 100);
              Serial.println(F("ok!B"));
              break;
            }
          case 'C':         //delete File
            {
              if (deleteFile(inputString.substring(inputString.indexOf(":") + 1, inputString.indexOf("\n"))))Serial.println(F("ok!C"));
              else Serial.println(F("error!C"));
              break;
            }
          case 'X':         //
            {
              Serial.print("connected!");
              Serial.println(DEVICENR);
              break;
            }
          default:
            {
            }
        }
        inputString = "";
      }
    }
  }
  if (runState) wdt_reset();
}
/*   createDataRow(float value, byte mode, boole time_, bool date_)
    mode: 0 creates Header with ID and Interval
          1 adds "Pause" in 2nd column
          2 adds Number in 2nd column
          3 adds value in 3rd and 4th column
    time_ adds current time in 1st column
    date: adds current date in 1st column
*/
String createDataRow(float value, byte mode, boolean time_, bool date_) {
  static unsigned int number = 0;
  String data = "";
  t = rtc.getTime();
  //---- beginn Header
  if (mode == 0) {
    data = "ID:;";
    data.concat(getID());
  }
  if (date_) data = data + ";Datum:;" + rtc.getDateStr1(true) + ";";
  if (mode == 0) {
    data.concat("Interval:;");
    String intervalString = String(writeValueInterval / 100, 3);   // To do: creat entry for Header
    intervalString.replace(".", ",");
    data.concat(intervalString);
    data.concat("\nTime;Nr;INSP;EXSP");
    //Serial.println(data);
  }
  //---- end Header
  //---- beginn Datarow
  if (time_) {
    data.concat(rtc.getTimeStr());
    data = data + ';';
  }
  else data = data + ';';
  if (mode == 1) {
    data.concat("Pause");
  }
  if (mode >= 2) {
    data.concat(number);
  }
  if (mode == 3) {
    if (value < 0) {               // inspiration; exspiration
      data.concat( ';' + String(value , 1) + ";0");
    }
    else {
      data.concat(";0;" + String(value, 1));
    }
    data.replace(".", ",");         // for excel - .csv
    number++;
  }
  return data;
}

/* write2File(bool dirfile, bool getfilename, String data)
   first call creats unique filename (idddmmnr.csv) nr = files per day
   logfiles: filename = logfiles.csv
   getfilename: no file on SD only returns current filename
   data: String is written in currenten file
*/
char *write2File(bool logfiles, bool getfilename, String data) {
  static bool first = true;
  static char filename[] = "xxxxxx00.csv";
  // char error[] = "error";
  if (first) {
    first = false;
    String  ID = getID();
    for (byte i = 0; i <= 1; i++) {
      filename[i] = ID[i];
    }
    char* t = rtc.getDateStr1(false);
    for (byte i = 2; i <= 5; i++) {
      filename[i] = t[i - 2];
    }
    do {
      filename[7]++;
      if (filename[7] == 57) {
        filename[7] = 48;
        filename[6]++;
      }
    } while (sd.exists(filename));
  }
  if (logfiles) {
    strcpy(filename, "logfiles.csv");
  }
  if (!getfilename) {
    myFile = sd.open(filename, FILE_WRITE);
    if (myFile) {
      myFile.println(data);
      t = rtc.getTime();
      myFile.timestamp(T_WRITE, t.year, t.mon, t.date, t.hour, t.min, t.sec);
      myFile.close();
    } else return 0;
  }
  return (char*)&filename;
}

void setTimer2() {
  cli();                              // disable interrupt
  TCNT2 = 0x00;
  TCCR2A = 0x00;
  TCCR2B = 0x00;
  OCR2A = 155; // 10ms
  TCCR2B |= (1 << CS22) + (1 << CS21) + (1 << CS20); // prescaler = 1024
  TCCR2A |= (1  << WGM21); // Clear Timer and Compare
  TIMSK2 = (1 << OCIE2A); // interrupt when Compare Match
  sei();                              // enable interrupt
}
ISR(TIMER2_COMPA_vect) { // Interrupt Service Routine
  static int writeValueFlagCounter = 0;
  static int beepCounter = 0;
  readSensorFlagTimerInterval = true;
  beepCounter++;
  writeValueFlagCounter++;

  if (beepCounter > INTERVAL_BEEP) {
    if (beep) {
      beep = false;
      digitalWrite(BUZZ, HIGH);
    }
    else digitalWrite(BUZZ, LOW);
    beepCounter = 0;
  }
  if (writeValueFlagCounter > writeValueInterval) {
    writeValueFlagTimerInterval = true;
    writeValueFlagCounter = 0;
  }
}
void setWDTR() {
  cli();
  wdt_enable(WDTO_2S);
  //  wdt_reset(); // Reset Watchdog Timer
  //  MCUSR &= ~(1 << WDRF); //Rücksetzen des Watchdog System Reset Flag
  //  WDTCSR = (1 << WDCE) | (1 << WDE); //Watchdog Change Enable
  //  WDTCSR = (1 << WDP3); //Watchdog Zyklus = 4 s
  //  WDTCSR |= (1 << WDIE); //Watchdog Interrupt enable
  sei();
}

void switch1() {
  static byte z = 0;
  static volatile unsigned long lastDebounceTime = 0;
  static volatile unsigned long pressed = 0;
  if ((millis() - lastDebounceTime) >= 20) {
    lastDebounceTime = millis();
    z++;    // z=1 is for catching the input after switch on
    if (z == 2) pressed = millis();
    if (z == 3) {
      z = 1;
      if ((millis() - pressed) > 3000) sw1lng = true;
      else {
        beep = true;
        if (!recordFlag)  runState = false;
        if (writeState && recordFlag) {

          row = 4;
        }
        if (pauseState) {

          endState = true;
        }
      }
    }
  }

}
//void switch2() {
//  static volatile unsigned long lastDebounceTime = 0;
//  static volatile unsigned long pressed = 0;
//  static boolean first = true;
//  if ((millis() - lastDebounceTime) >= 20) {
//    lastDebounceTime = millis();
//    if (first) {
//      first = false;
//      pressed = millis();
//    }
//    else {
//      first = true;
//      if ((millis() - pressed) > 3000){
//        Serial.println("sw2shrt");
//        sw2shrt = true;
//      }
//      else sw2lng = true;
//    }
//  }
//}

String getID() {
  byte id;
  String idStr = "0";
  EEPROM.get(0, id);
  if (id > 15) idStr = String(id, HEX);
  else idStr = idStr + String (id, HEX);
  return idStr;
}

boolean setID(byte id) {
  EEPROM.update(0, id);
  return true;
}

float getInterval() {
  byte iv;
  EEPROM.get(1, iv);
  float ivI = iv;
  return ivI;
}
boolean setInterval(byte iv) {
  byte id;
  EEPROM.update(1, iv);
  return true;
}

boolean setSN() {
  boolean mark = false;
  unsigned int i = 2;
  for (; i < EEPROM.length() ; i++) {
    EEPROM.get(i, mark);
    if (mark == true) break;
  }
  EEPROM.put(i, false);
  if (i == EEPROM.length() - 1) EEPROM.put(2, true);
  else EEPROM.put(i + 1, true);
  return true;
}

boolean resetSN() {
  EEPROM.update(2, true);
  for (int i = 3 ; i < EEPROM.length() ; i++) {
    EEPROM.update(i, false);
  }
  return true;
}

String getSN() {
  boolean mark = false;
  String SN = "";
  unsigned int i = 2;
  unsigned int z = 0;
  for (; i < EEPROM.length() ; i++) {
    EEPROM.get(i, mark);
    if (mark == true) {
      break;
    }
    z++;
    //if (z == EEPROM.length() / 2)z = 0; // funktioniert anscheinend nicht noch testen ...
  }
  SN.concat(z);
  return SN;
}

boolean getFile(String filename) {
  char line[100];
  int n;
  char buff[13];
  filename.toCharArray(buff, 13);
  if (!sd.exists(buff)) return false;
  else {
    SdFile rdfile(buff, O_RDONLY);

    if (!rdfile.isOpen()) return false;

    // read lines from the file
    while ((n = rdfile.fgets(line, sizeof(line))) > 0) {

      if (line[n - 1] == '\n') {
        Serial.print(filename + "!");
        Serial.print(line);
        // break;
      }
    }
  }
  return true;
}
boolean deleteFile (String filename) {
  char buff[13];
  filename.toCharArray(buff, 13);
  if (sd.remove(buff))  return true;
  else return false;
}
