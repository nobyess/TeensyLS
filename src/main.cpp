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

#define SERIAL_BUFFER_SIZE 256

#include <Arduino.h>
#include <Bounce.h>
#include <elapsedMillis.h>
#include <AccelStepper.h>
#include <Encoder.h>
#include <EasyNextionLibrary.h>

bool clock60hz;
elapsedMillis ellapsed500ms;
elapsedMillis ellapsed50ms;

elapsedMicros loopTime;
int loopTimeMax;
int loopTimeMin;
int loopTimeAvg;

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

#define startCmd     "page 4"

#define strConvDigits 3

#define pageIntro     0
#define pageDebug     1
#define pageScope     2
#define pageDebugTxt  3
#define pageMenu      4
#define pageJogFeed   5
#define pageThreading 6
#define pageInputPos  7
#define pageError     8
#define pageStarts    9

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

EasyNex nex(Serial5);

String inputPositionValue;

int inputPositionLastpage;
int inputPositionVar;
int nexCurrentPage;

void updateNextion();
void nexInputPosition(String question, int returnPage, int var, String initialValue);
void nexGotoPage(int page);
void nexUpdatePage(int page);
String unitString(bool spell);
String floatToString(float in);
String unitsToString(float in);
String positionString();
String feedString();
String rpmString();
String threadString();


//--------------------------------------------
// Movement defines/variables/functions
//--------------------------------------------

Encoder spindle(spindleA, spindleB);

AccelStepper lsDriver(1, drvStep, drvDirection);

#define mmPerRev      2
#define stepsPerRev   800
#define stepsPerMM    400
#define stepsPerIN    10160

int ticksPerRev = 2880;

bool jogAdjust = true;
float jogFeedMulti = .1;
float jogFeedSpeed = 1;

bool imperial;

float rpm;

bool threading;
float threadCount = 1.0;
int numStarts = 1;
int start = 1;
float startOffset;

int32_t currentSpindle;
long threadNumber;
float spindleMod;

bool threadDirection;

float current;
long currentTick;

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
// Setup
//--------------------------------------------

void setup() {

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

  lsDriver.setAcceleration(50000);
  lsDriver.setMaxSpeed(100000);

  delay(2000);
  nexGotoPage(pageMenu);
  loopTime = 0;
}

void loop() {
  static int32_t lastSpindle;

  updateIO();
  updateMovement();

  if (!lsDriver.isRunning() && !threading) { updateNextion(); } //can't update the nextion while the stepper is moving. unfortunately there is still some delay that causes jitter in stepping

  if (ellapsed500ms > 500) {
    ellapsed500ms = 0;
    clock60hz = !clock60hz;

    rpm = ((currentSpindle - lastSpindle) / (float)ticksPerRev) * 2;
    lastSpindle = currentSpindle;
  }
 /*
  int lt = loopTime;
  if (lt < loopTimeMin) { loopTimeMin = lt; }
  if (lt > loopTimeMax) { loopTimeMax = lt; }
  loopTimeAvg = loopTimeAvg + lt;
  loopTimeAvg = loopTimeAvg / 2;
  loopTime = 0;*/

}

void updateMovement() {
  currentSpindle = -spindle.read();

  threadNumber = floor(currentSpindle / ticksPerRev);
  spindleMod = (currentSpindle % ticksPerRev) / (float)ticksPerRev;
  
  switch (nexCurrentPage)
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
      if (!btnKnob.read()) {
        switch (nex.currentPageId) {
          case pageMenu:
          case pageJogFeed:
            jFM = (imperial ? jogFeedMulti / 60: jogFeedMulti);
            jogFeedSpeed += (knobB.read() ? jFM : -jFM);
            if (imperial) {
              if (jogFeedSpeed < jFM) { jogFeedSpeed = jFM; }
              if (jogFeedSpeed > 0.3937) { jogFeedSpeed = 0.3937; }
            } else {
              if (jogFeedSpeed < jFM) { jogFeedSpeed = jFM; }
              if (jogFeedSpeed > 10) { jogFeedSpeed = 10; }
            }
            break;
          case pageThreading:

            if (imperial) {
              threadCount += knobB.read() ? 1 : -1;
              if (threadCount < 4) { threadCount = 4; }
            } else {
              threadCount += knobB.read() ? 0.05 : -0.05;
              if (threadCount < 0.05) { threadCount = 0.05; }
              if (threadCount > 4) { threadCount = 4; }
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
  static long target;
  static long offset;
  static long sp;

  lsDriver.setMaxSpeed(100000);

  if (threading) {
    if (threadDirection) {
      target = offset - spindleToStep((threadNumber - sp) + spindleMod + startOffset);
      if (target < offset) { target = offset; }
      if (target >= rightSteps) {
        target = rightSteps;
        threading = 0;
      }
    } else {
      target = offset + spindleToStep((threadNumber - sp) + spindleMod + startOffset);
      if (target > offset) { target = offset; }
      if (target <= leftSteps) {
        target = leftSteps;
        threading = 0;
      }
    }

    if (switchEnable.read() && btnLeft.read() && btnRight.read()) { threading = false; }
  
    lsDriver.moveTo(target);
  } else {
    if (switchEnable.read()) { threading = false; }
    if (!btnLeft.read() && leftStopOn) {
      threadDirection = false;
      threading = true;
      sp = threadNumber - 1;
      offset = lsDriver.currentPosition();
    } else if (!btnRight.read() && rightStopOn) {
      threadDirection = true;
      threading = true;
      sp = threadNumber - 1;
      offset = lsDriver.currentPosition();
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
  String buttonText;
  String statusText;

  static elapsedMillis tmrNextionUpdate;

  static String inputValue;

  nex.NextionListen();

  switch (nexCurrentPage)
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
  return String(abs(rpm));
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

void nexShowError(String title, String description, int returnPage) {

}

void nexInputPosition(String question, int returnPage, int var, String initialValue) {
  nex.writeStr("vis b15,1");
  nex.writeStr("vis b11,1");
  nex.writeStr("vis b3,1");
  inputPositionValue = initialValue;
  inputPositionLastpage = returnPage;
  inputPositionVar = var;
  nex.writeStr("input.value.txt", inputPositionValue);
  nex.writeStr("input.q.txt", question);
  nexGotoPage(pageInputPos);
}

void nexInputNumber(String question, int returnPage, int var, int initialValue) {
  nex.writeStr("vis b15,0");
  nex.writeStr("vis b11,0");
  nex.writeStr("vis b3,0");
  inputPositionValue = String(initialValue);
  inputPositionLastpage = returnPage;
  inputPositionLastpage = var;
  nex.writeStr("input.value.txt", inputPositionValue);
  nex.writeStr("input.q.txt", question);
  nexGotoPage(pageInputPos);
}

void nexGotoPage(int page)
{
  nexUpdatePage(page);
  nexCurrentPage = page;
  String i = "page ";
  nex.writeStr(i + String(page));
}

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
  }
}

void trigger0() { // handle positional input keypad buttons and display
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
      }
      //break; don't break, just go into cancel which changes the page back
    case keyCancel:
      nexGotoPage(inputPositionLastpage);
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
      nexGotoPage(inputPositionLastpage);
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

void trigger1() {
  switch (nex.readNumber("menu.key.val")) {
    case 0:
      nexGotoPage(pageJogFeed);
      break;
    case 1:
      nexGotoPage(pageThreading);
      break;
  }
}

void trigger2() { // call to get a full page update (update all data on screen for page switch)
  //nexUpdatePage(nexCurrentPage);
}


void trigger6() { // handle buttons on feed menu
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
      nexUpdatePage(nexCurrentPage);
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
      nexInputPosition("Left Stop Position (" + unitString(true) + ")", pageJogFeed, varLeftStop, floatToString(leftStop));
      break;
    case 8:
      nexInputPosition("Right Stop Position (" + unitString(true) + ")", pageJogFeed, varRightStop, floatToString(rightStop));
      break;
    case 9:
      nexGotoPage(pageMenu);
  }
}

void trigger7() {
  switch (nex.readNumber("threading.key.val")) {
    case 0: //pitch/tpi button
      invertUnits();
      nexUpdatePage(pageThreading);
      break;
    case 1:
      nexGotoPage(pageStarts);
      break;
    case 2:
      nexGotoPage(pageMenu);
      break;
    case 3:
      nexInputPosition("Left Stop Position (" + unitString(true) + ")", pageThreading, varLeftStop, floatToString(leftStop));
      break;
    case 4:
      nexInputPosition("Right Stop Position (" + unitString(true) + ")", pageThreading, varRightStop, floatToString(rightStop));  
      break;
    case 5:
      current = 0;
      lsDriver.setCurrentPosition(0);
      nexUpdatePage(pageThreading);
      break;
  }
}

void trigger8() {
  int val = nex.readNumber("starts.key.val");
  switch (val) {
    case 0: //ok
      startOffset = (1 / numStarts) * (start - 1);
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
