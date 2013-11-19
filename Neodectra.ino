/******************************************************************************
* Neodectra - Unofficial compatible firmware for use with Audectra [http://www.audectra.com/]
*
* Firmware written by Brandon Wooldridge [brandon+neodectra@hive13.org]
* Audectra written by A1i3n37 [support@audectra.com]
*
* This program is provided as is. People with epilepsy should exercise extreme
*   caution when using this firmware. All rights to Audectra are retained by
*   A1i3n37 and his team. Neodectra is an independent project, and does not
*   represent or claim to represent Audectra in any way, shape, or form.
*
* FILE: Neodectra.ino
* DESC: Our main codebase for the Arduino
*
******************************************************************************/

// INCLUDES
#include "FastSPI_LED2.h"
#include <SD.h>
#include "config.h"

// STRUCTS
typedef struct
{
  unsigned long prevTime;
  unsigned long currTime;
  boolean		Increment;
} DelayTimer;

// FUNCTION PROTOTYPES
void fadeAfterDelay( DelayTimer *dTimer, uint8_t fDelay, uint8_t fPercentage );

// CONFIG: TESTING
#define	CS_PIN			10
#define	LOGFILE			"AUDECTRA.TXT"
#define POTPIN			A0
#define POT_READ		0  // Read from POT connected to POTPIN instead of having a constant value. 0 = disabled, 1 = enabled

// GLOBAL VARIABLES
CRGB ledStrip[STRIP_LENGTH];
DelayTimer UpdateDelay, LFODelay, FadeDelay, VUDelay;
uint8_t Offset = 0;
boolean Increase;
boolean SDLogging = false;
File logFileHandle;

void setup() {
  LEDS.setBrightness(255);  // between 0 and 255, default 25% brightness (64)
  LEDS.addLeds<CHIPSET, DATAPIN, PIXEL_ORDER>(ledStrip, STRIP_LENGTH);  // initialize our LED object
  memset(ledStrip, 0,  STRIP_LENGTH * sizeof(struct CRGB));  // set all LEDs to off

  pinMode(DATAPIN, OUTPUT);  // setup serial interface for communication
  pinMode(CS_PIN, OUTPUT);   // setup pin for sd card logger

  Serial.begin(BAUDRATE);

  // initialize our SD interface, and if it fails, provide visual feedback and disable logging
  if( !SD.begin(CS_PIN) || !SDLogging ) {
    memset(ledStrip, 100, STRIP_LENGTH * sizeof(struct CRGB));
    SDLogging = false;
  }

  if( SDLogging && SD.exists(LOGFILE) ) SD.remove(LOGFILE);  // delete logfile if it exists on the filesystem

  UpdateDelay.prevTime = 0;
  LFODelay.prevTime = 0;

  FastLED.show();  // update our strip, to blank out everything
}

void loop() {
  UpdateDelay.currTime = LFODelay.currTime = FadeDelay.currTime = VUDelay.currTime = millis();

  if( Serial.available () > 4 ) {
    if( SDLogging ) logFileHandle = SD.open(LOGFILE, FILE_WRITE);
    char buffer[4];
    for( int i = 0; i < 4; i++ ) buffer[i] = '\0';
	

    Serial.readBytes(buffer,4);  // read in our color values from the Audectra client

    if( UpdateDelay.currTime - UpdateDelay.prevTime > SAMPLERATE ) {
      UpdateDelay.prevTime = UpdateDelay.currTime;
      
      /*** THIS IS WHERE YOU SET THE EFFECTS. UNCOMMENT THE EFFECT YOU WANT THEN COMMENT THE OLD ONE ***/
      //colorSetAll( buffer[0], buffer[1], buffer[2] );
      colorSetSplit( buffer[0], buffer[1], buffer[2], &Offset );
      //colorSetVU( buffer[0], buffer[1], buffer[2], MASTER_GAIN );
      //colorSetVUSplit( buffer[0], buffer[1], buffer[2] );
      //colorSetGridX( buffer[0], buffer[1], buffer[2], 16, 18, &Offset );

      if( LFODelay.currTime - LFODelay.prevTime > LFO_RATE ) {
        LFODelay.prevTime = LFODelay.currTime;
        lfoCalc(2, &Offset, &Increase);
      }
    }

    FastLED.show();
    logFileHandle.close();
  }
  else {
    // if we've received no data from the serial port, start fading display until all pixels are 0, or we receive data
    if( ((FadeDelay.currTime - FadeDelay.prevTime) > FADE_DELAY) && (FADE_DELAY != 0) ) {
      FadeDelay.prevTime = FadeDelay.currTime;
      for( int pixel = 0; pixel < STRIP_LENGTH; pixel++ ) {
        ledStrip[pixel].fadeToBlackBy( FADE_PERCENT );
      }
      FastLED.show();
  }
  }
}

// VISUAL EFFECTS

// set all LEDs in a strip to the same color
void colorSetAll(uint8_t Red, uint8_t Green, uint8_t Blue) {
  for( int i = 0; i < STRIP_LENGTH; i++ ) ledStrip[i] = CRGB( Red, Green, Blue );
}

// split the strip into pixel groups of three and update red, green, and blue as determined by switch function
void colorSetSplit(uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t* DelayOffset) {
  for( int i = 0; i < STRIP_LENGTH; i++ ) {
    int currentPixel = (i + *DelayOffset) % STRIP_LENGTH;

    switch ( i % 3 ) {
    case 0:
      ledStrip[currentPixel] = CRGB( Red, 0, 0 );
      break;
    case 2:
      ledStrip[currentPixel] = CRGB( 0, Green, 0 );
      break;
    case 1:
      ledStrip[currentPixel] = CRGB( 0, 0, Blue );
      break;
    }
  }
}

// Audectra Settings: Bass[85,8] - Mid[65,16] - High[40,32], ymmv
void colorSetVU(uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t vuGain) {
  unsigned int maxBounds = map(volCalc(&Red, &Green, &Blue, vuGain), 0, (255*3), 0, STRIP_LENGTH);
  unsigned int newBounds = map(volCalc(&Red, &Green, &Blue, vuGain), 0, (255*3), 0, STRIP_LENGTH);
  (abs(newBounds - maxBounds) > STRIP_LENGTH/32) ? maxBounds = newBounds : maxBounds -= 5;
  
  if( VUDelay.currTime - VUDelay.prevTime > VU_DELAY ) {
    for( int i = 0; i <= STRIP_LENGTH; i++ ) {
      ledStrip[i] = CRGB( Red, Green, Blue );
      if (i > maxBounds) ledStrip[i] = CRGB( 0, 0, 0 );
    }
  }
}

// VU meter effect, shoots from the middle outward in both directions
void colorSetVUSplit(uint8_t Red, uint8_t Green, uint8_t Blue) {
  unsigned int maxBounds = map( volCalc(&Red,&Green,&Blue,MASTER_GAIN), 0, readPot(), 0, STRIP_CENTER );
  /*logFileHandle.println("maxBounds: " + String(maxBounds) + " [" + String(Red) + "," + String(Green) + "," + String(Blue) + \
   "] [MAX: " + String(MAX_VOLUME_RANGE) + "] CNTR:" + String(STRIP_CENTER) + "]"); */

  maxBounds = constrain(maxBounds, 0, STRIP_CENTER);  // may not be needed but leaving for now

  // calculate the pixel we're on, starting from the center. set right half of strip to proper values. mirror to left side of strip
  // NOTE: does not handle an odd number of pixels, will have an off by one error
  for( int i = 0; i < STRIP_CENTER; i++ ) {
    ledStrip[STRIP_CENTER + i] = CRGB( Red, Green, Blue );  // right
    if( i > maxBounds ) ledStrip[STRIP_CENTER + i] = CRGB( 0, 0, 0 );
    ledStrip[(STRIP_CENTER - 1) - i] = ledStrip[STRIP_CENTER + i];  // left
  }
}

// Preliminary grid function, tested with a 9x9 array. (y - 1) * 9 + x
void colorSetGridX(uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t xSize, uint8_t ySize, uint8_t* Offset) {
  for( int xPosition = 0; xPosition < xSize; xPosition++ ) {
    for( int yPosition = 1; yPosition <= ySize; yPosition++ ) {
      uint8_t currentPixel = ((yPosition - 1) * xSize) + xPosition;
      switch (xPosition % 3) {
        case 0:
          (yPosition % 2) ? ledStrip[(currentPixel) % STRIP_LENGTH] = CRGB( Red, 0, 0 ) : ledStrip[((currentPixel) + 2) % STRIP_LENGTH] = CRGB( Red, 0, 0 );
          break;
        case 1:
          ledStrip[(currentPixel + *Offset) % STRIP_LENGTH] = CRGB( 0, Green, 0 );
          break;
        case 2:
          (yPosition % 2) ? ledStrip[(currentPixel) % STRIP_LENGTH] = CRGB( 0, 0, Blue ) : ledStrip[(currentPixel - 2) % STRIP_LENGTH] = CRGB( 0, 0, Blue );
          break;
      }
    }
  }
}


// UTILITY FUNCTIONS
unsigned int readPot() {
  unsigned int readVal = map(analogRead(POTPIN), 0, 1023, 0, MAX_VOLUME_RANGE);
  if( !POT_READ ) readVal = 3200;  // magic value that seems to work best
  if( SDLogging ) logFileHandle.println("potval: " + String(readVal));
  return readVal;

  //return map(analogRead(POTPIN), 0, 1023, 0, MAX_VOLUME_RANGE);
}

unsigned int volCalc(uint8_t* freqHi, uint8_t* freqMid, uint8_t* freqLow, uint8_t MasterGain) {
  // return a "Volume" level by combining all of our color codes, then multiplying them by a gain
  return ( ((*freqHi * HI_GAIN) + (*freqMid * MID_GAIN) + (*freqLow * LOW_GAIN)) / 3) * MasterGain;
}

// lfoCalc: counts up to lfoMax, then starts counting down, then does it again
void lfoCalc(uint8_t lfoMax, uint8_t* OffsetVal, boolean* IncSwitch) {
  // check our switch, then increment or decrement our value as needed
  (*IncSwitch) ? *OffsetVal +=1 : *OffsetVal -=1;
	
  // figure out if we should start increasing or decreasing
  if (*OffsetVal == 0) *IncSwitch = true;
  if (*OffsetVal == lfoMax) *IncSwitch = false;
}

void fadeAfterDelay( DelayTimer *dTimer, uint8_t fDelay, uint8_t fPercentage ) {
  if( ((dTimer->currTime - dTimer->prevTime) > fDelay) && (fDelay != 0) ) {
    dTimer->prevTime = dTimer->currTime;
    for( int pixel = 0; pixel < STRIP_LENGTH; pixel++ ) {
      ledStrip[pixel].fadeToBlackBy( fPercentage );
    }
  }
}

/*->
RGBRGBRGB
       <-
BGRBGRBGR
->
RGBRGBRGB
       <-
BGRBGRBGR
*/
