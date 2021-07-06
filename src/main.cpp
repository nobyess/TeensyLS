/*
TeensyLS by Lonnie Headley

MIT License

Copyright (c) 2021 Lonnie Headley

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <Arduino.h>
#include <EEPROM.h>
#include <Bounce.h>
#include <elapsedMillis.h>
#include <AccelStepper.h>
#include <Encoder.h>
#include <EasyNextionLibrary.h>

bool clock60hz;
elapsedMillis ellapsed500ms;

/*elapsedMicros loopTime;
int loopTimeMax;
int loopTimeMin;
int loopTimeAvg;*/

//--------------------------------------------
// I/O defines/variables/functions
//--------------------------------------------

#define knobAIn       0
#define knobBIn       1

#define btnKnobIn     2
#define btnLeftIn     3
#define btnRightIn    4
#define btnLeftOut    5
#define btnRightOut   6
#define switchIn      7

#define drvStep       9
#define drvDirection  10

#define spindleA      16
#define spindleB      17

Bounce knobA(knobAIn, 15);
Bounce knobB(knobBIn, 15);
Bounce btnKnob(btnKnobIn, 10);
Bounce btnLeft(btnLeftIn, 10);
Bounce btnRight(btnRightIn, 10);
Bounce switchEnable(switchIn, 10);

int knobValue;

void updateIO();

//--------------------------------------------
// Nextion defines/variables/functions
//--------------------------------------------

#define strConvDigits 3

#define pageIntro     0
#define pageDebug     1 //not used anymore
#define pageScope     2 //not used anymore
#define pageDebugTxt  3 //not used anymore
#define pageMenu      4
#define pageJogFeed   5
#define pageThreading 6
#define pageInputPos  7
#define pageError     8
#define pageStarts    9
#define pageSetup     10

#define keyCurrent    -8
#define keyBS         -7
#define keyCancel     -6
#define keyOK         -5
#define keyDot        -4
#define keySign       -3
#define keyClear      -1
#define keyZero       0

#define varLeftStop   0
#define varRightStop  1
#define varPPR        2
#define varSPMM       3
#define varAccel      4
#define varSteprate   5

EasyNex nex(Serial5);

String inputPositionValue;
int inputPositionVar;

int currentPage;
int returnPage;

String unitString(bool spell);
String floatToString(float in);
String unitsToString(float in);
String positionString();
String feedString();
String rpmString();
String threadString();


void updateNextion();
void nexInputPosition(String question, int var, String initialValue);
void nexGotoPage(int page);
void nexUpdatePage(int page);


//--------------------------------------------
// Movement defines/variables/functions
//--------------------------------------------
#define minTPI        4
#define maxTPI        200
#define maxMMS        10
#define maxIPS        0.3937
#define minMMPT       0.05
#define maxMMPT       4

#define minAccel      20000
#define minMaxSR      5000

Encoder spindle(spindleA, spindleB);

AccelStepper lsDriver(1, drvStep, drvDirection);

int pulsesPerRev = 2880;
int stepsPerMM = 800;
int acceleration = 200000;
int maxStepRate = 40000;

bool jogAdjust = true;
float jogFeedMulti = .1;
float jogFeedSpeed = 1;

bool imperial;

float rpm;

bool threading;
//bool jogging;

float threadCount = 1.0;
int numStarts = 1;
int start = 1;
float startOffset;

bool invertSpindle = true;
int32_t currentSpindle;

float current;

float leftStop;
long leftSteps;
bool leftStopOn;

float rightStop;
long rightSteps;
bool rightStopOn;

void updateMovement();
void invertUnits();
long unitsToStep(float in);
long spindleToStep(float in);
float stepsToUnits(long in);
void processThread();
void processFeed();

//--------------------------------------------
// System defines/variables/functions
//--------------------------------------------
#define goodEepromValue 1984 // arbitrary value, just to check to see if eeprom has been written stored at least once

void eepromGet() {
  EEPROM.get(4, pulsesPerRev);
  EEPROM.get(8, stepsPerMM);
  EEPROM.get(12, acceleration);
  EEPROM.get(16, maxStepRate);
}

void eepromPut() {
  EEPROM.put(0, goodEepromValue);
  EEPROM.put(4, pulsesPerRev);
  EEPROM.put(8, stepsPerMM);
  EEPROM.put(12, acceleration);
  EEPROM.put(16, maxStepRate);
}

//--------------------------------------------
// Setup
//--------------------------------------------

void setup() {

  // pinMode for spindle is set up by the encoder library
  //pinMode(spindleA, INPUT); 
  //pinMode(spindleB, INPUT);

  pinMode(knobAIn, INPUT_PULLUP);
  pinMode(knobBIn, INPUT_PULLUP);
  pinMode(btnKnobIn, INPUT_PULLUP);
  pinMode(btnLeftIn, INPUT_PULLUP);
  pinMode(btnRightIn, INPUT_PULLUP);
  pinMode(btnLeftOut, OUTPUT);
  pinMode(btnRightOut, OUTPUT);
  pinMode(switchIn, INPUT_PULLUP);

  nex.begin(115200);
  Serial.begin(9600);

  int eepromGood;
  EEPROM.get(0, eepromGood);

  if (eepromGood == goodEepromValue) {
    eepromGet();
  } else {
    eepromPut();
  }
  lsDriver.setAcceleration(acceleration);
  delay(2000);
  nexGotoPage(btnKnob.read() ? pageMenu : pageSetup);
}

void loop() {
  static int32_t lastSpindle; 

  updateIO();
  updateMovement();

  if (!lsDriver.isRunning() && !threading) {
    if (ellapsed500ms > 500) {
      ellapsed500ms = 0;
      clock60hz = !clock60hz;

      rpm = ((currentSpindle - lastSpindle) / (float)pulsesPerRev) * 120;
      lastSpindle = currentSpindle;
    }
    updateNextion(); //can't update the nextion while the stepper is moving. unfortunately there is still some delay that causes jitter in stepping
  } 
}

void updateMovement() {
  currentSpindle = (invertSpindle ? -spindle.read() : spindle.read());
  
  switch (currentPage)
  {
  case pageMenu:
  case pageJogFeed:
    processFeed();
    break;
  case pageThreading:
    processThread();
    break;
  default:
    break;
  }

  lsDriver.run();
  current = stepsToUnits(lsDriver.currentPosition());
}

// update input and outputs
void updateIO() {
  float jFM;

    // using bounce library for smoother reading of cheapo encoder
  knobB.update();
  if (knobA.update()) {
    if (knobA.fallingEdge()) {
      if (knobB.read() == true) {
        knobValue++;
      } else {
        knobValue--;
      }
      if (!btnKnob.read() && !threading) {
        switch (nex.currentPageId) {
          case pageMenu:
          case pageJogFeed:
            jFM = (imperial ? jogFeedMulti / 60: jogFeedMulti);
            jogFeedSpeed += (knobB.read() ? jFM : -jFM);
            if (imperial) {
              if (jogFeedSpeed < jFM) { jogFeedSpeed = jFM; }
              if (jogFeedSpeed > maxIPS) { jogFeedSpeed = maxIPS; }
            } else {
              if (jogFeedSpeed < jFM) { jogFeedSpeed = jFM; }
              if (jogFeedSpeed > maxMMS) { jogFeedSpeed = maxMMS; }
            }
            break;
          case pageThreading:
            if (imperial) {
              threadCount += knobB.read() ? 1 : -1;
              if (threadCount < minTPI) { threadCount = minTPI; }
            } else {
              threadCount += knobB.read() ? 0.05 : -0.05;
              if (threadCount < minMMPT) { threadCount = minMMPT; }
              if (threadCount > maxMMPT) { threadCount = maxMMPT; }
            }
            break;
        }
      }
    }
  }

  btnKnob.update();
  btnLeft.update();
  btnRight.update();
  switchEnable.update();
}

void processThread() {

  static float spindlePosition;
  static bool direction;
  static long threadNumber;
  static long target;
  static long positionOffset;
  static long threadOffset;
  
  // calculate the number of full rotations
  threadNumber = floor(currentSpindle / pulsesPerRev);

  // give us a number between 0.0 and 1.0 for the orientation of the spindle
  spindlePosition = (currentSpindle % pulsesPerRev) / (float)pulsesPerRev;

  if (threading) {
    if (direction) { // which way are we going. 0 = left, 1 = right.
      // thread magic - calculate target position based on the current thread number,
      // current spindle orientation, and add in the offset for the start selected
      target = positionOffset - spindleToStep((threadNumber - threadOffset) + spindlePosition + startOffset);

      // since we are starting behind the actual thread to cut we have to restrict that movement
      if (target < positionOffset) { target = positionOffset; }

      if (switchEnable.read()) {
        if (btnRight.read()) {
          // button is not pressed and we are jogging - turn off threading
          threading = 0;
        }
      } else {
        if (target >= rightSteps) {
          // we have hit the end stop - turn off threading
          threading = 0;

          // could have overshot so just bump the target to the exact end stop position
          target = rightSteps;
        }
      }
    } else {
      target = positionOffset + spindleToStep((threadNumber - threadOffset) + spindlePosition + startOffset);
      if (target > positionOffset) { target = positionOffset; }
      if (switchEnable.read()) {
        if (btnLeft.read()) {
          threading = 0;
        }
      } else {
        if (target <= leftSteps) {
          threading = 0;
          target = leftSteps;
        }
      }
    }

    // turn threading mode off if the switch is turned off while none of the direction buttons are pressed
    if (switchEnable.read() && btnLeft.read() && btnRight.read()) { threading = false; }
  
    lsDriver.setMaxSpeed(maxStepRate);
    lsDriver.moveTo(target);
  } else {
    // threading mode is not on so we must check for user input
    // switch off - check for only a direction button press for jogging 
    // switch on - check for direction button press but the end stop must be enabled and the current position can't exceed it
    if (switchEnable.read() ? !btnLeft.read() : (!btnLeft.read() && leftStopOn && lsDriver.currentPosition() > leftSteps)) {
      // set up for a new thread operation
      direction = false;
      threading = true;
      threadOffset = threadNumber - 1; // fall back behind the current position by 1 thread
      positionOffset = lsDriver.currentPosition(); // save the current position as the offset
    } else if (switchEnable.read() ? !btnRight.read() : (!btnRight.read() && rightStopOn && lsDriver.currentPosition() < rightSteps)) {
      direction = true;
      threading = true;
      threadOffset = threadNumber - 1;
      positionOffset = lsDriver.currentPosition();
    }
  }
}

void processFeed() {
  lsDriver.setMaxSpeed(unitsToStep(jogFeedSpeed));

  if (switchEnable.read()) {
    if (!btnLeft.read()) {
      lsDriver.moveTo(lsDriver.currentPosition() - 1000);
    } else if (!btnRight.read()) {
      lsDriver.moveTo(lsDriver.currentPosition() + 1000);
    } else {
      if (lsDriver.isRunning()) {
        lsDriver.stop();
      }
    }
  } else {
    if (!btnLeft.read() && leftStopOn) {
      lsDriver.moveTo(unitsToStep(leftStop));
    } else if (!btnRight.read() && rightStopOn) {
      lsDriver.moveTo(unitsToStep(rightStop));
    }        
  }
}

void invertUnits() {
  if (imperial) {
    imperial = false;
    current = current * 25.4;
    leftStop = leftStop * 25.4;
    rightStop = rightStop * 25.4;
    jogFeedSpeed = jogFeedSpeed * 25.4;
    float t = (1 / threadCount) * 25.4;
    threadCount = floor(t / 0.05) * 0.05;
    if (threadCount > 4) { threadCount = 4; }

  } else {
    imperial = true;
    threadCount = floor(25.4 / threadCount);
    current = current / 25.4;
    leftStop = leftStop / 25.4;
    rightStop = rightStop / 25.4;
    jogFeedSpeed = jogFeedSpeed / 25.4;

  }
}


float stepsToUnits(long in) {
  return in / (imperial ? stepsPerMM * 25.4 : stepsPerMM);
}

long spindleToStep(float in) {
  float val;
  if (imperial) {
    val = in * stepsPerMM * (1 / threadCount) * 25.4;
  } else {
    val = in * stepsPerMM * threadCount;
  }
  return val;
}

long unitsToStep(float in) {
  return in * (imperial ? stepsPerMM * 25.4 : stepsPerMM);
}

bool closeEnough(float v1, float v2, float tolerance) {
  return (abs(v1 - v2) < tolerance);
}


// update the Nextion based on which page is currently being displayed
void updateNextion() {
  static elapsedMillis tmrNextionUpdate;

  nex.NextionListen();

  switch (currentPage)
  {
  case pageDebug:
    break;
  case pageScope:
    break;
  case pageJogFeed:
    if (tmrNextionUpdate > 50) {
      tmrNextionUpdate = 0;
      nex.writeStr("powerfeed.fr.txt", feedString());
      nex.writeStr("powerfeed.position.txt", positionString());
      nex.writeStr("powerfeed.rpm.txt", rpmString());
    }
  case pageMenu:
    if (tmrNextionUpdate > 50) {
      tmrNextionUpdate = 0;
      nex.writeStr("menu.fr.txt", feedString());
      nex.writeStr("menu.rpm.txt", rpmString());
    }
    break;
  case pageThreading:
    if (tmrNextionUpdate > 50) {
      tmrNextionUpdate = 0;
      nex.writeStr("threading.position.txt", positionString());
      nex.writeStr("threading.pitch.txt", threadString());
      nex.writeStr("threading.rpm.txt", rpmString());
    }
    break;
  default:
    break;
  }
}

String positionString() {
  return String(current, 3);
}

String threadString() {
  return imperial ? String(threadCount, 0) + " tpi" : String(threadCount, 2) + "mm";
}

String rpmString() {
  return String((rpm < 0 ? -rpm : rpm), 0);
}

String feedString() {
  return String((imperial ? jogFeedSpeed * 60 : jogFeedSpeed), 2) + (imperial ? "ipm" : "mm/s");
}

String unitString(bool spell) {
  String ret;
  if (spell) {
    ret = imperial ? "Inch" : "Metric";
  } else {
    ret = imperial ? "in" : "mm";
  }
  return ret;
}

String floatToString(float in) {
  String ret = String(in, strConvDigits);
  for (int i = ret.length() - 1; i > 0; i--)
  {
    if (ret.charAt(i) == '0') { 
      ret.remove(i);
    } else if (ret.charAt(i) == '.') {
      ret.remove(i);
      break;
    } else {

      break;
    }
  }
  return ret;
}

String unitsToString(float in) {
  String ret = floatToString(in);
  //ret += unitString(false);
  return ret;
}

void nexShowError(String title, String message) {
  nex.writeStr("error.title.txt", "Error" + title);
  nex.writeStr("error.message.txt", message);
  returnPage = currentPage;
  nexGotoPage(pageError);
}

void nexInputPosition(String question, int var, String initialValue) {
  nex.writeNum("input.integer.val", 0);
  inputPositionValue = initialValue;
  returnPage = currentPage;
  inputPositionVar = var;
  nex.writeStr("input.value.txt", inputPositionValue);
  nex.writeStr("input.q.txt", question);
  nexGotoPage(pageInputPos);
}

void nexInputNumber(String question, int var, int initialValue) {
  nex.writeNum("input.integer.val", 1);
  inputPositionValue = String(initialValue);
  returnPage = currentPage;
  inputPositionVar = var;
  nex.writeStr("input.value.txt", inputPositionValue);
  nex.writeStr("input.q.txt", question);
  nexGotoPage(pageInputPos);
}

// do full page update and request page change to that updated page
void nexGotoPage(int page)
{
  nexUpdatePage(page);
  currentPage = page;
  String i = "page ";
  nex.writeStr(i + String(page));
}

// do full page update
void nexUpdatePage(int page)
{
  switch (page)
  {
    case (pageMenu):
      nex.writeStr("menu.fr.txt", feedString());
      break;
    case (pageJogFeed):
      nex.writeStr("powerfeed.fr.txt", feedString());
      nex.writeStr("powerfeed.position.txt", positionString());      
      nex.writeStr("powerfeed.leftstop.txt", (leftStopOn ? unitsToString(leftStop) : "---"));
      nex.writeStr("powerfeed.rightstop.txt", (rightStopOn ? unitsToString(rightStop) : "---"));
      nex.writeStr("powerfeed.units.txt", unitString(true));      
      break;
    case (pageThreading):
      nex.writeStr("threading.position.txt", positionString());      
      nex.writeStr("threading.leftstop.txt", (leftStopOn ? unitsToString(leftStop) : "---"));
      nex.writeStr("threading.rightstop.txt", (rightStopOn ? unitsToString(rightStop) : "---"));
      nex.writeStr("threading.starts.txt", String(start) + " of " + String(numStarts));
      nex.writeStr("threading.bunits.txt", unitString(true));
      nex.writeStr("threading.threadlabel.txt", "Thread:"); //" + imperial ? "(tpi):" : "(mm):"); //remove this crap
      nex.writeStr("threading.pitch.txt", threadString());
      break;
    case (pageStarts):
      nex.writeNum("starts.b0.bco", start == 1 ? 26051 : 65535);
      nex.writeNum("starts.b1.bco", start == 2 ? 26051 : 65535);
      nex.writeNum("starts.b2.bco", start == 3 ? 26051 : 65535);
      nex.writeNum("starts.b3.bco", start == 4 ? 26051 : 65535);
      nex.writeNum("starts.b4.bco", start == 5 ? 26051 : 65535);
      nex.writeNum("starts.b5.bco", numStarts == 1 ? 26051 : 65535);
      nex.writeNum("starts.b6.bco", numStarts == 2 ? 26051 : 65535);
      nex.writeNum("starts.b7.bco", numStarts == 3 ? 26051 : 65535);
      nex.writeNum("starts.b8.bco", numStarts == 4 ? 26051 : 65535);
      nex.writeNum("starts.b9.bco", numStarts == 5 ? 26051 : 65535);
      break;
    case pageSetup:
      nex.writeStr("setup.ppr.txt", String(pulsesPerRev / 4));
      nex.writeStr("setup.spmm.txt", String(stepsPerMM));
      nex.writeStr("setup.accel.txt", String(acceleration / 1000));
      nex.writeStr("setup.steprate.txt", String(maxStepRate / 1000));
      break;
  }
}

void trigger0() { // handle UI triggers on input page
  static String displayString;
  static String p;
  int keyVal = nex.readNumber("input.key.val");

  switch (keyVal)
  {

    case keyOK:
      switch (inputPositionVar) {
        case varLeftStop:
          if (inputPositionValue.length() > 0) {
            leftStopOn = true;
            leftStop = inputPositionValue.toFloat();
            leftSteps = unitsToStep(leftStop);
          } else {
            leftStopOn = false;
            leftStop = 0;
            leftSteps = 0;
          }
          nex.writeStr("powerfeed.leftstop.txt", (leftStopOn ? unitsToString(leftStop) : ""));
          break;
        case varRightStop:
          if (inputPositionValue.length() > 0) {
            rightStopOn = true;
            rightStop = inputPositionValue.toFloat();
            rightSteps = unitsToStep(rightStop);
          } else {
            rightStopOn = false;
            rightStop = 0;
            rightSteps = 0;
          }
          nex.writeStr("powerfeed.rightstop.txt", (rightStopOn ? unitsToString(rightStop) : ""));
          break;
        case varPPR:
          pulsesPerRev = inputPositionValue.toInt() * 4;
          if (pulsesPerRev < 1) { pulsesPerRev = 1; }
          nex.writeStr("setup.ppr.txt", String(pulsesPerRev / 4));
          eepromPut();
          break;
        case varSPMM:
          stepsPerMM = inputPositionValue.toInt();
          if (stepsPerMM < 1) { stepsPerMM = 1; }
          nex.writeStr("setup.spmm.txt", String(stepsPerMM));
          eepromPut();
          break;
        case varAccel:
          acceleration = inputPositionValue.toInt() * 1000;
          nex.writeStr("setup.accel.txt", String(acceleration / 1000));
          lsDriver.setAcceleration(acceleration);
          eepromPut();
          break;
        case varSteprate:
          maxStepRate = inputPositionValue.toInt() * 1000;
          nex.writeStr("setup.steprate.txt", String(maxStepRate / 1000));
          eepromPut();
          break;
      }
      nexGotoPage(returnPage);
      break;
    case keyCancel:
      nexGotoPage(returnPage);
      break;
    case keySign:
      if (inputPositionValue.startsWith("-")) {
        inputPositionValue.remove(0, 1);

      } else {
        if (inputPositionValue != "0") {
          inputPositionValue = "-" + inputPositionValue; 
        } else {
          inputPositionValue = "-";
        }
      }
      break;
    case keyDot:
      if (inputPositionValue.indexOf(".") == -1) {
        inputPositionValue += ".";
      }
      break;
    case keyCurrent:
      inputPositionValue = floatToString(current);
      break;
    case keyBS:
      if (inputPositionValue.length() > 1) {
        inputPositionValue = inputPositionValue.substring(0, inputPositionValue.length() - 1);
      } else {
        inputPositionValue = "";
      }
      break;
    case keyClear:
      inputPositionValue = "";
      switch (inputPositionVar) {
        case varLeftStop:
          leftStopOn = false;
          leftStop = 0;
          nex.writeStr("powerfeed.leftstop.txt", "---");
          break;
        case varRightStop:
          rightStopOn = false;
          rightStop = 0;
          nex.writeStr("powerfeed.rightstop.txt", "---");
          break;
      }
      nexGotoPage(returnPage);
      break;
    default:
      if (inputPositionValue == "0") {
        inputPositionValue = String(keyVal);
      } else {
        inputPositionValue += String(keyVal);
      }
      break;
  }
  
  nex.writeStr("input.value.txt", inputPositionValue);
}

void trigger1() { // handle UI triggers on main menu page
  switch (nex.readNumber("menu.key.val")) {
    case 0:
      nexGotoPage(pageJogFeed);
      break;
    case 1:
      nexGotoPage(pageThreading);
      break;
    case 2:
      nexGotoPage(pageSetup);
      break;
  }
}

void trigger6() { // handle UI triggers on feed page
  switch (nex.readNumber("powerfeed.key.val")) {
    case 0:
      leftStop = current;
      leftStopOn = true;
      nex.writeStr("powerfeed.leftstop.txt", floatToString(leftStop));
      break;
    case 1:
      current = 0;
      lsDriver.setCurrentPosition(0);
      nex.writeStr("powerfeed.position.txt", positionString());      
      break;
    case 2:
      rightStop = current;
      rightStopOn = true;
      nex.writeStr("powerfeed.rightstop.txt", floatToString(rightStop));
      break;
    case 3:
      invertUnits();
      nexUpdatePage(currentPage);
      break;
    case 4:
      jogFeedMulti = 0.01;
      break;
    case 5:
      jogFeedMulti = 0.1;
      break;
    case 6:
      jogFeedMulti = 1;
      break;
    case 7:
      nexInputPosition("Left Stop Position (" + unitString(true) + ")", varLeftStop, floatToString(leftStop));
      break;
    case 8:
      nexInputPosition("Right Stop Position (" + unitString(true) + ")", varRightStop, floatToString(rightStop));
      break;
    case 9:
      nexGotoPage(pageMenu);
  }
}

void trigger7() { // handle UI triggers on threading page
  switch (nex.readNumber("threading.key.val")) {
    case 0: //pitch/tpi button
      invertUnits();
      nexUpdatePage(currentPage);
      break;
    case 1:
      nexGotoPage(pageStarts);
      break;
    case 2:
      nexGotoPage(pageMenu);
      break;
    case 3:
      nexInputPosition("Left Stop Position (" + unitString(true) + ")", varLeftStop, floatToString(leftStop));
      break;
    case 4:
      nexInputPosition("Right Stop Position (" + unitString(true) + ")", varRightStop, floatToString(rightStop));  
      break;
    case 5:
      current = 0;
      lsDriver.setCurrentPosition(0);
      nexUpdatePage(pageThreading);
      break;
  }
}

void trigger8() { // handle UI triggers on starts page
  int val = nex.readNumber("starts.key.val");
  switch (val) {
    case 0: //ok
      startOffset = (1.0 / numStarts) * (start - 1);
      nexGotoPage(pageThreading);
      break;
    default:
      if (val <= 5) {
        if (val <= numStarts) { start = val; }
     } else {
        numStarts = val - 5;
        if (start > numStarts) { start = numStarts; }
      }
      nexUpdatePage(pageStarts);
      break;
  }  
}

void trigger9() { // handle UI triggers on setup page
  int val = nex.readNumber("setup.key.val");
  switch (val) {
    case -1:
      nexGotoPage(pageMenu);
      break;
    case 0:
      nexInputNumber("Spindle Pulses/Revolution", varPPR, pulsesPerRev / 4);
      break;
    case 1:
      nexInputNumber("Steps/MM", varSPMM, stepsPerMM);
      break;
    case 2:
      nexInputNumber("Acceleration (x1000)", varAccel, acceleration / 1000);
      break;
    case 3:
      nexInputNumber("Maximum Steprate (x1000)", varSteprate, maxStepRate / 1000);
      break;
  }
}

void trigger10() {
  int val = nex.readNumber("error.key.val");
  switch (val) {
    case -1:
      nexGotoPage(returnPage);
      break;
  }
}
