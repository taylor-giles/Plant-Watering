//Last Updated: Mar. 16, 2020

#include <DS3231.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define ERR_WATER_TIME 300 //Max watering time in seconds before error occurs
#define LCD_CHANGE_TIME 4000 //Number of milliseconds before LCD switches display screens

/**
 * Plant structure
 */
typedef struct Plant {
  String name;
  int pumpPin;
  int lightPin;
  int maxDryTime;
  int waterTime;
  int buttonPin;
  Time lastWatered;
  int lastWateredFor;
  boolean needsWater;
} Plant;

const int numPlants = 3; //Number of plants

Time nullTime; //Define a location in memory to serve as a "null time" (just for plant initialization)

//Plant objects
Plant gBonsai = {"LEFT BONSAI", 6, 4, 7200, 180, 12, nullTime, 0, false};
Plant bBonsai = {"RIGHT BONSAI", 7, 3, 4320, 180, 11, nullTime, 0, false};
Plant succulent = {"SUCCULENT", -1, 2, 10080, 90, 10, nullTime, 0, false};

//Array of plants
Plant plants[numPlants] {gBonsai, bBonsai, succulent};

//Other variables
const boolean DEBUGGING = true; //Debug mode

boolean error = false; //Error mode
boolean watering = false; //Watering mode

int plantCounter = 0; //Index of current plant being processed
Plant currentPlant; //Current plant being processed

//LCD Variables
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); //LCD
int lcdCounter = numPlants; //Index of current plant being displayed by LCD
Time lcdLastChange;

//Drop image bitmap
byte drop[8] = {
  B00100,
  B01010,
  B01010,
  B10001,
  B10001,
  B10001,
  B01110,
  B00000
};
//x image bitmap
byte nodrop[8] = {
  B00000,
  B00000,
  B01010,
  B00100,
  B01010,
  B00000,
  B00000,
  B00000
};

//Time variables
DS3231 rtc(SDA, SCL); //RTC address is at 0x68 (irrelevant?)
Time waterStartTime; //Time at which the last watering process began
Time currentTime; //The current time, updated with every cycle through loop()

//Pins
const int statusLED = 9;
const int errorLED = 8;
const int errorResetButton = 10;
const int switchPin = 13;

void setup() {
  //Serial output for debugging
  if (DEBUGGING) {
    Serial.begin(9600);
    Serial.println("Debugging ready.");
  }

  //Define pin modes
  pinMode(statusLED, OUTPUT);
  pinMode(errorLED, OUTPUT);
  pinMode(errorResetButton, INPUT);
  pinMode(switchPin, INPUT);

  //Start RTC
  rtc.begin();

  //Start LCD
  lcd.begin(20, 4);
  lcd.clear();
  lcd.createChar(0, drop);
  lcd.createChar(1, nodrop);

  //Initialize errorLED as off
  digitalWrite(errorLED, LOW);

  //Initialize all times
  lcdLastChange = rtc.getTime();
  for(int i = 0; i < numPlants; i++){
    plants[i].lastWatered = rtc.getTime();
  }
}

void loop() {
  currentPlant = plants[plantCounter];
  currentTime = rtc.getTime(); //Update the current time

  //Print time if debugging
  if (DEBUGGING) {
    Serial.print("Current time:  ");
    Serial.print(currentTime.hour);
    Serial.print(":");
    Serial.println(currentTime.min);
    Serial.print(currentTime.mon);
    Serial.print("/");
    Serial.println(currentTime.date);
  }

  //Determine whether or not this plant needs water
  currentPlant.needsWater = (elapsedTime(currentTime, currentPlant.lastWatered) >= currentPlant.maxDryTime);

  //If this plant does need water, turn on its LED, otherwise, turn it off
  digitalWrite(currentPlant.lightPin, currentPlant.needsWater);

  //If this plant's button is pressed, start watering
  if (digitalRead(currentPlant.buttonPin) == LOW) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("********************");
    lcd.setCursor(0, 1);
    lcd.print("** RELEASE BUTTON **");
    lcd.setCursor(0, 2);
    lcd.print("** TO WATER PLANT **");
    lcd.setCursor(0, 3);
    lcd.print("********************");

    //Wait until button is released
    while (digitalRead(currentPlant.buttonPin) == LOW){}

    //Turn on watering mode
    watering = true;
    lcd.clear();
  }

  if (watering) {
    //Turn on pump
    digitalWrite(currentPlant.pumpPin, HIGH);

    //Display watering message
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("***** WATERING *****");
    lcd.setCursor(0, 1);
    lcd.print(currentPlant.name);

    //Initialize time variables
    long wateringTime = 0;
    waterStartTime = rtc.getTime();

    //Start watering
    while (watering) {
      currentTime = rtc.getTime();
      wateringTime = elapsedTime(currentTime, waterStartTime);

      //If the watering time has elapsed, or the button was pressed, stop watering
      if (wateringTime >= currentPlant.waterTime || digitalRead(currentPlant.buttonPin) == LOW) {
        watering = false;
        break;
      }

      //If watering time has exceeded error time, start error mode and stop watering
      if (wateringTime >= ERR_WATER_TIME) {
        error = true;
        watering = false;
        break;
      }

      //Update display time
      lcd.setCursor(0, 2);
      lcd.print((String)elapsedTime(currentTime, waterStartTime) + " secs");
    }

    //When watering process is finished...
    digitalWrite(currentPlant.pumpPin, LOW); //Turn off pump
    lcd.clear(); //Clear LCD

    //Set plant variables
    currentPlant.lastWatered = waterStartTime;
    currentPlant.lastWateredFor = wateringTime;

  } else { //If not watering, move on to check next plant
    plantCounter++;
    if (plantCounter >= numPlants) {
      plantCounter = 0;
    }
  }

  //Handle error state
  if(error){
    //Turn on error light
    digitalWrite(errorLED, HIGH);

    //Turn off every pump
    for(int i = 0; i < numPlants; i++){
      digitalWrite(plants[i].pumpPin, LOW);
    }

    //Display error message
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("****** ERROR: ******");
    lcd.setCursor(0, 1);
    lcd.print("* WATERED TOO LONG *");
    lcd.setCursor(0, 2);
    lcd.print("*** PRESS BUTTON ***");
    lcd.setCursor(0, 3);
    lcd.print("***** TO RESET *****");

    //Wait for reset
    while(error){
      //If button is pressed, reset the error
      if (digitalRead(errorResetButton) == LOW) {
        error = false;
        lcd.clear();
      }
    }
  }

  //Handle LCD printing
  if(elapsedTime(currentTime, lcdLastChange) >= LCD_CHANGE_TIME){
    lcdCounter++;
    if(lcdCounter == numPlants){
      lcdCounter = 0;
    }
    lcd.clear();
  }
  lcdPrint(plants[lcdCounter]);
}

/**
 * Prints information about the program to serial port
 */
void debugPrint(){
  Serial.print("Current plant: ");
  Serial.println(currentPlant.name);
  Serial.println();

  if(watering){
    Serial.print("Watering: ");
    Serial.print(currentPlant.name);
  }

}

/**
   Returns the number of seconds between startTime and endTime
*/
long elapsedTime(Time endTime, Time startTime) {
  return (rtc.getUnixTime(endTime) - rtc.getUnixTime(startTime)) / 1000;
}

/**
 * Prints a report about the given plant on the LCD
 */
void lcdPrint(Plant p){
  currentTime = rtc.getTime();
  
  lcd.setCursor(0, 0);
  lcd.print(p.name);

  //Droplet icons
  lcd.setCursor(16,0);
  lcd.print(" ");

  //Droplet 1
  if((double)elapsedTime(currentTime, p.lastWatered) < 1.0){
    lcd.write((uint8_t)0); //Draw drop
  } else {
    lcd.write((uint8_t)1); //Draw nodrop
  }

  //Droplet 2
  if((double)elapsedTime(currentTime, p.lastWatered) < .67){
    lcd.write((uint8_t)0); //Drop
  } else {
    lcd.write((uint8_t)1); //Nodrop
  }

  //Droplet 3
  if((double)elapsedTime(currentTime, p.lastWatered) < .33){
    lcd.write((uint8_t)0); //Drop
  } else {
    lcd.write((uint8_t)1); //Nodrop
  }

  //Plant information
  if(p.pumpPin >= 0){ //If this plant has a pump
    //Last time watered
    lcd.setCursor(0,1);
    lcd.print(timeOutput(p.lastWatered));
  
    //Last water duration
    lcd.setCursor(0,2);
    lcd.print(" for ");
    lcd.print(p.lastWateredFor);
    lcd.print(" secs");
  }

  //Water-need information
  lcd.setCursor(0,3);
  currentTime = rtc.getTime();
  if(p.needsWater){
    lcd.print("Needs to be watered!");
  } else {
    lcd.print((String)(((p.maxDryTime * 3600) - elapsedTime(currentTime, p.lastWatered)) / 3600));
    lcd.print("/");
    lcd.print((String)(p.maxDryTime));
    lcd.print(" hrs left");
  }
}

/**
 * Converts a given time into LCD-printable output
 */
String timeOutput(Time t){
  String out = "";
  switch (t.dow) {
    case 1: out += "Mon, "; break;
    case 2: out += "Tue, "; break;
    case 3: out += "Wed, "; break;
    case 4: out += "Thu, "; break;
    case 5: out += "Fri, "; break;
    case 6: out += "Sat, "; break;
    case 7: out += "Sun, "; break;
    default: out += "ERROR"; break;
  }
  switch (t.mon) {
    case 1: out += "Jan "; break;
    case 2: out += "Feb "; break;
    case 3: out += "Mar "; break;
    case 4: out += "Apr "; break;
    case 5: out += "May "; break;
    case 6: out += "Jun "; break;
    case 7: out += "Jul "; break;
    case 8: out += "Aug "; break;
    case 9: out += "Sep "; break;
    case 10: out += "Oct "; break;
    case 11: out += "Nov "; break;
    case 12: out += "Dec "; break;
    default: out += "ERROR"; break;
  }

  out += ((String)t.date);
  out += ", ";
  out += ((String)t.hour);
  out += ":";
  if (t.min < 10) {
    out += "0";
  }
  out += ((String)t.min);
}
