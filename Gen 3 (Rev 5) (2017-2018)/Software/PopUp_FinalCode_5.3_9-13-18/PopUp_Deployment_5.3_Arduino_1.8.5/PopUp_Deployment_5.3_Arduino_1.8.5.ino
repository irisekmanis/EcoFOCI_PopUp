//include various libraries
#include <ctype.h>
#include <math.h>                      //Library for math functions used in calculating sensor values
#include <Wire.h>                      //Library for I2C Comms
#include <SPI.h>                       //Library for SPI Comms
#include <SD.h>                        //Library for SD Card
#include <RTClib.h>                    //Library for RTC Functions
#include <RTC_DS3234.h>                //Library for DS3234 RTC
#include <MemoryFree.h>                //Library to determine amount of free RAM while program is running
#include <TinyGPS++.h>                 //Library for interfacing with GPS module
#include <IridiumSBD.h>                //Library for Iridium interface - rockBlock Mk3

#define TEMP_SWITCH_1 3                //Define pin for switching between temp sensors and reference thermistors
#define TEMP_SWITCH_2 4                //Define pin for switching between temp sensors and reference thermistors
#define SENSOR_POWER_5V 6              //Define pin for switching on power to 5V sensors
#define SENSOR_POWER_3V3 32            //Define pin for switching on power to 3.3V sensors
#define GPS_POWER 9                    //Define pin for switching on GPS
#define IRIDIUM_POWER 8                //Define pin for switching on Iridium power
#define IRIDIUM_ON_OFF 11              //Define pin for switching on Iridium module
#define ACCEL_CS A8                    //Define pin for accelerometer SPI chip select
#define SDCARD_CS A5                   //Define pin for sdcard SPI chip select
#define RTC_CS A2                      //Define pin for RTC SPI chip select
#define SHUTDOWN A0                    //Define pin for shutting down unit
#define FLUOROMETER_X10 24             //Define pin for setting Fluorometer Sensitivity to X10
#define FLUOROMETER_X100 26            //Define pin for setting Fluorometer Sensitivity to X100
#define FLUOROMETER_ON_OFF 28          //Define pin for switching on Fluorometer
#define CAMERA_RESET 13                //Define pin for switching on Camera reset function
#define CAMERA_ON_OFF 12               //Define pin for switching on Camera module
#define SERIAL_COMMS_ON_OFF 2          //Define pin for switching on Serial Comms chip (5V to 3.3V translator)

#define kellerADDR 0x41       	       	        //Define pressure sensor control register
#define kellerCheckPressure 0xAC       	        //Define pressure sensor check pressure command
#define ads1100A0 B1001000     		   	          //Define temperature ADC control register
#define ads1100A1 B1001001             	        //Define PAR ADC control register
#define ads1100A2 B1001010             	        //Define Fluorometer ADC control register

#define RECEIVE_BUFFER_SIZE 15		   	          //Size of buffer to receive Iridium Messages
#define CHARS_PER_SBDMESSAGE 120       	        //Number of bytes to pull from data file and send at once   (nominal CHARS_PER_SBDMESSAGE 120 = 120 bytes)
#define BYTES_PER_SBDMESSAGE 338       	        //Number of bytes to pull from data file and send at once   (nominal BYTES_PER_SBDMESSAGE 338 =  338 bytes)
#define GPS_FIX_SEARCH_SECONDS 120              //Number of seconds to search for GPS fix                   (nominal GPS_FIX_SEARCH_SECONDS 120 = 2 minutes)
#define GPS_SATELLITE_SEARCH_SECONDS 20	        //Number of seconds to search for GPS satellite             (nominal GPS_SATELLITE_SEARCH_SECONDS 10 = 10 seconds)
#define SEND_DATA_INTERVAL 1200                 //Default interval to send data once GPS has been found     (nominal SEND_DATA_INTERVAL 1200 = 20 minutes)
#define BOTTOM_SAMPLE_INTERVAL 3600	  		      //Interval between bottom samples 			                	  (nominal BOTTOM_SAMPLE_INTERVAL 3600 = 1 hour)
#define PRE_RELEASE_WAKEUP_INTERVAL 7200		    //How long before planned release to wait for profile 	    (nominal PRE_RELEASE_WAKEUP_INTERVAL 7200 = 2 hours)
#define UNDER_ICE_SAMPLE_INTERVAL 3600			    //Interval between samples at the surface (or uncer ice)	  (nominal UNDER_ICE_SAMPLE_INTERVAL 3600	= 1 hour)
#define DRIFT_MODE_SAMPLE_INTERVAL 10800			  //Interval between samples at the surface (or uncer ice)	  (nominal DRIFT_MODE_SAMPLE_INTERVAL 10800	= 3 hours)
#define PROFILE_LENGTH_SECONDS 90               //Length of profile in seconds                              (nominal PROFILE_LENGTH_SECONDS 90 = 90 seconds)
#define TIMEZONE -9                            //
#define DEPTH_CHANGE_TO_TRIP_PROFILE 2           //Depth in meters needed to begin profile mode            (nominal DEPTH_CHANGE_TO_TRIP_PROFILE 2 = 2 meters)
#define IMAGE_HOUR 15           //            

HardwareSerial &IridiumSerial = Serial1;        //Define Alias for Iridium Serial Comms (Arduino MEGA Serial1)
HardwareSerial &CameraSerial = Serial2;         //Define Alias for Iridium Serial Comms (Arduino MEGA Serial2)
HardwareSerial &GPSSerial = Serial3;            //Define Alias for Iridium Serial Comms (Arduino MEGA Serial3)

IridiumSBD isbd(IridiumSerial,IRIDIUM_ON_OFF);	//Object for Iridium Module
TinyGPSPlus gps;			                      	  //Object for GPS Module
RTC_DS3234 RTC(RTC_CS);			                	  //Object for RTC

DateTime wakeupTime;				                    //Global Variable for when unit first wakes
DateTime alarmtime;			                    	  //Global Variable for setting RTC Alarm
boolean foundGPS=false; 		                	  //Global Variable for GPS lock success
uint16_t fileSelection=0;                       //Global Variable for marking which file to send data from
uint32_t filePosition=0;		       		      	  //Global variable to designate position in file when sending data
boolean newFile=true;                     		  //Global variable for marking when a file has completed transmitting
boolean beginSDsuccess;                   		  //Global variable for informing user if SDCard initialized sucessfully

static const int MAX_SATELLITES = 73;

TinyGPSCustom totalGPGSVMessages(gps, "GPGSV", 1); // $GPGSV sentence, first element
TinyGPSCustom messageNumber(gps, "GPGSV", 2);      // $GPGSV sentence, second element
TinyGPSCustom satsInView(gps, "GPGSV", 3);         // $GPGSV sentence, third element
TinyGPSCustom satNumber[4]; // to be initialized later
TinyGPSCustom elevation[4];
TinyGPSCustom azimuth[4];
TinyGPSCustom snr[4];

struct{
  bool active;
  int elevation;
  int azimuth;
  int snr;
} 
sats[MAX_SATELLITES];


//***************************************************************************************************************************************************************************//
//***SET DATES AND PROGRAM INTERVALS HERE************************************************************************************************************************************//
//***************************************************************************************************************************************************************************//
DateTime unitStart =    DateTime(18,9,22,0,0,0);            //Date and Time of first sample: 				                  DateTime(YEAR,MON,DAY,HOUR,MIN,SEC);
DateTime releaseTime =  DateTime(19,3,1,15,0,0);          	 //Date and Time of actual release of burn wire: 	      	DateTime(YEAR,MON,DAY,HOUR,MIN,SEC);
//**************************************************************************************************************************************************************************//
//***END PROGRAM DATES AND SAMPLE INTERVAL SECTION**************************************************************************************************************************//
//**************************************************************************************************************************************************************************//

void setup(){
  initializePins();                             //Initialize Arduino Pins for Output and Set Voltage Levels
  Wire.begin();                                 //Start I2C Communication
  SPI.begin();                                  //Start up SPI Communication (needed for various ICs) 
  SPI.setBitOrder(MSBFIRST);                    //Configure SPI Communication to Most Significant Bit First
  SPI.setClockDivider(SPI_CLOCK_DIV4);          //Set SPI Clock Speed

  Serial.begin(115200);  
  setupRTCandSD();                              //Start and configure RTC and SD Card 
  setupAccel();                                 //Start and configure Accelerometer
  turnOn3v3SensorPower();                       //turn on 3.3V Power to sensors (PAR, TEMP, PRESSURE)
  turnOn5vSensorPower();                        //turn on 5V Power to sensors 
  delay(100);                                    //Short delay here to let voltages stabilize before configuring ADCs
  setupTempADC();                               //Set Up the ADC for Temperature Measurement
  setupPARADC();                                //Set Up the ADC for PAR Measurement
  setupFluorADC();                              //Set Up the ADC for Fluorometer Measurement
  delay(500);                                   //Longer delay here to let voltages stabilize

  uint8_t mode = selectModeandSetAlarm();	      //Select mode based on current time and programmed dates and set the alarm
  uint32_t timestamp = wakeupTime.unixtime();   //timestamp variable for reducing number of bytes transmitted
    
  if(mode==1){                                                                              //if in bottom sampling mode
	  if((wakeupTime.unixtime()-unitStart.unixtime())%BOTTOM_SAMPLE_INTERVAL==0){             //if wakeuptime is an even timestamp interval
		  timestamp=(wakeupTime.unixtime()-unitStart.unixtime())/BOTTOM_SAMPLE_INTERVAL;        //calculate the number of intervals passed rather than the timestamp (fewer bytes to transmit)
	  }
  }
  if(mode==4||mode==6){                                                                     //if in under ice sampling or SST Drifter mode
	  if((wakeupTime.unixtime()-releaseTime.unixtime())%UNDER_ICE_SAMPLE_INTERVAL==0){        //if wakeuptime is an even timestamp interval
		  timestamp=(wakeupTime.unixtime()-releaseTime.unixtime())/UNDER_ICE_SAMPLE_INTERVAL;   //calculate the number of intervals passed rather than the timestamp (fewer bytes to transmit)
	  }
  }
  
  if(mode>0){
    sampleData(mode, timestamp);    	//Sample sensors and store data set 
  }
  
  if(mode==0){			                  //Mode 0 = Before Initial sample date
    displayDeploymentParameters();    //Display Deployment Parameters to Serial window for user on initial setup
    shutdownUnit();			    	        //Turn off power and wait
  }
  else if(mode==1){			              //Mode 1 = On Bottom, Not waiting for release 
    shutdownUnit();			              //Turn off power and wait
  }
  else if(mode==2){			              //Mode 2 = On Bottom, waiting for profile to begin
    waitForProfile();  			          //continuously monitor depth waiting for change to signal start of profile
    collectProfile();			            //collect samples and store data at high sample rate for pre-determined length of time
    shutdownUnit();			    	        //Turn off power and wait
  }
  
  //mode 3 is profile collection, triggered by depth change while in mode 2
  
  else if(mode==4||mode==5){			    //Mode 4 = Under Ice, Mode 5 = Sending Under Ice Data
    underIceAndSendDataMode();  	    //take a sample when unit wakes up, look for gps satellite, send any available data if satellite is found
    shutdownUnit();			    	        //Turn off power and wait
  }
  else if(mode==6){			              //Mode 6=SST Drifter
    sstDrifterMode();  		            //take a sample when unit wakes up, look for a gps FIX, try to send any available data, regardless of GPS Fix status
    shutdownUnit();			    	        //Turn off power and wait
  }
}

void loop(){
  shutdownUnit();
 }

void initializePins(){
  pinMode(SHUTDOWN,OUTPUT);               //SHUTDOWN PIN for turning unit off (Sleep mode)
  digitalWrite(SHUTDOWN,HIGH);            //SHUTDOWN PIN HIGH keeps unit's power on
  pinMode(TEMP_SWITCH_1,OUTPUT);          //TEMP_SWITCH PIN for switching between thermistors and reference resistor
  digitalWrite(TEMP_SWITCH_1,LOW);       //TEMP_SWITCH PIN HIGH for reading reference resistor
  pinMode(TEMP_SWITCH_2,OUTPUT);          //TEMP_SWITCH PIN for switching between thermistors and reference resistor
  digitalWrite(TEMP_SWITCH_2,LOW);       //TEMP_SWITCH PIN HIGH (Doesn't matter when reference resistor is selected)
  pinMode(SENSOR_POWER_5V,OUTPUT);        //SENSOR_POWER_5V PIN for turning 5V sensor power on and off
  digitalWrite(SENSOR_POWER_5V,HIGH);     //SENSOR_POWER_5V PIN HIGH for 5V sensor power off (P-Channel Transistor)
  pinMode(SENSOR_POWER_3V3,OUTPUT);       //SENSOR_POWER_3V3 PIN for turning 3.3V power on and off
  digitalWrite(SENSOR_POWER_3V3,HIGH);    //SENSOR_POWER_3V3 PIN HIGH for 3.3V sensor power off (P-Channel Transistor)
  pinMode(GPS_POWER,OUTPUT);              //GPS_POWER PIN for turning power to GPS on and off
  digitalWrite(GPS_POWER,HIGH);           //GPS_POWER PIN HIGH for GPS power off (P-Channel Transistor)
  pinMode(IRIDIUM_POWER,OUTPUT);          //IRIDIUM_POWER PIN for turning power to Iridium on and off
  digitalWrite(IRIDIUM_POWER,HIGH);       //IRIDIUM_POWER PIN HIGH for Iridium power off (P-Channel Transistor)
  pinMode(IRIDIUM_ON_OFF,OUTPUT);         //IRIDIUM_ON_OFF PIN for Iridium sleep or awake
  digitalWrite(IRIDIUM_ON_OFF,LOW);       //IRIDIUM_ON_OFF PIN LOW for Iridium sleep mode
  pinMode(ACCEL_CS,OUTPUT);               //ACCEL_CS PIN for SPI communication to Accelerometer
  digitalWrite(ACCEL_CS,HIGH);            //ACCEL_CS PIN HIGH for SPI communication to Accelerometer inactive
  pinMode(SDCARD_CS,OUTPUT);              //SDCARD_CS PIN for SPI communication to SDCard
  digitalWrite(SDCARD_CS,HIGH);           //ACCEL_CS PIN HIGH for SPI communication to SDCard inactive
  pinMode(RTC_CS,OUTPUT);                 //RTC_CS PIN for SPI communication to RTC
  digitalWrite(RTC_CS,HIGH);              //ACCEL_CS PIN HIGH for SPI communication to RTC inactive
  pinMode(FLUOROMETER_X10,OUTPUT);        //FLUOROMETER_X10 PIN for Fluorometer sensitivity x10 enable and disable         
  digitalWrite(FLUOROMETER_X10,HIGH);     //FLUOROMETER_X10 PIN LOW for Fluorometer sensitivity x10       
  pinMode(FLUOROMETER_X100,OUTPUT);       //FLUOROMETER_X100 PIN for Fluorometer sensitivity x10 enable and disable       
  digitalWrite(FLUOROMETER_X100,LOW);     //FLUOROMETER_X100 PIN LOW for Fluorometer sensitivity x0        
  pinMode(FLUOROMETER_ON_OFF,OUTPUT);     //FLUOROMETER_ON_OFF PIN for Fluorometer on and off           
  digitalWrite(FLUOROMETER_ON_OFF,LOW);   //FLUOROMETER_X10 PIN HIGH for Fluorometer power on (P-Channel Transistor)          
  pinMode(CAMERA_RESET,OUTPUT);           //CAMERA_RESET PIN for camera hardware reset  
  digitalWrite(CAMERA_RESET,LOW);         //CAMERA_RESET PIN for LOW camera hardware reset inactive
  pinMode(CAMERA_ON_OFF,OUTPUT);          //CAMERA_ON_OFF PIN for camera on and off     
  digitalWrite(CAMERA_ON_OFF,HIGH);       //CAMERA_ON_OFF PIN HIGH for Fluorometer power off (P-Channel Transistor) 
  pinMode(SERIAL_COMMS_ON_OFF,OUTPUT);    //SERIAL_COMMS_ON_OFF PIN for SPI communication to SDCard          
  digitalWrite(SERIAL_COMMS_ON_OFF,LOW);  //SERIAL_COMMS_ON_OFF PIN HIGH for Serial Comms off            
}

void setupRTCandSD(){
  SPI.setDataMode(SPI_MODE1);		          //Set SPI Data Mode to 1 for RTC
  RTC.begin();                            //Start RTC (defined by RTC instance in global declarations)
  wakeupTime = RTC.now();                 //Get time from RTC and store into global variable for when unit awoke
  SPI.setDataMode(SPI_MODE0);             //Set SPI Data Mode to 0 for SDCard
  beginSDsuccess = SD.begin(SDCARD_CS);   //Start SD Card so it is available for other function calls 
}

uint8_t selectModeandSetAlarm(){
  alarmtime = wakeupTime.unixtime() + 86400UL;  		                                                                                            //failsafe if program freezes (t+1day)																				
  uint8_t modeSelect=0;  						                                                                                                            //initialize variable to select mode	

  if(wakeupTime.unixtime()<unitStart.unixtime()){               	                                                                              //if time is before first sample time, set alarm for first sample time and go to sleep																		
    modeSelect=0;
    alarmtime = unitStart.unixtime();
  }	
  else if((wakeupTime.unixtime()<(releaseTime.unixtime()-PRE_RELEASE_WAKEUP_INTERVAL))&&wakeupTime.unixtime()>=unitStart.unixtime()){           //if time is earlier than pre-release wakeup, just take bottom sample and go to sleep																								
    modeSelect=1;	
    uint32_t preReleaseWakeUpBuffer=PRE_RELEASE_WAKEUP_INTERVAL+BOTTOM_SAMPLE_INTERVAL+10UL;					                                          //must be at least this many seconds from release to take normal bottom sample.  10s buffer added so unit does not skip the correct wake up time
    if(wakeupTime.unixtime()<(releaseTime.unixtime()-preReleaseWakeUpBuffer)){          						                                            //If still far enough from release for a normal bottom sample, set alarm for time + BOTTOM_SAMPLE_INTERVAL     
      alarmtime = wakeupTime.unixtime()+BOTTOM_SAMPLE_INTERVAL;            				
    }   	
    else{    																                                                                                                    //If within 1 BOTTOM_SAMPLE_INTERVAL of pre release wake up, then set alarm for pre release wake up time                                                            
      alarmtime = releaseTime.unixtime()-PRE_RELEASE_WAKEUP_INTERVAL;          
    } 
  }
  else if((wakeupTime.unixtime()>=(releaseTime.unixtime()-PRE_RELEASE_WAKEUP_INTERVAL))&&(wakeupTime.unixtime()<=(releaseTime.unixtime()))){    //if time is equal or after pre-release wakeup but before or equal to release, then wait for profile
    modeSelect=2;
    uint32_t postReleaseWakeUp=PRE_RELEASE_WAKEUP_INTERVAL+UNDER_ICE_SAMPLE_INTERVAL;
    alarmtime = releaseTime.unixtime()+postReleaseWakeUp;       
  }  
  else if((wakeupTime.unixtime()>releaseTime.unixtime())){     		                                                                              //if after Release Date and Before Send Data Date then sample data and go to sleep. (Look for GPS once a day and Don't attempt to transmit data)
    readFilePositionSD();
    if(fileSelection==0){
      modeSelect=4;
      alarmtime = wakeupTime.unixtime()+UNDER_ICE_SAMPLE_INTERVAL;     	
    }
    else if(fileSelection!=0xFFFE){
      modeSelect=5;
      alarmtime = wakeupTime.unixtime()+UNDER_ICE_SAMPLE_INTERVAL;  
    }
    else{
      modeSelect=6;  
      alarmtime = wakeupTime.unixtime()+SEND_DATA_INTERVAL;   
    }
  }  
  setAlarmTime();			                                                                                                                          //Set the Alarm by writing to RTC registers
  return modeSelect; 		                                                                                                                        //return mode for program flow
}

void displayDeploymentParameters(){
  Serial.begin(115200);
  const uint8_t len = 32;                                                                               //buffer length for RTC display on Serial comm
  static char buf[len];                                                                                 //string for RTC display on Serial comm
  
  Serial.println(F("Unit Status:"));
  if(beginSDsuccess){
    Serial.println(F("  SD Card Initialized Successfully."));
  }
  else{
    Serial.println(F("  **SD Card error.  Check if SD Card is present. If SD Card is present reset Arduino Mega with loopback test."));
  }
  SPI.setDataMode(SPI_MODE1);                         	                                                //Set SPI mode to 1 for RTC 
  DateTime now = RTC.now();
  Serial.print(F("  Current RTC Time/Date: "));
  Serial.println(now.toString(buf,len));

  int tempValTop = readTopTemp();                                                                           //read voltage from top probe thermistor
  Serial.print(F("  Temp Sensor ADC Val: "));
  Serial.print(tempValTop);
  Serial.print(F(" (approx "));
  float tempCalc = (1 / (.0012768 + .00053964 * log10(tempValTop) + .0000011763 * pow(log10(tempValTop), 3))) - 273.15; //calculate estimated temperature based on Steinhart-hart equation
  Serial.print(tempCalc, 2);
  Serial.println(F(" C)"));

  int tempValSST = readSST();                                                                               //read voltage from sst probe thermistor
  Serial.print(F("  SST (Sea Surface Temp) Sensor ADC Val: "));
  Serial.print(tempValSST);
  Serial.print(F(" (approx "));
  float SSTCalc = (1 / (.0012768 + .00053964 * log10(tempValSST) + .0000011763 * pow(log10(tempValSST), 3))) - 273.15; //calculate estimated temperature based on Steinhart-hart equation
  Serial.print(SSTCalc, 2);
  Serial.println(F(" C)"));

  int tempRefVal = readTempRef();                                                                           //read voltage from reference thermistor
  Serial.print(F("  Temp Ref    ADC Val: "));
  Serial.print(tempRefVal);
  float tempRefCalc = (tempRefVal / 16384.0 * 3.0 / 4.0) * 39.872;                                              //calculate estimated resistance based on voltage output curve from LTSpice model
  Serial.print(F(" (approx "));
  Serial.print(tempRefCalc, 2);
  Serial.println(F(" kOhms)"));
  
  int16_t parVal = readPAR();
  Serial.print(F("  PAR  Sensor ADC Val: ")); 
  Serial.print(parVal);
  float parCalc = (parVal-5460)*0.065753;
  Serial.print(F(" (approx "));
  Serial.print(parCalc);
  Serial.println(F(" umol/(m2s))"));

  turnOnFluorometer();
  while(millis()<2000){}
  int16_t fluorVal = readFluor();
  turnOffFluorometer();
  Serial.print(F("  Fluorometer ADC Val: ")); 
  Serial.print(fluorVal);
  float fluorCalc = fluorVal;
  Serial.print(F(" (approx "));
  Serial.println(F("? ug/L)"));
  
  uint16_t pressureVal = readPressure();
  Serial.print(F("  100m Pressure Sensor Val: "));
  Serial.print(pressureVal);
  float pressureCalc = (pressureVal-16384.0)/327.68;
  Serial.print(F(" (approx "));
  Serial.print(pressureCalc);
  Serial.println(F(" m)"));
  
  uint8_t tiltAngle = readAccel();
  Serial.print(F("  Tilt Angle: "));
  Serial.print(tiltAngle);
  Serial.println(F(" degrees from horizontal")); 
       
  Serial.println(F("\nDeployment Schedule:"));
  Serial.println(F("--PHASE 1 (ON BOTTOM)--"));  
  Serial.println(F(" -This mode starts at the scheduled Start Time."));
  Serial.print(F("  Unit will start sampling ["));
  Serial.print(unitStart.toString(buf,len));
  Serial.print(F("] and take a sample every "));
  displayHoursMinsSecs(BOTTOM_SAMPLE_INTERVAL);
  
  Serial.println(F("\n--PHASES 2 AND 3 (WAIT FOR AND COLLECT PROFILE)--"));  
  Serial.println(F(" -This mode starts just before the scheduled Release Time."));
  Serial.print(F("  Release Expected ["));
  Serial.print(releaseTime.toString(buf,len));
  Serial.println(F("] (<--Set Burn Wire Release for this exact time/date.)"));
  Serial.print(F("  Unit will wake up  "));
  displayHoursMinsSecs(PRE_RELEASE_WAKEUP_INTERVAL);  
  Serial.println(F(" before scheduled release and wait for profile."));
  Serial.print(F("  Start of profile will be triggered by a change in depth of ("));
  Serial.print(DEPTH_CHANGE_TO_TRIP_PROFILE);
  Serial.println(F(" meters)."));
  Serial.print(F("  Profile will sample at 4 Hz for "));
  displayHoursMinsSecs(PROFILE_LENGTH_SECONDS);  
  Serial.println(F(" seconds before proceeding to phase 3. "));
  Serial.print(F("  If unit does not sense the release within  "));
  displayHoursMinsSecs(PRE_RELEASE_WAKEUP_INTERVAL*2UL);  
  Serial.println(F(" of waking it will assume it missed the profile and proceed to phase 3."));

  Serial.println(F("--PHASES 4 AND 5 (UNDER ICE AND TRANSMIT DATA)--"));  
  Serial.println(F(" -This mode starts after profile has been collected (or missed)."));
  Serial.print(F("  Unit will take a sample every "));
  displayHoursMinsSecs(UNDER_ICE_SAMPLE_INTERVAL);  
  Serial.println(F(" and take an image once a day."));
  Serial.print(F("  Unit will search for GPS satellites for "));
  displayHoursMinsSecs(GPS_SATELLITE_SEARCH_SECONDS);  
  Serial.println(F("  after every sample."));
  Serial.println(F("  If unit does successfully find a GPS satellite, it will attempt to send all collected data back in the following order:"));
  Serial.println(F("    1-File Summary, 2-Profile Data, 3-Under Ice Data, 4-Bottom Data, 5-Daily Images"));  
  Serial.print(F("  If any iridium messages send successfully, it will continue sending until one message fails and make the next attempt to send in "));  
  displayHoursMinsSecs(SEND_DATA_INTERVAL);  
  Serial.print(F("\n  Otherwise, if no messages send, the unit will take the next sample and attempt to send again in "));
  displayHoursMinsSecs(UNDER_ICE_SAMPLE_INTERVAL);  
  
  Serial.println(F("\n--PHASE 6 (SURFACE DRIFTER)--"));  
  Serial.println(F(" -This mode starts after all data has finished sending."));
  Serial.print(F("  Unit will take a sample every "));
  displayHoursMinsSecs(DRIFT_MODE_SAMPLE_INTERVAL);  
  Serial.print(F(" and search for a GPS fix for "));
  displayHoursMinsSecs(GPS_FIX_SEARCH_SECONDS);  
  Serial.print(F(" after every sample."));
  Serial.print(F("\n  Unit will attempt to send any new data regardless of whether a GPS fix was found"));


  Serial.println(F("\n\n**Check all settings, take a screenshot of this dialog, and disconnect from USB when ready.**"));
  Serial.print(F("**Set Burn Wire Release for ["));
  Serial.print(releaseTime.toString(buf,len));
  Serial.println(F("]**\n "));
  
  if(!beginSDsuccess){
    Serial.println(F("**WARNING!!! SD Card error.  Check if SD Card is present.**"));
  }
  if(now.unixtime()>unitStart.unixtime()){
    Serial.println(F("**WARNING!!! Start time/Date is before current time. Set start time/date to after current time to prevent errors.**"));
  }
  if(unitStart.unixtime()>releaseTime.unixtime()){
    Serial.println(F("**WARNING!!! Expected release is before start time/date. Check all time/date settings.**"));
  }
}

void displayHoursMinsSecs(uint32_t numSeconds){
  uint32_t displayIntervalHours=numSeconds/3600UL;
  uint32_t displayIntervalMinutes=(numSeconds%3600UL)/60UL;
  uint32_t displayIntervalSeconds=numSeconds%60UL;

  Serial.print(F("(")); 
  Serial.print(displayIntervalHours);
  Serial.print(F("h,")); 
  Serial.print(displayIntervalMinutes); 
  Serial.print(F("m,")); 
  Serial.print(displayIntervalSeconds);
  Serial.print(F("s)"));    
}

void sampleData(uint8_t modeSelected, uint32_t timestamp){
  uint16_t pressureVal = readPressure();   		  	            //Read 10 bar Pressure Sensor
  uint8_t tiltAngle = readAccel();		              	        //Get Tilt Angle from Accelerometer
  int16_t tempValTop = 0;	  	    				                    //Initialize Top Thermistor Value
  int16_t tempValSST = 0;	  	    				                    //Initialize SST Thermistor Value
  int16_t tempRefVal = 0;					          	                //Initialize Temperature Reference Value
  if(modeSelected!=3){					              	              //If not profiling, then check and store the temperature values (This takes 1 second for voltage to stabilize for each measurment switch. Profile must sample much faster and reference value is not needed at every sample for data quality)
    tempRefVal = readTempRef();                               //Read Temperature Reference Resistor Value
    tempValTop = readTopTemp();                               //Read Top Thermistor Value
    tempValSST = readSST();                                   //Read SST Thermistor Value
  }
  else{
    tempValTop = readTopTempProfile();                        //Read Top Thermistor Value
  }
  
  int16_t parVal = readPAR();		            	  	            //Read PAR Sensor
  int16_t fluorVal = readFluor();	  	    			              //Read fluorometer
  
  if(modeSelected!=3){                                        //Unless we are profiling
      turnOffFluorometer();                                   //turn off fluorometer
  }
  
  SPI.setDataMode(SPI_MODE0);                                 //Set SPI Data Mode to 0 for SDCard 
  File dataFile;					    		      	                    //Initialize dataFile
  
  if(modeSelected==1){                                        //If just a bottom sample
    dataFile = SD.open("botdat.txt", FILE_WRITE);             //open file with write permissions
  }
  else if(modeSelected==2||modeSelected==3){		  	          //if waiting for profile or profiling  
    dataFile = SD.open("prodat.txt", FILE_WRITE);             //open file with write permissions      
  }
  else if(modeSelected==4){		  	                            //If under ice or at surface
    dataFile = SD.open("icedat.txt", FILE_WRITE);             //open file with write permissions
  }
  else if(modeSelected==5||modeSelected==6){		  	          //If under ice or at surface
    dataFile = SD.open("sstdat.txt", FILE_WRITE);             //open file with write permissions
  }
  
  if (dataFile) {                                             //if file is available, write to it:
    dataFile.write(0xFF);                                     //Mark the start of a new dataset
    dataFile.write(0xFF);                                     //Mark the start of a new dataset
    if(timestamp>65536){
			dataFile.write(timestamp>>24 & 0xFF);      		          //
			dataFile.write(timestamp>>16 & 0xFF);      		          //
		}
		dataFile.write(timestamp>>8 & 0xFF);      				        //
		dataFile.write(timestamp & 0xFF);      				            //
    
    dataFile.write(pressureVal>>8 & 0xFF);                    //ADC Value from 10 bar pressure sensor (High Byte)
    dataFile.write(pressureVal & 0xFF);                       //ADC Value from 10 bar pressure sensor (Low Byte)
	
    dataFile.write(tempValTop>>8 & 0xFF);                     //ADC Value for Top Temperature Sensor (High Byte) 
    dataFile.write(tempValTop & 0xFF);                      	//ADC Value for Top Temperature Sensor (Low byte)
    
    if(modeSelected!=3){					                            //If not profiling, then check and store the temperature reference value (This takes ~0.5 second for voltage to stabilize on switch. Profile must sample much faster and reference value is not needed at every sample for data quality)
      dataFile.write(tempValSST>>8 & 0xFF);                   //ADC Value SST Temperature Sensor (High Byte) 
      dataFile.write(tempValSST & 0xFF);                      //ADC Value SST Temperature Sensor (Low byte)
      dataFile.write(tempRefVal>>8 & 0xFF);                   //Value from Temperature Reference Resistor (Difference from tempRefOffset) (High Byte)
      dataFile.write(tempRefVal & 0xFF);                      //Value from Temperature Reference Resistor (Difference from tempRefOffset) (Low Byte)
    }
    
    dataFile.write(parVal>>8 & 0xFF);                     	  //ADC Value from PAR sensor (High Byte)
    dataFile.write(parVal & 0xFF);                      	    //ADC Value from PAR sensor (Low Byte)
    
    dataFile.write(fluorVal>>8 & 0xFF);                       //ADC Value from PAR sensor (High Byte)
    dataFile.write(fluorVal & 0xFF);                      	  //ADC Value from PAR sensor (Low Byte)
    
    dataFile.write(tiltAngle & 0xFF);                 	      //Tilt Angle
    dataFile.close();					    		                        //Close data file (Also flushes datastream to make sure all data is written)
  }
}

void waitForProfile(){
  uint16_t pressureAverageLong = averagePressureLong(20);							          //store baseline reading for average pressure (20 readings over 20 seconds)
  uint16_t pressureAverageShort = averagePressureShort(3);						          //store reading for current pressure (3 readings over 300 ms)
  uint16_t pressureChangeVal = 328U*DEPTH_CHANGE_TO_TRIP_PROFILE;				        //calculate ADC reading change needed for desired depth change 
  SPI.setDataMode(SPI_MODE1);                                     				      //Set SPI Data Mode to 1 for reading RTC
  DateTime now = RTC.now();                                                      //Get current time from RTC  
  while(pressureAverageShort>(pressureAverageLong-pressureChangeVal)){          //while short average of last 3 depth readings is less still deeper than cutoff depth to trigger profile
    now = RTC.now();                                     						            //update the RTC time
    if(now.unixtime()>(releaseTime.unixtime()+PRE_RELEASE_WAKEUP_INTERVAL)){  	//if past expected release window, shut the unit down (alarm already set for releaseTime+Wait Period+Sample Interval)
      turnOnFluorometer();
      delay(2000);
      sampleData(2,now.unixtime());                                             //take a data sample
      turnOffFluorometer();
      shutdownUnit();							        						                          //immediately just shut down unit and don't continue with collect profile function
    }
    delay(250);								        							                            //wait 1/4 second between sample bins
    pressureAverageShort=averagePressureShort(3);                  				      //update the short depth average
  }
}

uint16_t averagePressureLong(uint8_t numSamples){		      //function to get baseline depth reading (used to establish depth before while waiting for profile)
  uint32_t pressureSum=0;									                //initialize depth average (long datatype so we can sum 20 samples without overflow)
  for(uint8_t sample=0;sample<numSamples;sample++){				//do this 20 times
    pressureSum=pressureSum+readPressure(); 					    //running sum of depth reading from 10 bar sensor
    delay(1000);										                      //delay 1 second between samples
  }
  uint16_t pressureAverage=pressureSum/numSamples;				//divide sum by 20 to get an average (will truncate any decimals)
  return pressureAverage;									                //return average value, will automatically convert to unsigned int type
}

uint16_t averagePressureShort(uint8_t numSamples){        //function to get baseline depth reading (used to establish depth before while waiting for profile)
  uint32_t pressureSum=0;                                 //initialize depth average (long datatype so we can sum 20 samples without overflow)
  for(uint8_t sample=0;sample<numSamples;sample++){       //do this 20 times
    pressureSum=pressureSum+readPressure();               //running sum of depth reading from 10 bar sensor
    delay(50);                                            //short delay between samples
  }
  uint16_t pressureAverage=pressureSum/numSamples;        //divide sum by 20 to get an average (will truncate any decimals)
  return pressureAverage;                                 //return average value, will automatically convert to unsigned int type
}

void collectProfile(){													                    //function to collect data once profile has been triggered by depth change 
  turnOnFluorometer();
  turnOnTopTemp();
  SPI.setDataMode(SPI_MODE1);                                                   //Set SPI Data Mode to 1 for reading RTC
  DateTime now = RTC.now();                                                      //Get current time from RTC  
  sampleData(3,now.unixtime());                                                 //take a data sample
  uint32_t profileLengthMillis=PROFILE_LENGTH_SECONDS*1000UL;	      //calculate length of profile in milliseconds
  uint32_t startProfileTime=millis();        							          //timestamp for start of profile
  uint32_t endProfileTime=startProfileTime+profileLengthMillis;     //timestamp for end of profile
  uint32_t quarterSecond=startProfileTime;  							          //timestamp to trigger next data sample
  uint32_t profileCentiseconds;											                //timestamp for individual samples (centiseconds for good resolution, but only 2 bytes of data to send for profiles up to 655 seconds)
	while(millis()<endProfileTime){										                //while current timestamp is less than profile cutoff timestamp, keep taking samples
		while(millis()<quarterSecond){ 									                //loop to take sample every 250 ms (4Hz)
			//wait for next sample mark
		}						            
		quarterSecond+=250UL;						        				                //reset timestamp to trigger next data sample
		profileCentiseconds=(millis()-startProfileTime)/10UL;				    //calculate the number of centiseconds since the profile started
		sampleData(3,profileCentiseconds);								              //function call to sample sensors (3=profile mode, don't take reference resistor reading)
  }
  turnOffFluorometer();
}

void turnOnTopTemp(){
  digitalWrite(TEMP_SWITCH_1,HIGH);        //send signal to switch to top thermistor reading
  digitalWrite(TEMP_SWITCH_2,LOW);        //send signal to switch to top thermistor reading
  delay(1000);                            //delay to let voltage stabilize
}

void underIceAndSendDataMode(){					  //mode for when unit has not yet broken free of ice and transmitted any data successfully
  turnOff3v3SensorPower();						    //turn off 3.3V power for sensors (no longer needed)
  turnOff5vSensorPower();						      //turn off 5V power for sensors (no longer needed)
  if(wakeupTime.hour()==IMAGE_HOUR){			//If it is close to noon local time
    takePicture();							          //take a picture
  }  
  if(lookForGPSSatellite()==true){				//if GPS is found successfully n times in a row
    tryToSendIridiumData();							  //try to send data over iridium
  }
}												

void sstDrifterMode(){							      //mode for when unit has not yet broken free of ice and transmitted any data successfully
  turnOff3v3SensorPower();						    //turn off 3.3V power for sensors (no longer needed)
  turnOff5vSensorPower();						      //turn off 5V power for sensors (no longer needed)
  if(lookForGPSSatellite()==true){        //if GPS is found successfully n times in a row
    lookForGPSFix(); 
    tryToSendIridiumData();							    //try to send data over iridium
  }
}												

void takePicture(){                                                                                   //function to check camera functionality by taking a picture
  turnOnCameraPower();                                                                                //turn on power to camera
  turnOnSerialComms();                                                                                //turn on serial comms chip
  delay(800);                                                                                         //wait 800 ms for camera startup (per datasheet)
  digitalWrite(CAMERA_RESET, LOW);
  delay(50);
  digitalWrite(CAMERA_RESET, HIGH);
  delay(800);                                                                                         //wait 800 ms for camera startup (per datasheet)
  CameraSerial.begin(9600);                                                                           //start serial comms with camera at 9600 baud
  if(syncCamera()){                                                                                   //if camera sync is successful
    delay(2000);                                                                                      //wait 2000 ms to let camera circuits stabilize (per datasheet)
    if(setupCamera()){                                                                                //if camera setup is successful
      if(takeCameraSnapshot()){                                                                       //if commands to take camera snapshot is successful
        delay(200);                                                                                   //wait 200 ms for picture to be available
        if(getImageFromCamera()){                                                                     //if all image data can be downloaded successfully 
            //Serial.println(F("Image Capture Successful!"));                                         
            //Serial.println(F("Image Saved on SDcard to camtest.jpg"));
            //Serial.println(F("Transfer File to PC and open to check image"));                         //let user know how to verify image
            }
        else{                                                                                         
          //Serial.println(F("FAILED to Get Image"));
        }
      }
      else{
        //Serial.println(F("Take Snapshot FAILED"));
      }
    }
    else{
      //Serial.println(F("Setup FAILED"));
    }
  }
  else{
    //Serial.println(F("Sync FAILED"));
  }
  turnOffSerialComms();
  turnOffCameraPower();                                                                               //turn off power to the camera
  CameraSerial.end(); 
}

boolean syncCamera(){                                                 //function to sync serial comms with camera
  uint8_t sync_attempts=1;                                            //variable for number of sync commands sent
  boolean syncStatus=false;                                           //variable for camera sync status
  uint8_t sync_delay_ms=5;                                            //variable for milliseconds to delay between sync commands
  uint8_t SYNC_COMMAND[6]={0xAA,0x0D,0x00,0x00,0x00,0x00};            //6 byte command to sync camera
  uint8_t SYNC_RESPONSE[6]={0xAA,0x0E,0x0D,0x00,0x00,0x00};           //6 byte sync response 
  while(sync_attempts<=60&&syncStatus==false){                        //while camera has not synced and we have sent less than 60 sync commands (per datasheet)
    sendCameraCommand(SYNC_COMMAND);                                  //send sync command over serial comms
    delay(sync_delay_ms);                                             //delay defined number of ms (per datasheet)
    syncStatus = readack(SYNC_RESPONSE);                              //listen for a responce, return true if it is the expected sync response
    sync_attempts++;                                                  //increment sync attempt
    sync_delay_ms = 5 + sync_attempts;                                //define number of ms to wait for next attempt (per datasheet)
  }
  if(syncStatus==true){                                               //if camera has synced properly
    syncStatus = readack(SYNC_COMMAND);                               //listen for the next 6 byte response from the camera
    uint8_t SYNC_ACK_COMMAND[6]={0xAA,0x0E,0x0D,0x00,0x00,0x00};      
    sendCameraCommand(SYNC_ACK_COMMAND);                              //send the final sync ack command
  }
  return syncStatus;                                                  //return true if sync was successful 
}

boolean waitForCameraSerialData(uint16_t bytesNeeded){                //function to wait for incoming serial data from camera (at 9600 baud this can take some time)
  uint32_t cameraSerialTimeout = millis() + 3000UL;                //set timeout for serial data to 3 seconds
  uint16_t bytesAvailable = 0;
  while (millis() < cameraSerialTimeout) {                            //wait for serial data until we have the proper number of bytes
    bytesAvailable = CameraSerial.available();
    if (bytesAvailable >= bytesNeeded) {                             //if not enough data is received before the timeout
      return true;                                                   //return false
    }
    //delay(10);
  }
  return false;                                                         //if we have enough bytes and serial comms did not timeout, then return true
}

boolean setupCamera(){                                                      //function to setup camera 
  boolean setupStatus=false;                                                //variable for setup status
  uint8_t INITIAL_COMMAND[6]={0xAA,0x01,0x00,0x07,0x00,0x07};               //6 byte command to configure camera settings (resolution + format)
  sendCameraCommand(INITIAL_COMMAND);                                       //send the 6 byte command
  setupStatus=waitForCameraSerialData(6);                                   //wait for a response of 6 bytes
  uint8_t ACK_RESPONSE_INITIAL[6]={0xAA,0x0E,0x01,0x00,0x00,0x00};          //6 byte response expected for command
  if(setupStatus){                                                          //if we have 6 bytes of data to read
    setupStatus = readack(ACK_RESPONSE_INITIAL);                            //read the response from the camera
  }
  if(setupStatus){                                                          //if no errors yet
    uint8_t PACKAGESIZE_COMMAND[6]={0xAA,0x06,0x08,0x00,0x02,0x00};         //6 byte command to configure package size for image data
    sendCameraCommand(PACKAGESIZE_COMMAND);                                 //send the 6 byte command
    setupStatus=waitForCameraSerialData(6);                                 //wait for a response of 6 bytes
    uint8_t ACK_RESPONSE_PACKAGESIZE[6]={0xAA,0x0E,0x06,0x00,0x00,0x00};    //6 byte response expected for command
    setupStatus = readack(ACK_RESPONSE_PACKAGESIZE);                        //read the response from the camera
  }
  return setupStatus;                                                       //if setup commands were sent and acked successfully, return true
} 

boolean takeCameraSnapshot(){                                               //function to send command to camera to take a snapshot (can be retreived from memory multiple times)
  boolean takeSnapshotStatus=false;                                         //variable for snapshot status
  uint8_t SNAPSHOT_COMMAND[6]={0xAA,0x05,0x00,0x00,0x00,0x00};              //6 byte command to take a snapshot
  sendCameraCommand(SNAPSHOT_COMMAND);                                      //send the 6 byte command
  takeSnapshotStatus=waitForCameraSerialData(6);                            //wait for a response of 6 bytes
  uint8_t ACK_RESPONSE_SNAPSHOT[6]={0xAA,0x0E,0x05,0x00,0x00,0x00};         //6 byte response expected for command
  if(takeSnapshotStatus){                                                   //if we have 6 bytes of data to read
    takeSnapshotStatus = readack(ACK_RESPONSE_SNAPSHOT);                    //read the response from the camera
  }
  return takeSnapshotStatus;                                                //if snapshot command was sent and acked successfully, return true
}

boolean getImageFromCamera(){                                               //function to get image from the camera
  boolean getImageStatus=false;                                             //variable for get Image status
  uint8_t GET_PICTURE_COMMAND[6]={0xAA,0x04,0x01,0x00,0x00,0x00};           //6 byte command to get image
  sendCameraCommand(GET_PICTURE_COMMAND);                                   //send the 6 byte command
  getImageStatus=waitForCameraSerialData(6);                                //wait for a response of 6 bytes
  uint8_t ACK_RESPONSE_GET_PICTURE[6]={0xAA,0x0E,0x04,0x00,0x00,0x00};      //6 byte response expected for command
  if(getImageStatus){                                                       //if we have 6 bytes of data to read
    getImageStatus = readack(ACK_RESPONSE_GET_PICTURE);                     //read the response from the camera
  }                   
  if(getImageStatus){                                                       //if no errors yet
    uint32_t imageSize = readImageSize();                                   //get the size of the image from the next message
    getImageStatus = getImageData(imageSize);                               //get actual image data from the camera
  }
  return getImageStatus;
}

boolean getImageData(uint32_t imageSize){                                   //function to get image data from the camera
  boolean getImageDataStatus=true;                                          //variable for if getting image data if successful
  uint8_t numPackages=imageSize/506;                                        //calculate the number of packages to read from the camera
  SPI.setDataMode(SPI_MODE0);                                               //Set SPI Data Mode to 0 for SD Card
  char filename[] = "jpgxxxxx.txt";
  DateTime underIceModeStart = releaseTime.unixtime() + PRE_RELEASE_WAKEUP_INTERVAL + UNDER_ICE_SAMPLE_INTERVAL;
  uint32_t imageNumber = 1+(wakeupTime.unixtime()- underIceModeStart.unixtime())/86400UL;
  filename[3] = (imageNumber/10000U)%10U + '0';
  filename[4] = (imageNumber/1000U)%10U + '0';
  filename[5] = (imageNumber/100U)%10U + '0';
  filename[6] = (imageNumber/10U)%10U + '0';
  filename[7] = imageNumber%10U + '0';

  if(SD.exists(filename)){
    SD.remove(filename);
  }
 
  for(uint8_t packageNum=0;packageNum<=numPackages;packageNum++){           //for the required number of packages
    if(getImageDataStatus){                                                 //if there are no errors
      getImageDataStatus = readPackage(packageNum, filename);                         //read the next package, store it to the SDCard, and output if it was successful
    }
  }
  
  if(getImageDataStatus){                                                   //if there were no errors
    uint8_t ACK_COMMAND[6]={0xAA,0x0E,0x00,0x00,0xF0,0xF0};                 //6 byte command to ack all data was received
    sendCameraCommand(ACK_COMMAND);                                         //send the 6 byte command
  }
  return getImageDataStatus;                                                //if all data was collected successfully, return true
}

boolean readPackage(uint8_t packageNum, char filename[]){                                    //function to read the next package of data from the camera
  boolean packageStatus = true;                                             //variable to tell if reading package was successful
  uint8_t readbyte;                                                         //variable to read incoming bytes
  uint16_t package_ID;                                                      //variable for incoming packageID
  uint8_t ACK_COMMAND[6] = {0xAA, 0x0E, 0x00, 0x00, packageNum, 0x00};      //6 byte command to get the next package
  sendCameraCommand(ACK_COMMAND);                                           //send the command
                                                      //variable for incoming packageID
  packageStatus = waitForCameraSerialData(1);                               //wait for 1 bytes
  if (packageStatus) {                                                      //if we have 1 byte of data
    readbyte = CameraSerial.read();
    package_ID = readbyte;
  }
  packageStatus = waitForCameraSerialData(1);                               //wait for 1 bytes
  if (packageStatus) {                                                      //if we have 1 bytes of data
    readbyte = CameraSerial.read();
    package_ID = package_ID | readbyte << 8;                                //read the first 2 bytes and store data into Package ID variable
  }
  
  uint16_t datasize;                                                        //variable for size of incoming data
  if (packageStatus) {                                                      //if no errors so far
    packageStatus = waitForCameraSerialData(1);                               //wait for 1 bytes
  }
  if (packageStatus) {                                                      //if no errors so far and we have 2 bytes of data
    readbyte = CameraSerial.read();
    datasize = readbyte;
  }
  if (packageStatus) {                                                      //if no errors so far
    packageStatus = waitForCameraSerialData(1);                               //wait for 1 bytes
  }
  if (packageStatus) {                                                      //if we have 1 bytes of data
    readbyte = CameraSerial.read();
    datasize = datasize | readbyte << 8;                                    //read the next 2 bytes and store data into datasize variable
  }
  
  uint16_t bytesRead=0;
  if(packageStatus){                                                        //if no errors so far
    SPI.setDataMode(SPI_MODE0);		                                          //Set SPI Data Mode to 0 for SD Card  
    File imagefile = SD.open(filename, FILE_WRITE);
    if(imagefile){
      unsigned long cameraSerialTimeout=millis()+5000UL;
      while(bytesRead<datasize){                                            //as long as we are still expecting more bytes to come in
        if(millis()>cameraSerialTimeout){                                   //if timeout has elapsed, return false and exit
          imagefile.close();                                                //close the file
          return false;
        }
        if(CameraSerial.available()){                                       //if any data is available on camera serial buffer
          readbyte = CameraSerial.read();                                   //read the next byte of data
          imagefile.write(readbyte);                                        //store the byte to the SD Card
          bytesRead++;                                                      //increment the number of bytes readfrom camera
        }
      }
      imagefile.close();                                                    //close the file
    }
    else{                                                                   //if file couldn't be opened
      packageStatus=false;                                                  //return false
    }
  }
  
  uint16_t verifycode;                                                      //variable for checksum 
  if(packageStatus){                                                        //if no errors so far
    packageStatus=waitForCameraSerialData(2);                               //wait for 2 last bytes of data
  }
  if(packageStatus){                                                        //if no errors so far and we have 2 more bytes of data
    readbyte = CameraSerial.read();
    verifycode = readbyte;
    readbyte = CameraSerial.read();
    verifycode = verifycode | readbyte<<8;                                  //read the last 2 bytes and put them in the checksum variable
  }
  return packageStatus;                                                     //if there were no errors in getting image data packages, return true
}

boolean readack(uint8_t response[6]){                                       //function to read and verify 6 bytes of data from camera
  uint8_t readbyte;                                                         //variable to read bytes of data
  boolean acked=false;                                                      //variable for if ack has been read successfully
  delay(50);
  for(uint8_t i=0;i<6;i++){                                                 //for each of the 6 bytes of data
    readbyte=CameraSerial.read();                                           //read the next byte coming from the camera
    if(readbyte==response[i] || response[i]==0x00){                         //if the byte from the camera matches the reponse we expected
      acked=true;                                                           //byte has been read successfully
    }
    else{                                                                   //if any byte is incorrect
      acked=false;                                                          //ack response was not read successfully
      return acked;                                                         //return false and exit
    }
  }
  return acked;                                                             //return status of reading ack response
}

uint32_t readImageSize(){                           //function to read the size of the image from camera response
  uint8_t readbuf[6];                               //buffer to read 6 bytes of data
  uint32_t imageSize=0;                             //variable for image size
  uint32_t sizeByte=0;                              //variable for calculating image size with individual bytes
  if(waitForCameraSerialData(6)){                   //if 6 bytes of data are available from the camera
    for(uint8_t k=0;k<6;k++){                       //for each of the 6 bytes
      readbuf[k]=CameraSerial.read();               //read each byte from the camera
    }
    sizeByte=readbuf[5];                            //calculate the image size using last 3 bytes of data
    imageSize=sizeByte<<16;
    sizeByte=readbuf[4];
    imageSize=imageSize+sizeByte<<8;
    sizeByte=readbuf[3];
  } 
  return imageSize;                                 //return the calculated size of the image
}

void sendCameraCommand(uint8_t command[6]){         //generic function to send a 6 byte command to the camers
  while(CameraSerial.available()){                  //if any data is available in the Camera Serial buffer 
    CameraSerial.read();                            //read and flush data in the buffer before sending the command (prevents errors in case all data is not read)
  }
  CameraSerial.write(command,6);                    //send each of the 6 bytes to the camera
}


boolean lookForGPSSatellite(){											                          //function to search for GPS - looks for GPS n times and returns foundGPS=true if found successfully n times
  boolean foundGPSSatellite=false;											                          //start each loop assuming GPS has not been found
  turnOnGPSPower();													                              //turn on power to GPS chip
  turnOnSerialComms();
  for (int i = 0; i < 4; ++i) {
    satNumber[i].begin(gps, "GPGSV", 4 + 4 * i); // offsets 4, 8, 12, 16
    elevation[i].begin(gps, "GPGSV", 5 + 4 * i); // offsets 5, 9, 13, 17
    azimuth[i].begin(  gps, "GPGSV", 6 + 4 * i); // offsets 6, 10, 14, 18
    snr[i].begin(      gps, "GPGSV", 7 + 4 * i); // offsets 7, 11, 15, 19
  }
  delay(100);														                                  //short delay to let GPS chip initialize and begin streaming data
  GPSSerial.begin(9600);                                                  //begin software serial comms at 9600 baud with GPS chip
  uint32_t gpsSearchMillis = 1000UL*GPS_SATELLITE_SEARCH_SECONDS;					//calculate number of milliseconds to search for GPS
  uint32_t startSearchMillis=millis();								                    //timestamp for when GPS search started  (we want to log the time to fix)
  uint32_t endSearchMillis=startSearchMillis+gpsSearchMillis;		          //timestamp for when to end GPS search
  while((millis()<endSearchMillis)&&(foundGPSSatellite==false)){				//while time to search is not over and gps satellite hasn't been found
    while ((GPSSerial.available() > 0)&&(foundGPSSatellite==false)){						//while data stream from GPS chip is incoming and gps hasn't been found	
      gps.encode(GPSSerial.read());
      if (totalGPGSVMessages.isUpdated()){
        for (int i=0; i<4; ++i){
          int no = atoi(satNumber[i].value());
          if (no >= 1 && no <= MAX_SATELLITES){
            sats[no - 1].elevation = atoi(elevation[i].value());
            sats[no - 1].azimuth = atoi(azimuth[i].value());
            sats[no - 1].snr = atoi(snr[i].value());
            sats[no-1].active = true;
          }
        }
        int totalMessages = atoi(totalGPGSVMessages.value());
        int currentMessage = atoi(messageNumber.value());
        uint8_t satsInView =0;
        uint32_t searchMillis=0;
        uint8_t searchSeconds=0;
        if (totalMessages == currentMessage){
          for (int i=0; i<MAX_SATELLITES; ++i){
            if (sats[i].active){
              if(foundGPSSatellite==false){
                foundGPSSatellite=true;
              }
            }
          }
          if(foundGPSSatellite==true){
            GPSSerial.end();                                                //end the serial comms with the GPS 
            turnOffGPSPower();                                                  //turn off GPS power in between searches (last position stored in chip and has coin cell for hot start)
            turnOffSerialComms();
            searchMillis=(millis()-startSearchMillis)/1000UL;
            searchSeconds=searchMillis;
            storeTimeToFindSat(searchSeconds);
            return foundGPSSatellite;              
          }
        }
      }																		                                  
    }
    if((foundGPSSatellite==true)||(millis()>endSearchMillis)){        //if GPS position has been found or loop has timed out
      GPSSerial.end();                                                //end the serial comms with the GPS 
    }                                                                 //keep searching and parsing data until GPS has been found or search timeout is exceeded
  }
  turnOffGPSPower();				                                          //turn off GPS power in between searches (last position stored in chip and has coin cell for hot start)
	turnOffSerialComms();
	return	foundGPSSatellite;
}

boolean lookForGPSFix(){										                    //function to search for GPS - looks for GPS n times and returns foundGPS=true if found successfully n times
  uint8_t tiltAngle= readAccel();									                        //initialize max tilt angle variable
  uint8_t maxTiltAngle = tiltAngle;									                      //initialize tilt angle reading, set to same as max reading
	boolean foundGPSFix=false;										                                            //start each loop assuming GPS has not been found
	turnOnGPSPower();										                                          //turn on power to GPS chip
	turnOnSerialComms();
	delay(100);											                                              //short delay to let GPS chip initialize and begin streaming data
	GPSSerial.begin(9600);										                                            //begin software serial comms at 9600 baud with GPS chip
	unsigned long gpsSearchMillis = 1000UL*GPS_FIX_SEARCH_SECONDS;					          //calculate number of milliseconds to search for GPS
	unsigned long startSearchMillis=millis();							                        //timestamp for when GPS search started  (we want to log the time to fix)
	unsigned long endSearchMillis=(long)startSearchMillis+(long)gpsSearchMillis;	        //timestamp for when to end GPS search
	while((millis()<endSearchMillis)&&(foundGPSFix==false)){					                //while time to search is not over and gps hasn't been foundGPS
	  while ((GPSSerial.available() > 0)&&(foundGPSFix==false)){						                //while data stream from GPS chip is incoming and gps hasn't been found	
  		if (gps.encode(GPSSerial.read())){								                                //parse incoming data and everytime a full message is encoded, execute the following
  	    //displayGPSInfo();                                               //display any GPS data which has been translated successfully on the serial monitor
  			tiltAngle = readAccel();							                                //read instantaneous tilt angle
  			if(tiltAngle>maxTiltAngle){							                            //if new tilt angle is more than old tilt angle (current cosine value is less than old cosine value)
  			  maxTiltAngle=tiltAngle;						                                //store the current cosine value into the max angle
  			}				
  		  if (gps.location.isValid()&&gps.date.isValid()&&gps.time.isValid()&&gps.date.year()<2050&&gps.satellites.value()>=3){		//if a valid fix is found (location, time, and date all valid)
          foundGPSFix=true;								                                        //flag GPS fix has been found
       	  uint32_t timetofix=millis()-startSearchMillis;
          storeGPSFix(timetofix,maxTiltAngle);
          GPSSerial.end();                                                           //end the serial comms with the GPS 
          turnOffGPSPower();                                                            //turn off GPS power in between searches (last position stored in chip and has coin cell for hot start)
          turnOffSerialComms();
          return  foundGPSFix;                        //store data from GPS position to SD File
  		  }
  		}
	  }
    if((foundGPSFix==true)||(millis()>endSearchMillis)){                         //if GPS position has been found or loop has timed out
      GPSSerial.end();                                                           //end the serial comms with the GPS 
    }
	}												                                                      //keep searching and parsing data until GPS has been found or search timeout is exceeded
	turnOffGPSPower();										                                        //turn off GPS power in between searches (last position stored in chip and has coin cell for hot start)
  turnOffSerialComms();
	return	foundGPSFix;
}


void displayGPSInfo()                                             //function to display incoming GPS data to serial monitor
{
  Serial.print(F("Location: "));
  if (gps.location.isValid())  {                                  //If location data from GPS is valid, display latitude and longitude to 6 decimal places on serial monitor
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.print(gps.location.lng(), 6);
  }
  else  {
    Serial.print(F("INVALID"));                                   //If location data is not valid, display INVALID
  }

  Serial.print(F("  Date/Time: "));                               //If date from GPS is valid, display  it on serial monitor
  if (gps.date.isValid())  {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
  }
  else  {
    Serial.print(F("INVALID"));                                   //If date is not valid, display INVALID
  }

  Serial.print(F(" "));
  if (gps.time.isValid())  {                                      //If Time from GPS is valid, display it on serial monitor
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(F("."));
    if (gps.time.centisecond() < 10) Serial.print(F("0"));
    Serial.print(gps.time.centisecond());
  }
  else  {
    Serial.print(F("INVALID"));                                   //If time is not valid, display INVALID
  }
  Serial.print(F("  Satellites in Use:"));                               //display the number of visible satellites, regardless of validity of other data
  Serial.print(gps.satellites.value());
  Serial.println();
}

void storeGPSFix(uint32_t timeToFix, uint8_t maxTiltAngle){
  SPI.setDataMode(SPI_MODE1);                               //Set SPI Data Mode to 1 for reading RTC
  DateTime now = RTC.now();				                          //Get current time to compare to GPS time for clock drift
  SPI.setDataMode(SPI_MODE0);                        	      //Set SPI Data Mode to 0 for SD Card Use
  File dataFile = SD.open("sstdat.txt", FILE_WRITE); 	      //open the file with write permissions
  
  if (dataFile) {                                    	      //if the file is available, write to it:
	int32_t gpsLat = gps.location.lat()*1000000;
	int32_t gpsLon = gps.location.lng()*1000000;
	uint32_t gpsDateVal = gps.date.value();
	uint32_t gpsTimeVal = gps.time.value();
  uint8_t timeToFixSeconds=timeToFix/1000UL;

	  dataFile.write(gpsLat>>24 & 0xFF);                         //
    dataFile.write(gpsLat>>16 & 0xFF);                         //
    dataFile.write(gpsLat>>8 & 0xFF);                          //
    dataFile.write(gpsLat & 0xFF);                             //
    
    dataFile.write(gpsLon>>24 & 0xFF);                         //
    dataFile.write(gpsLon>>16 & 0xFF);                         //
    dataFile.write(gpsLon>>8 & 0xFF);                          //
    dataFile.write(gpsLon & 0xFF);                             //
    
    dataFile.write(gpsDateVal>>24 & 0xFF);                     //
    dataFile.write(gpsDateVal>>16 & 0xFF);                     //
    dataFile.write(gpsDateVal>>8 & 0xFF);                      //
    dataFile.write(gpsDateVal & 0xFF);                         //
    
    dataFile.write(gpsTimeVal>>24 & 0xFF);                     //
    dataFile.write(gpsTimeVal>>16 & 0xFF);                     //
    dataFile.write(gpsTimeVal>>8 & 0xFF);                      //
    dataFile.write(gpsTimeVal & 0xFF);                         //

    dataFile.write(timeToFixSeconds & 0xFF);                   //
    
    dataFile.write(maxTiltAngle);			                             
    
    dataFile.close();				                            //Close data file (Also flushes datastream to make sure all data is written)
  }
}

void tryToSendIridiumData(){
  
  if(fileSelection!=0){                                                     //if fileSelection still=0, then this is the very first message (or we are re-sending it) so we want to get a summary of the data files
    setAlarmNextSendAttempt(SEND_DATA_INTERVAL);                            //reset alarm for next send attempt in 20 minutes
  }
  else{
    uint16_t numImages = determineImageOrder();
	  writeFileSummary(numImages);                                             //function to collect file size info 
	  goToNextFile();                                                         //increment fileSelection from 0 to 1
    writeFilePosition();                                                    //record current file position and file selection
  }

  setupRockBlock();  
  
  const uint16_t sendbuffer_size = BYTES_PER_SBDMESSAGE+1;	                //calculate size of array for send message buffer
  uint8_t iridiumSendBuffer[sendbuffer_size];		    					              //initialize array for send message buffer (reserve adequate space in memory)
  
  size_t sendMessageSize = fillSendBufferFirstMessage(iridiumSendBuffer);
  uint8_t iridiumReceiveBuffer[RECEIVE_BUFFER_SIZE];			    		          //Initialize array for received message (will only send short messages of 12 chars to change filename/position)
  emptyReceiveBuffer(iridiumReceiveBuffer);
  size_t receiveBufferSize = RECEIVE_BUFFER_SIZE;  							            //Define size of array for received message 
  
  int16_t err = isbd.sendReceiveSBDBinary(iridiumSendBuffer, sendMessageSize, iridiumReceiveBuffer, receiveBufferSize);	//Attempt to send first message ("I am alive!")
  
  if (err != 0){
      return;
  }										                                                      //If sending message fails, then exit routine and shutdown unit  
  readReceivedMessage(iridiumReceiveBuffer);                                //read the received message and if it contains info on where to start parsing data, then override the info from the SDCard
  
  while(fileSelection!=0xFFFF){									                            //0xFFFF is marker for all files being completely sent; as long as this is not true, keep parsing and sending data
    writeFilePosition();                                                    //record current file position
    setAlarmNextSendAttempt(SEND_DATA_INTERVAL);                            //reset alarm for 20 minutes from current time 
    emptyReceiveBuffer(iridiumReceiveBuffer);
    sendMessageSize = fillSendBuffer(iridiumSendBuffer);					          //Fill the send message buffer based on file and file position 
    err = isbd.sendReceiveSBDBinary(iridiumSendBuffer, sendMessageSize, iridiumReceiveBuffer, receiveBufferSize);	    //attempt to send message
    if (err != 0){                                                                                                    //If sending message fails
      return;                                                                                         //then exit routine and shutdown unit
    }
    else{                                                                                             //if message is successful
      if(newFile==false){
        if(fileSelection>=5&&fileSelection<0xFFFE){
          filePosition=filePosition+sendMessageSize-8;                         //increment file position accordingly
        }
        else{
          filePosition=filePosition+sendMessageSize-6;                         //increment file position accordingly
        }
      }
    	else{
	      goToNextFile();
	    }
    }									                          
  }
  readReceivedMessage(iridiumReceiveBuffer);                                        //check if received message contains any info on where to go in file structure next
  turnOffIridiumPower();										                                        //Once unit is done sending messages, turn off power to the iridium module 
  setAlarmNextSynopticHour();                                                       //reset alarm for 1 day from current time
}

void setupRockBlock(){
  turnOnIridiumPower();					    						//switch on voltage to Iridium module 
  delay(50);						    						//short delay to let voltage stabilize 
  IridiumSerial.begin(19200);  					    					//start serial comms on nss pins defined above
  isbd.attachConsole(Serial);
  //isbd.attachDiags(Serial);
  isbd.setPowerProfile(0); 				    						//1 for low power application (USB, limited to ~90mA, interval between transmits = 60s)
  isbd.setMinimumSignalQuality(2);			    						//minimum signal quality (0-5) needed before sending message.  normally set to 2 (by isbd recommendations)
  isbd.adjustSendReceiveTimeout(300);
  isbd.begin();  					    						//wake up the Iridium module and prepare it to communicate
}

uint16_t fillSendBufferFirstMessage(uint8_t sendBuffer[]){	  
  for(uint8_t k =0;k<10;k++){
	  sendBuffer[k] = 0xFF;			            						//First message to send if unit wake up is "0xFF(1)0xFF(2)...0xFF(10)"  (preferred to not send data in case of file corruption - unit will still respond if data won't send)
  }
  return 10;
}

void storeTimeToFindSat(uint32_t timeToFindSat){
  SPI.setDataMode(SPI_MODE0);             //Set SPI Data Mode to 0 for SD Card Use
  File dataFile = SD.open("sstdat.txt", FILE_WRITE);                                  //open the file with read permissions
  if(dataFile){
    dataFile.write(timeToFindSat & 0xFF);                                                         
  }
  dataFile.close();
}

void readFilePositionSD(){
  uint32_t positionByte=0;
  SPI.setDataMode(SPI_MODE0);                        	                                //Set SPI Data Mode to 0 for SD Card Use
  File dataFile = SD.open("filepos.txt", FILE_READ);//open the file with read permissions
  if(dataFile.available()>=6){                                                            
    uint8_t readFilePositionBuffer[6];
    for(uint8_t n=0;n<6;n++){
      readFilePositionBuffer[n]=byte(dataFile.read());
    }
    dataFile.close();
    fileSelection = readFilePositionBuffer[0]<<8;
    positionByte = readFilePositionBuffer[1];
    fileSelection = fileSelection + positionByte;
    positionByte = readFilePositionBuffer[2];
    filePosition = filePosition << 24;
    positionByte = readFilePositionBuffer[3];
    filePosition = filePosition + positionByte<<16;
    positionByte = readFilePositionBuffer[4];
    filePosition = filePosition + positionByte<<8;
    positionByte = readFilePositionBuffer[5];
    filePosition = filePosition + positionByte;
	}
  else{
    filePosition = 0;
    fileSelection = 0;
  }
}

void emptyReceiveBuffer(uint8_t receiveBuffer[]){
  for(uint16_t y=0;y<RECEIVE_BUFFER_SIZE;y++){
    receiveBuffer[y] = 0;			            					
  }
}

void readReceivedMessage(uint8_t receiveBuffer[]){                                                                              //Function to parse received message and get file position (example message = 'p,1234567890' or 'p,95')
	uint16_t newFileSelection=0;
	newFileSelection=(receiveBuffer[0]<<8+receiveBuffer[1]);
	uint32_t newFilePosition=0;
	newFilePosition=(receiveBuffer[2]<<24+receiveBuffer[3]<<16+receiveBuffer[4]<<8+receiveBuffer[5]<<0);
	
  if(newFileSelection){	
	  fileSelection=newFileSelection;                                                                                                 //set file designator appropriately
    filePosition=newFilePosition;					                                                                        //reset value for file position unsigned long is a maximum of 10 digits (4,294,967,295)
	}
}

uint16_t determineImageOrder(){
	SPI.setDataMode(SPI_MODE0);                        	                                //Set SPI Data Mode to 0 for SD Card Use
	uint16_t numImages = 0;
	if(SD.exists("jpgorder.txt")){
		SD.remove("jpgorder.txt");
	}
	File dataFile = SD.open("jpgorder.txt", FILE_WRITE); 	                                //open the file with read permissions
	if(dataFile){
		DateTime underIceModeStart = releaseTime.unixtime() + PRE_RELEASE_WAKEUP_INTERVAL + UNDER_ICE_SAMPLE_INTERVAL;
		numImages = floor((wakeupTime.unixtime() - releaseTime.unixtime())/86400UL);
    uint8_t orderUsed[numImages];
    for(uint16_t i = 0;i<numImages;i++){
      orderUsed[i]=0;
    }
		uint16_t nextImage = 1;
		dataFile.write(nextImage>>8 & 0xFF);
		dataFile.write(nextImage & 0xFF);
		orderUsed[0]=1;
		nextImage = numImages;
		if(numImages>1){
		  dataFile.write(nextImage>>8 & 0xFF);
		  dataFile.write(nextImage & 0xFF);
		}
		orderUsed[numImages-1]=true;
		
		uint8_t divisor = 2;
		uint16_t OrderMark = 3;
		while(numImages/divisor>1){
      for(uint8_t increment=2;increment<=divisor;increment=increment+2){
        nextImage=floor(numImages/divisor)+ceil((increment-2)*(numImages/divisor));
				if(orderUsed[nextImage-1]==0){
					dataFile.write(nextImage>>8 & 0xFF);
					dataFile.write(nextImage & 0xFF);
					OrderMark++; 
					orderUsed[nextImage-1]=1;
				}
			}
			divisor=divisor*2;
		}
		for(uint16_t fillHoles=1;fillHoles<numImages;fillHoles++){
			if(orderUsed[fillHoles-1]==0){
				dataFile.write(fillHoles>>8 & 0xFF);
				dataFile.write(fillHoles & 0xFF);
				OrderMark++; 
				orderUsed[fillHoles-1]=1;
			}
		}
	dataFile.close();
	}
  return numImages;
}

uint16_t fillSendBuffer(uint8_t sendBuffer[]){	          //function to fill message based on file and file position
  uint16_t messageLength=6;
  sendBuffer[0]=(fileSelection>>8)&0xFF;
  sendBuffer[1]=(fileSelection&0xFF);
  SPI.setDataMode(SPI_MODE0);                             //Set SPI Data Mode to 0 for sd card
  File currentFile;									                      //Initialize file variable
  if(fileSelection==1){									                  //'1' designates summary data (summary.txt)
    currentFile=SD.open("summary.txt", FILE_READ);		    //open file with read access
  }
  else if(fileSelection==2){								              //'2' designates profile data (prodat.txt)
    currentFile=SD.open("prodat.txt", FILE_READ);		      //open file with read access
  }
  else if(fileSelection==3){								              //'3' designates under ice data (icedat.txt)
    currentFile=SD.open("icedat.txt", FILE_READ);  		    //open file with read access
  }
  else if(fileSelection==4){								              //'4' designates bottom data (botdat.txt)
    currentFile=SD.open("botdat.txt", FILE_READ);  		    //open file with read access
  }	
  else if(fileSelection==0xFFFE){								          //FFFF designates SST Drifter mode
    currentFile=SD.open("sstdat.txt", FILE_READ);					//Initialize file variable
  }
  else{
	  uint16_t imageNumber = getImageNumber();
  	char filename[] = "jpgxxxxx.txt";
	  filename[3] = (imageNumber/10000U)%10U + '0';
	  filename[4] = (imageNumber/1000U)%10U + '0';
	  filename[5] = (imageNumber/100U)%10U + '0';
	  filename[6] = (imageNumber/10U)%10U + '0';
	  filename[7] = imageNumber%10U + '0';
    sendBuffer[6]=(imageNumber>>8)&0xFF;
    sendBuffer[7]=imageNumber&0xFF;
    messageLength=8;
	  currentFile=SD.open(filename, FILE_READ);					          //Initialize file variable
  } 
  sendBuffer[2]=(filePosition>>24)&0xFF;
  sendBuffer[3]=(filePosition>>16)&0xFF;
  sendBuffer[4]=(filePosition>>8)&0xFF;
  sendBuffer[5]=filePosition&0xFF;
  newFile=false;                                                //initialize the newfile variable
  if(currentFile.size()>filePosition){									        //If file is open for reading
    currentFile.seek(filePosition);							                //move forward to proper file position
    for(uint16_t i=messageLength;i<BYTES_PER_SBDMESSAGE;i++){	  //parse one char at a time for length of message
      if(currentFile.available()){						                  //If not yet at end of the file
        sendBuffer[i]=byte(currentFile.read());  			          //put the next char into the send message buffer
		    messageLength=i+1;
	    }      
      else{										                                  //If end of file has been reached
		    currentFile.close();							                      //close the file
        newFile=true;
        return messageLength;
      }
    }
    currentFile.close();                                        //close the file
  }  
  else{
    for(uint8_t k = messageLength;k<20;k++){
		  sendBuffer[k] = 0xFF;			            						        //Send Message (0xFF(1)0xFF(2)...0xFF(20)) to indicate that datafile did not open/was empty
	    messageLength=k+1;  
	  }
	  goToNextFile();					                                    //go to the next file
  }
  return messageLength;
}

uint16_t getImageNumber(){
	SPI.setDataMode(SPI_MODE0);                        	                                //Set SPI Data Mode to 0 for SD Card Use
	File dataFile = SD.open("jpgorder.txt", FILE_READ); 	                                //open the file with read permissions
	uint16_t imageNumber=0;
	uint32_t positionInFile = (fileSelection-5)*2UL;
  uint8_t readByte = 0;
	if(dataFile){
		dataFile.seek(positionInFile);
		if(dataFile.available()>=2){
      readByte = byte(dataFile.read());
			imageNumber = readByte << 8;
      readByte = byte(dataFile.read());
			imageNumber = imageNumber + readByte;
		}
		else{
			fileSelection=0xFFFE;
			return fileSelection;
		}
		dataFile.close();
	}
	return imageNumber;
}

void writeFileSummary(uint16_t numImages){
  SPI.setDataMode(SPI_MODE0);                                                           //Set SPI Data Mode to 0 for sd card
  unsigned long prodatSize=0;                                                           //variable for prodat.txt file size
  unsigned long icedatSize=0;                                                           //variable for icedat.txt file size
  unsigned long botdatSize=0;                                                           //variable for botdat.txt file size
  File dataFile=SD.open("prodat.txt", FILE_READ);			                //Initialize File variable and Open Prodat.txt to get file size
  if(dataFile){                                                                         //if file exists
    prodatSize=dataFile.size();                                                         //get file size (length in bytes)
    dataFile.close();                                                                   //Close the File
  }
  dataFile=SD.open("icedat.txt", FILE_READ);					        //Open Icedat.txt to get file size
  if(dataFile){                                                                         //if file exists
    icedatSize=dataFile.size();                                                         //get file size (length in bytes)
    dataFile.close();                                                                   //Close the File
  }
  dataFile=SD.open("botdat.txt", FILE_READ);					        //Open Botdat.txt to get file size
  if(dataFile){                                                                         //if file exists
    botdatSize=dataFile.size();                                                         //get file size (length in bytes)
    dataFile.close();                                                                   //Close the File
  }
  if(SD.exists("summary.txt")){
    SD.remove("summary.txt");
  }  
  dataFile=SD.open("summary.txt", FILE_WRITE);					        //Create and Open summary.txt 
  if(dataFile){
    dataFile.write(prodatSize>>24 & 0xFF);                                       
    dataFile.write(prodatSize>>16 & 0xFF);                                       
    dataFile.write(prodatSize>>8 & 0xFF);                                        
    dataFile.write(prodatSize & 0xFF);                                         
    dataFile.write(icedatSize>>24 & 0xFF);                                  
    dataFile.write(icedatSize>>16 & 0xFF);                                  
    dataFile.write(icedatSize>>8 & 0xFF);                                        
    dataFile.write(icedatSize & 0xFF);                                           
    dataFile.write(botdatSize>>24 & 0xFF);                                      
    dataFile.write(botdatSize>>16 & 0xFF);                                       
    dataFile.write(botdatSize>>8 & 0xFF);                                        
    dataFile.write(botdatSize);
    dataFile.write(numImages>>8 & 0xFF);                                                
    dataFile.write(numImages & 0xFF);                                                                                        
    dataFile.close();                                                     //close the file
  }
}

void writeFilePosition(){
  SPI.setDataMode(SPI_MODE0);                                                           //Set SPI Data Mode to 0 for sd card
  if(SD.exists("filepos.txt")){
    SD.remove("filepos.txt");
  }  
  File dataFile=SD.open("filepos.txt", FILE_WRITE);					//Initialize file variable
  if(dataFile){
	  dataFile.write(fileSelection>>8 & 0xFF);                                                            //File Designator
	  dataFile.write(fileSelection & 0xFF);                                                            //File Designator
	  dataFile.write(filePosition>>24 & 0xFF);                                                            //File Designator
	  dataFile.write(filePosition>>16 & 0xFF);                                                            //File Designator
	  dataFile.write(filePosition>>8 & 0xFF);                                                            //File Designator
	  dataFile.write(filePosition & 0xFF);                                                            //File Designator
	  dataFile.close();
  }
}

void goToNextFile(){			                                              //function for selecting the next file based on the current file (when file does not open or end of file has been reached)
  newFile=false;
  if(fileSelection==0xFFFE){
    fileSelection++;
    return;
  }
  filePosition=0;
  if(fileSelection<5){
	  fileSelection++;
  }
  else{
	  goToNextImageFile();	  
  }
}

void goToNextImageFile(){
	SPI.setDataMode(SPI_MODE0);                                           //Set SPI Data Mode to 0 for sd card
	File dataFile=SD.open("jpgorder.txt", FILE_READ);											//Initialize file variable
	uint16_t positionInFile = (fileSelection-4)*2;
	if(dataFile){
		dataFile.seek(positionInFile);
		if(dataFile.available()>=2){
  		fileSelection++;			
		}
		else{
			fileSelection=0xFFFE;
		}
		dataFile.close();
	}
}

void setAlarmNextSendAttempt(uint32_t secondsFromNow){
  SPI.setDataMode(SPI_MODE1);                                           //Set SPI Data Mode to 1 for RTC
  DateTime now = RTC.now();                                             //Get time from RTC and store into global variable for when unit awoke
  alarmtime=now.unixtime()+secondsFromNow;         
  setAlarmTime();                                                       //Set The Alarm 
}

void setAlarmNextSynopticHour(){
  SPI.setDataMode(SPI_MODE1);                                           //Set SPI Data Mode to 1 for RTC
  DateTime now = RTC.now();                                             //Get time from RTC and store into global variable for when unit awoke

  alarmtime=now.unixtime()+60UL;
  alarmtime=alarmtime.unixtime()+(3600-alarmtime.unixtime()%3600);
  
  int8_t timeZoneShift=-1*TIMEZONE;
  alarmtime=alarmtime.unixtime()+(3600)*timeZoneShift;
  
  if(alarmtime.unixtime()%10800!=0){
    alarmtime=alarmtime.unixtime()+3600;
  }
  if(alarmtime.unixtime()%10800!=0){
    alarmtime=alarmtime.unixtime()+3600;
  }

  alarmtime=alarmtime.unixtime()-(3600)*timeZoneShift;  
  setAlarmTime(); //Set The Alarm   
}

uint16_t readPressure(){                          //function to read pressure measurement from sensor: depth in m = [(reading-16384)*maxbar/32768]*10
  uint8_t pressureStatus;                         //initialize variable for sensor status
  uint16_t pressureReading;                       //initialize variable for pressure reading 
  uint8_t eocDelay=8;                             //variable for conversion delay (8 ms defined in Keller communication protocol)
  Wire.beginTransmission(kellerADDR);             //Begin I2C comms with pressure sensor
  Wire.write(kellerCheckPressure);                //Send write command to start pressure conversion 
  Wire.endTransmission();                         //End command for pressure conversion 
  delay(eocDelay);                                //Delay for conversion (8 ms defined in Keller communication protocol)
  Wire.requestFrom(kellerADDR,3);                 //Read data from pressure sensor
  while(Wire.available()){                        //Ensure all the data comes in
    pressureStatus = Wire.read();                 //First byte is Sensor Status  (Can possibly be used for error checking)
    pressureReading = Wire.read();                //Second byte is Pressure reading MSB
    pressureReading = (pressureReading << 8);     //Shift second byte to MSB 
    pressureReading += Wire.read();               //Third byte is LSB, add to pressure reading
  }
  Wire.endTransmission();                         //End I2C communication for reading pressure
  return pressureReading;                         //Return value for pressure only
}

void setupTempADC(){
  Wire.beginTransmission(ads1100A0);	    //Begin I2C comms with Temp ADC 
  Wire.write(B10001110); 				          //set ADC gain to 4, 8 Samples per Second (16-bit Resolution)
  Wire.endTransmission();				          //End I2C Comms
  delay(50);							                //Short delay to let ADC initialize
}

int16_t readTopTempProfile(){			        //function to read temperature ADC for top thermistor when profiling (no delay and always sending current through top thermistor)
  int16_t measurement = readTempADC();	  //read the ADC (Same for thermistor or reference)
  return measurement;					            //return the measured value from the ADC
}

int16_t readTopTemp(){                    //function to read temperature ADC for top thermistor 
  digitalWrite(TEMP_SWITCH_1,HIGH);        //send signal to switch to top thermistor reading
  digitalWrite(TEMP_SWITCH_2,LOW);        //send signal to switch to top thermistor reading
  delay(1000);                            //delay to let voltage stabilize
  int16_t measurement = readTempADC();    //read the ADC (Same for thermistor or reference)
  digitalWrite(TEMP_SWITCH_1,LOW);       //send signal to switch back to reference resistor 1
  digitalWrite(TEMP_SWITCH_2,LOW);       //send signal to switch back to reference resistor 2
  return measurement;                     //return the measured value from the ADC
}

int16_t readSST(){                        //function to read temperature ADC for SST thermistor 
  digitalWrite(TEMP_SWITCH_1,HIGH);        //send signal to switch to top thermistor reading
  digitalWrite(TEMP_SWITCH_2,HIGH);       //send signal to switch to top thermistor reading
  delay(1000);                            //delay to let voltage stabilize
  int16_t measurement = readTempADC();    //read the ADC (Same for thermistor or reference)
  digitalWrite(TEMP_SWITCH_1,LOW);       //send signal to switch back to reference resistor 1
  digitalWrite(TEMP_SWITCH_2,LOW);       //send signal to switch back to reference resistor 2
  return measurement;                     //return the measured value from the ADC
}

int16_t readTempRef(){                    //function to read temperature ADC for top thermistor 
  delay(1000);                            //delay to let voltage stabilize
  int16_t measurement = readTempADC();    //read the ADC (Same for thermistor or reference)
  return measurement;                     //return the measured value from the ADC
}

int16_t readTempADC(){							
  uint8_t controlRegister = 0;				    //initialize control registoer variable 
  int16_t adcVal = 0;						          //initialize ADC Value variable
  Wire.requestFrom(ads1100A0, 3);  			  //request 3 bytes from appropriate ADC using I2C
  while(Wire.available()){ 					      //ensure all the data comes in
    adcVal = Wire.read(); 		        	  //first byte is MSB 
    adcVal = ((uint16_t)adcVal << 8);			//shift first byte 8 bits to the left to move it into MSB
    adcVal += Wire.read(); 					      //second byte is LSB.  add this to the MSB
    controlRegister = Wire.read();			  //third byte is control register
  }
  return adcVal;							            //return single value for ADC reading
}

void setupPARADC(){
  Wire.beginTransmission(ads1100A1);		  //Begin I2C comms with Temp ADC 
  Wire.write(B10001100); 					        //set ADC gain to 1, 8 Samples per Second (16-bit Resolution)
  Wire.endTransmission();					        //End I2C Comms
  delay(50);								              //Short delay to let ADC initialize
}

int16_t readPAR(){
  byte controlRegister = 0;					      //initialize control register variable 
  int16_t adcVal = 0;						          //initialize ADC Value variable
  Wire.requestFrom(ads1100A1, 3);  	      //request 3 bytes from appropriate ADC using I2C
  while(Wire.available()){ 					      //ensure all the data comes in
    adcVal = Wire.read(); 					      //first byte is MSB 
    adcVal = ((uint16_t)adcVal << 8);		  //shift first byte 8 bits to the left to move it into MSB
    adcVal += Wire.read(); 					      //second byte is LSB.  add this to the MSB
    controlRegister = Wire.read();	      //third byte is control register
  }
  return adcVal;							            //return single value for ADC reading
}

void setupFluorADC(){
  Wire.beginTransmission(ads1100A2);      //Begin I2C comms with Fluor ADC 
  Wire.write(B10001100);                  //set ADC gain to 1, 8 Samples per Second (16-bit Resolution)
  Wire.endTransmission();                 //End I2C Comms
  delay(50);                              //Short delay to let ADC initialize
}

int16_t readFluor(){
  byte controlRegister = 0;               //initialize control register variable 
  int16_t adcVal = 0;                     //initialize ADC Value variable
  Wire.requestFrom(ads1100A2, 3);         //request 3 bytes from appropriate ADC using I2C
  while(Wire.available()){                //ensure all the data comes in
    adcVal = Wire.read();                 //first byte is MSB 
    adcVal = ((uint16_t)adcVal << 8);     //shift first byte 8 bits to the left to move it into MSB
    adcVal += Wire.read();                //second byte is LSB.  add this to the MSB
    controlRegister = Wire.read();        //third byte is control register
  }
  return adcVal;                          //return single value for ADC reading
}

void setupAccel(){                              //Function to Setup ADXL345 Chip (accelerometer)
  SPI.setDataMode(SPI_MODE3);                   //Set SPI Data Mode to 3 for Accelerometer
  sendSPIdata(ACCEL_CS,0x2C,B00001010);         //0x2C=data rate and power mode control, B00001010=normal power mode, SPI data rate =100HZ
  sendSPIdata(ACCEL_CS,0x31,B00001011);         //0x31=data format control, B00001011=no self test, SPI mode, Active LOW, full resolution mode, left justified (MSB), +/-16g range 
  sendSPIdata(ACCEL_CS,0x2D,B00001000);         //0x2D=power control, B00001000=measurement mode
  SPI.setDataMode(SPI_MODE2);                   //Set SPI Data Mode to default
}

uint8_t readAccel(){						          //Read tilt angle from ADXL345 Chip
  SPI.setDataMode(SPI_MODE3);		          //Set SPI Data Mode to 3 for Accelerometer
  digitalWrite(ACCEL_CS, LOW);				    //Select Accelerometer for SPI Communication	
  SPI.transfer(0x32 | 0xC0);				      //Request x,y,and z Acceleration Values from Accelerometer 
  byte x0 = SPI.transfer(-1);				      //x0=Accel LSB
  byte x1 = SPI.transfer(-1);				      //x1=Accel MSB
  byte y0 = SPI.transfer(-1);				      //y0=Accel LSB
  byte y1 = SPI.transfer(-1);				      //y1=Accel MSB
  byte z0 = SPI.transfer(-1);				      //z0=Accel LSB
  byte z1 = SPI.transfer(-1);				      //z1=Accel MSB
  digitalWrite(ACCEL_CS, HIGH);				    //De-Select Accelerometer for SPI Communication

  float x = x1<<8 | x0;						                  //Combine x1(MSB) and x0(LSB) into x
  float y = y1<<8 | y0;						                  //Combine y1(MSB) and y0(LSB) into y
  float z = z1<<8 | z0;						                  //Combine z1(MSB) and z0(LSB) into z
  float xg = x*3.9/1000.0;					                //Convert x accel value to g force units
  float yg = y*3.9/1000.0;					                //Convert x accel value to g force units
  float zg = z*3.9/1000.0;					                //Convert x accel value to g force units
  float gForce=sqrt(sq(xg)+sq(yg)+sq(zg));	        //Find Total G force (3 dim Pythagorean)
  float tiltAngle = acos(-zg/gForce)/3.14159*180.0;  //calculate the angle using horizontal component of g-force
  uint8_t truncatedAngle = tiltAngle;               //convert angle to a single byte integer value
  return truncatedAngle;				                    //Return the angle from horizontal 
}

void sendSPIdata(char pin, byte b1, byte b2){   //generic function to send SPI data
  digitalWrite(pin, LOW);                       //pull chip select pin low to select
  SPI.transfer(b1);                             //transfer first byte
  SPI.transfer(b2);                             //transfer secons byte
  digitalWrite(pin, HIGH);                      //pull chip select pin high to deselect
}

void setAlarmTime(){                            //function to set the alarmtime on the DS3234 RTC
  SPI.setDataMode(SPI_MODE1);                   //Set SPI Data Mode to 1 for RTC
  RTCWrite(0x87,alarmtime.second() & 0x7F);     //set alarm time: seconds     //87=write to location for alarm seconds    //binary & second with 0x7F required to turn alarm second "on"
  RTCWrite(0x88,alarmtime.minute() & 0x7F);     //set alarm time: minutes     //88=write to location for alarm minutes    //binary & minute with 0x7F required to turn alarm minute "on"
  RTCWrite(0x89,alarmtime.hour() & 0x7F);       //set alarm time: hour        //89=write to location for alarm hour       //binary & hour with 0x7F required to turn alarm hour "on"
  RTCWrite(0x8A,alarmtime.day() & 0x3F);        //set alarm time: day         //8A=write to location for alarm day        //binary & day with 0x3F required to turn alarm day "on" (not dayofWeek) 
  RTCWrite(0x8B,0);                             //Set Alarm #2 to zll zeroes (disable)
  RTCWrite(0x8C,0);                             //Set Alarm #2 to zll zeroes (disable)
  RTCWrite(0x8D,0);                             //Set Alarm #2 to zll zeroes (disable)
  RTCWrite(0x8F,B00000000);                     //reset flags                //8F=write to location for control/status flags    //B00000000=Ocillator Stop Flag 0, No Batt Backed 32 KHz Output, Keep Temp CONV Rate at 64 sec (may change later), disable 32 KHz output, temp Not Busy, alarm 2 not tripped, alarm 1 not tripped
  RTCWrite(0x8E,B00000101);                     //set control register       //8E=write to location for control register        //B01100101=Oscillator always on, SQW on, Convert Temp off, SQW freq@ 1Hz, Interrupt enabled, Alarm 2 off, Alarm 1 on
}

void RTCWrite(char reg, char val){              //function to send SPI data to RTC in the proper format
  SPI.setClockDivider(SPI_CLOCK_DIV16);         //Set SPI Clock Speed
  digitalWrite(RTC_CS, LOW);                    //enable SPI read/write for chip
  SPI.transfer(reg);                            //define memory register location
  SPI.transfer(bin2bcd(val));                   //write value
  digitalWrite(RTC_CS, HIGH);                   //disable SPI read/write for chip
}

void turnOn3v3SensorPower(){
  digitalWrite(SENSOR_POWER_3V3,LOW);        //Pin LOW=Power ON (P-Channel switch)
}
void turnOff3v3SensorPower(){
  digitalWrite(SENSOR_POWER_3V3,HIGH);      //Pin HIGH=Power OFF (P-Channel switch)
}

void turnOn5vSensorPower(){
  digitalWrite(SENSOR_POWER_5V,LOW);        //Pin LOW=Power ON (P-Channel switch)
}
void turnOff5vSensorPower(){
  digitalWrite(SENSOR_POWER_3V3,HIGH);      //Pin HIGH=Power OFF (P-Channel switch)
}

void turnOnFluorometer(){
  digitalWrite(FLUOROMETER_ON_OFF,LOW);     //Pin HIGH=Power OFF (P-Channel switch)
}
void turnOffFluorometer(){
  digitalWrite(FLUOROMETER_ON_OFF,HIGH);    //Pin HIGH=Power OFF (P-Channel switch)
}
void setFluorSensitivityX10(){
  digitalWrite(FLUOROMETER_X10,HIGH);         
  digitalWrite(FLUOROMETER_X100,LOW);         
}
void setFluorSensitivityX100(){
  digitalWrite(FLUOROMETER_X10,LOW);         
  digitalWrite(FLUOROMETER_X100,HIGH);         
}

void turnOnGPSPower(){
  digitalWrite(GPS_POWER,LOW);              //Pin LOW=Power ON (P-Channel switch)
}
void turnOffGPSPower(){
  digitalWrite(GPS_POWER,HIGH);             //Pin HIGH=Power OFF (P-Channel switch)
} 

void turnOnIridiumPower(){
  digitalWrite(IRIDIUM_POWER,LOW);          //Pin LOW=Power ON (P-Channel switch)
  digitalWrite(IRIDIUM_ON_OFF,HIGH);        //Digital Pin for Iridium module sleep.  HIGH = wake from sleep
}
void turnOffIridiumPower(){
  digitalWrite(IRIDIUM_POWER,HIGH);         //Pin HIGH=Power OFF (P-Channel switch)
  digitalWrite(IRIDIUM_ON_OFF,LOW);         //Digital Pin for Iridium module sleep.  LOW = go to sleep
}

void turnOnCameraPower(){
  digitalWrite(CAMERA_ON_OFF,LOW);          //Pin LOW=Power ON (P-Channel switch)
  digitalWrite(CAMERA_RESET,HIGH);          //Digital Pin for Camera Reset.  HIGH = not active
}
void turnOffCameraPower(){
  digitalWrite(CAMERA_ON_OFF,HIGH);         //Pin HIGH=Power OFF (P-Channel switch)
  digitalWrite(CAMERA_RESET,LOW);           //Digital Pin for Iridium module sleep.  LOW = active reset
}

void turnOnSerialComms(){
  digitalWrite(SERIAL_COMMS_ON_OFF,HIGH);    //Pin LOW=Power ON
}
void turnOffSerialComms(){
  digitalWrite(SERIAL_COMMS_ON_OFF,LOW);   //Pin HIGH=Power OFF
}

void shutdownUnit(){
  digitalWrite(SHUTDOWN,LOW);			//Pin Low=Power to entire unit off
  while(true){}							      //loop continuously and wait for unit to shutdown
}

bool ISBDCallback()				        //This function executes in the background while doing certain Iridium functions, we don't need it to do anything here
{
  return true;					          //Must return true to continue using ISBD protocol
}
