/*
*   RF_RECEIVER v3.3 for Arduino
*   Sketch to use an arduino as a receiver/sending device for digital signals
*
*   The Sketch can also encode and send data via a transmitter,
*   while only PT2262 type-signals for Intertechno devices are implemented in the sketch,
*   there is an option to send almost any data over a send raw interface
*   2014-2015  N.Butzek, S.Butzek
*   2016-2017 S.Butzek

*   This software focuses on remote sensors like weather sensors (temperature,
*   humidity Logilink, TCM, Oregon Scientific, ...), remote controlled power switches
*   (Intertechno, TCM, ARCtech, ...) which use encoder chips like PT2262 and
*   EV1527-type and manchester encoder to send information in the 433MHz Band.
*   But the sketch will also work for infrared or other medias. Even other frequencys
*   can be used
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


// Config flags for compiling correct options / boards Define only one
#define ARDUINO_ATMEGA328P_MINICUL 1
//#define ARDUINO_AVR_ICT_BOARDS_ICT_BOARDS_AVR_RADINOCC1101 1;
//#define OTHER_BOARD_WITH_CC1101  1

//#define CMP_MEMDBG 1

// #todo: header file für die Boards anlegen
#ifdef OTHER_BOARD_WITH_CC1101
	#define CMP_CC1101     
#endif
#ifdef ARDUINO_ATMEGA328P_MINICUL
	#define CMP_CC1101     
#endif
#ifdef ARDUINO_AVR_ICT_BOARDS_ICT_BOARDS_AVR_RADINOCC1101
	#define CMP_CC1101     
#endif



#define PROGVERS               "3.3.1-RC3"
#define PROGNAME               "RF_RECEIVER"
#define VERSION_1               0x33
#define VERSION_2               0x1d



#ifdef CMP_CC1101
	#ifdef ARDUINO_AVR_ICT_BOARDS_ICT_BOARDS_AVR_RADINOCC1101
		#define PIN_LED               13
		#define PIN_SEND              9   // gdo0Pin TX out
		#define PIN_RECEIVE				   7
		#define digitalPinToInterrupt(p) ((p) == 0 ? 2 : ((p) == 1 ? 3 : ((p) == 2 ? 1 : ((p) == 3 ? 0 : ((p) == 7 ? 4 : NOT_AN_INTERRUPT)))))
		#define PIN_MARK433			  4
		#define SS					  8  
	#elif ARDUINO_ATMEGA328P_MINICUL  // 8Mhz 
		#define PIN_LED               4
		#define PIN_SEND              2   // gdo0Pin TX out
		#define PIN_RECEIVE           3
		#define PIN_MARK433			  analogInputToDigitalPin(0)
	#else 
		#define PIN_LED               9
		#define PIN_SEND              3   // gdo0Pin TX out
	    #define PIN_RECEIVE           2
	#endif
#else
	#define PIN_RECEIVE            2
	#define PIN_LED                13 // Message-LED
	#define PIN_SEND               11
#endif

#define BAUDRATE               57600 // 500000 //57600
#define FIFO_LENGTH			   50 //150
//#define DEBUG				   1


#include <avr/wdt.h>
#include "FastDelegate.h"
#include "output.h"
#include "bitstore.h"
#include "signalDecoder.h"
#include <TimerOne.h>  // Timer for LED Blinking

#include "SimpleFIFO.h"
SimpleFIFO<int,FIFO_LENGTH> FiFo; //store FIFO_LENGTH # ints
SignalDetectorClass musterDec;


#include <EEPROM.h>
#include "cc1101.h"

#define pulseMin  90
volatile bool blinkLED = false;
String cmdstring = "";
volatile unsigned long lastTime = micros();
bool hasCC1101 = false;

#ifdef CMP_MEMDBG

extern unsigned int __data_start;
extern unsigned int __data_end;
extern unsigned int __bss_start;
extern unsigned int __bss_end;
extern unsigned int __heap_start;
extern void *__brkval;
uint8_t *heapptr, *stackptr;
uint16_t diff=0;
void check_mem() {
 stackptr = (uint8_t *)malloc(4);          // use stackptr temporarily
 heapptr = stackptr;                     // save value of heap pointer
 free(stackptr);      // free up the memory again (sets stackptr to 0)
 stackptr =  (uint8_t *)(SP);           // save value of stack pointer
}
//extern int __bss_end;
//extern void *__brkval;

int get_free_memory()
{
 int free_memory;

 if((int)__brkval == 0)
    free_memory = ((int)&free_memory) - ((int)&__bss_end);
 else
   free_memory = ((int)&free_memory) - ((int)__brkval);

 return free_memory;
}


int16_t ramSize=0;   // total amount of ram available for partitioning
int16_t dataSize=0;  // partition size for .data section
int16_t bssSize=0;   // partition size for .bss section
int16_t heapSize=0;  // partition size for current snapshot of the heap section
int16_t stackSize=0; // partition size for current snapshot of the stack section
int16_t freeMem1=0;  // available ram calculation #1
int16_t freeMem2=0;  // available ram calculation #2

#endif


// EEProm Address
#define EE_MAGIC_OFFSET      0
#define addr_features        0xff


void handleInterrupt();
void enableReceive();
void disableReceive();
void serialEvent();
void cronjob();
int freeRam();
void changeReciver();
void changeFilter();
void HandleCommand();
bool command_available=false;
unsigned long getUptime();
void getConfig();
void enDisPrint(bool enDis);
void getPing();
void configCMD();
void configSET();
void storeFunctions(const int8_t ms=1, int8_t mu=1, int8_t mc=1);
void getFunctions(bool *ms,bool *mu,bool *mc);
void initEEPROM(void);
void changeReceiver();
uint8_t cmdstringPos2int(uint8_t pos);
void printHex2(const byte hex);
uint8_t rssiCallback() { return 0; };	// Dummy return if no rssi value can be retrieved from receiver
size_t writeCallback(const uint8_t *buf, uint8_t len = 1);


void setup() {

	Serial.begin(BAUDRATE);
	while (!Serial) {
		; // wait for serial port to connect. Needed for native USB
	}
	if (MCUSR & (1 << WDRF)) {
		DBG_PRINTLN("Watchdog caused a reset");
	}
	/*
	if (MCUSR & (1 << BORF)) {
		DBG_PRINTLN("brownout caused a reset");
	}
	if (MCUSR & (1 << EXTRF)) {
		DBG_PRINTLN("external reset occured");
	}
	if (MCUSR & (1 << PORF)) {
		DBG_PRINTLN("power on reset occured");
	}
	*/
	wdt_reset();

	wdt_enable(WDTO_2S);  	// Enable Watchdog

	//delay(2000);
	pinAsInput(PIN_RECEIVE);
	pinAsOutput(PIN_LED);
	// CC1101
	
	wdt_reset();

	#ifdef CMP_CC1101
	cc1101::setup();
	#endif
  	initEEPROM();
	#ifdef CMP_CC1101
	DBG_PRINT(F("CCInit "));

	cc1101::CCinit();					 // CC1101 init
	hasCC1101 = cc1101::checkCC1101();	 // Check for cc1101
	
	if (hasCC1101)
	{
		DBG_PRINTLN("CC1101 found");
		musterDec.setRSSICallback(&cc1101::getRSSI);                    // Provide the RSSI Callback
	} else {
		musterDec.setRSSICallback(&rssiCallback);	// Provide the RSSI Callback		
	}
	#endif 

	pinAsOutput(PIN_SEND);
	DBG_PRINTLN("Starting timerjob");
	delay(50);

	Timer1.initialize(31*1000); //Interrupt wird jede n Millisekunden ausgeloest
	Timer1.attachInterrupt(cronjob);


	/*MSG_PRINT("MS:"); 	MSG_PRINTLN(musterDec.MSenabled);
	MSG_PRINT("MU:"); 	MSG_PRINTLN(musterDec.MUenabled);
	MSG_PRINT("MC:"); 	MSG_PRINTLN(musterDec.MCenabled);*/
	cmdstring.reserve(40);

	musterDec.setStreamCallback(&writeCallback);

	if (!hasCC1101 || cc1101::regCheck()) {
		enableReceive();
		DBG_PRINTLN(F("receiver enabled"));
	}
	else {
		DBG_PRINTLN(F("cc1101 is not correctly set. Please do a factory reset via command e"));
	}
}

void cronjob() {

	 const unsigned long  duration = micros() - lastTime;
	 
	 if (duration > maxPulse) { //Auf Maximalwert pr�fen.
		 int sDuration = maxPulse;
		 if (isLow(PIN_RECEIVE)) { // Wenn jetzt low ist, ist auch weiterhin low
			 sDuration = -sDuration;
		 }
		 FiFo.enqueue(sDuration);

		 lastTime = micros();
	

	 }
	 digitalWrite(PIN_LED, blinkLED);
	 blinkLED = false;
	/*
	 if (FiFo.count() >19)
	 {
		 DBG_PRINT("SF:"); DBG_PRINTLN(FiFo.count());
	 }
	 */

}


void loop() {
	int aktVal;
	bool state;
#ifdef __AVR_ATmega32U4__	
	serialEvent();
#endif
	if (command_available) {
		command_available=false;
		HandleCommand();
		if (!command_available) { cmdstring = ""; }
		blinkLED=true;
	}
	wdt_reset();
	while (FiFo.count()>0 ) { //Puffer auslesen und an Dekoder uebergeben

		aktVal=FiFo.dequeue();
		state = musterDec.decode(&aktVal); 
		if (state) blinkLED=true; //LED blinken, wenn Meldung dekodiert
	}

 }




//========================= Pulseauswertung ================================================
void handleInterrupt() {
  const unsigned long Time=micros();
  //const bool state = digitalRead(PIN_RECEIVE);
  const unsigned long  duration = Time - lastTime;
  lastTime = Time;
  if (duration >= pulseMin) {//kleinste zulaessige Pulslaenge
	int sDuration;
    if (duration < maxPulse) {//groesste zulaessige Pulslaenge, max = 32000
      sDuration = int(duration); //das wirft bereits hier unnoetige Nullen raus und vergroessert den Wertebereich
    }else {
      sDuration = maxPulse; // Maximalwert set to maxPulse defined in lib.
    }
    if (isHigh(PIN_RECEIVE)) { // Wenn jetzt high ist, dann muss vorher low gewesen sein, und dafuer gilt die gemessene Dauer.
      sDuration=-sDuration;
    }
	//MSG_PRINTLN(sDuration);
    FiFo.enqueue(sDuration);


    //++fifocnt;
  } // else => trash

}

void enableReceive() {
	attachInterrupt(digitalPinToInterrupt(PIN_RECEIVE), handleInterrupt, CHANGE);
   #ifdef CMP_CC1101
   if (hasCC1101) cc1101::setReceiveMode();
   #endif
}

void disableReceive() {
  detachInterrupt(digitalPinToInterrupt(PIN_RECEIVE));

  #ifdef CMP_CC1101
  if (hasCC1101) cc1101::setIdleMode();
  #endif
  FiFo.flush();

}

//============================== Write callback =========================================
size_t writeCallback(const uint8_t *buf, uint8_t len = 1)
{
	while (!MSG_PRINTER.availableForWrite() )
		yield();
	//DBG_PRINTLN("Called writeCallback");

	//MSG_PRINT(*buf);
	//MSG_WRITE(buf, len);
	MSG_PRINTER.write(buf,len);
	
	//serverClient.write("test");

}

//================================= RAW Send ======================================
void send_raw(const uint8_t startpos,const uint16_t endpos,const int16_t *buckets, String *source=&cmdstring)
{
	uint8_t index=0;
	unsigned long stoptime=micros();
	bool isLow;
	uint16_t dur;
	for (uint16_t i=startpos;i<=endpos;i++ )
	{
		//DBG_PRINT(cmdstring.substring(i,i+1));
		index = source->charAt(i) - '0';
		//DBG_PRINT(index);
		isLow=buckets[index] >> 15;
		dur = abs(buckets[index]); 		//isLow ? dur = abs(buckets[index]) : dur = abs(buckets[index]);

		while (stoptime > micros()){
			;
		}
		isLow ? digitalLow(PIN_SEND): digitalHigh(PIN_SEND);
		stoptime+=dur;
	}
	while (stoptime > micros()){
		;
	}
	//DBG_PRINTLN("");

}
//SM;R=2;C=400;D=AFAFAF;




void send_mc(const uint8_t startpos,const uint8_t endpos, const int16_t clock)
{
	int8_t b;
	char c;
	//digitalHigh(PIN_SEND);
	//delay(1);
	uint8_t bit;

	unsigned long stoptime =micros();
	for (uint8_t i = startpos; i <= endpos; i++) {
		c = cmdstring.charAt(i);
		b = ((byte)c) - (c <= '9' ? 0x30 : 0x37);

		for (bit = 0x8; bit>0; bit >>= 1) {
			for (byte i = 0; i <= 1; i++) {
				if ((i == 0 ? (b & bit) : !(b & bit)))
					digitalLow(PIN_SEND);
				else
					digitalHigh(PIN_SEND);
				
					stoptime += clock;
					while (stoptime > micros())
						yield();
			}
			
		}
		
	}
	// MSG_PRINTLN("");
}



bool split_cmdpart(int16_t *startpos, String *msg_part)
{
	int16_t endpos=0;
	//startpos=cmdstring.indexOf(";",startpos);   			 // search first  ";"
	endpos=cmdstring.indexOf(";",*startpos);     			 // search next   ";"

	if (endpos ==-1 || *startpos== -1) return false;
	*msg_part = cmdstring.substring(*startpos,endpos);
	*startpos=endpos+1;    // Set startpos to endpos to extract next part
	return true;
}
// SC;R=4;SM;C=400;D=AFFFFFFFFE;SR;P0=-2500;P1=400;D=010;SM;D=AB6180;SR;D=101;
// SC;R=4;SM;C=400;D=FFFFFFFF;SR;P0=-400;P1=400;D=101;SM;D=AB6180;SR;D=101;
// SR;R=3;P0=1230;P1=-3120;P2=-400;P3=-900;D=030301010101010202020202020101010102020202010101010202010120202;
// SM;C=400;D=AAAAFFFF;
// SR;R=10;P0=-2000;P1=-1000;P2=500;P3=-6000;D=2020202021212020202121212021202021202121212023;

struct s_sendcmd {
	int16_t sendclock=0;
	uint8_t type;
	uint8_t datastart=0;
	uint16_t dataend=0;
	int16_t buckets[6];
	uint8_t repeats=1;
} ;

void send_cmd()
{
	#define combined 0
	#define manchester 1
	#define raw 2

	String msg_part;
	msg_part.reserve(30);
	uint8_t repeats=1;  // Default is always one iteration so repeat is 1 if not set
	//uint8_t type;
	int16_t start_pos=0;
	//int16_t buckets[6]={};
	uint8_t counter=0;
	//uint16_t sendclock;
	bool extraDelay = true;

	s_sendcmd command[5];

	uint8_t ccParamAnz = 0;   // Anzahl der per F= uebergebenen cc1101 Register
	uint8_t ccReg[4];
	uint8_t val;

	disableReceive();

	uint8_t cmdNo=255;


	while (split_cmdpart(&start_pos,&msg_part))
	{
		DBG_PRINTLN(msg_part);
		if (msg_part.charAt(0) == 'S')
		{
			if (msg_part.charAt(1) == 'C')  // send combined informatio flag
			{
				//type=combined;
				//cmdNo=255;
				cmdNo++;
				//command[cmdNo].repeats = 0;
				command[cmdNo].type = combined;
				extraDelay = false;
			}
			else if (msg_part.charAt(1) == 'M') // send manchester
			{
				//type=manchester;
				cmdNo++;
				//command[cmdNo].repeats = 0;
				command[cmdNo].type=manchester;
				DBG_PRINTLN("Adding manchester");

			}
			else if (msg_part.charAt(1) == 'R') // send raw
			{
				//type=raw;
				cmdNo++;
				//command[cmdNo].repeats = 0;
				command[cmdNo].type=raw;
				DBG_PRINTLN("Adding raw");
				extraDelay = false;
			}
		}
		else if (msg_part.charAt(0) == 'P' && msg_part.charAt(2) == '=') // Do some basic detection if data matches what we expect
		{
			counter = msg_part.substring(1,2).toInt(); // extract the pattern number
			//buckets[counter]=  msg_part.substring(3).toInt();
			command[cmdNo].buckets[counter]=msg_part.substring(3).toInt();
			DBG_PRINTLN("Adding bucket");

		} else if(msg_part.charAt(0) == 'R' && msg_part.charAt(1) == '=') {
			command[cmdNo].repeats = msg_part.substring(2).toInt();
			DBG_PRINT("Adding repeats: "); DBG_PRINTLN(command[cmdNo].repeats);


		} else if (msg_part.charAt(0) == 'D') {
			command[cmdNo].datastart = start_pos - msg_part.length()+1;
			command[cmdNo].dataend = start_pos-2;
			DBG_PRINT("locating data start:");
			DBG_PRINT(command[cmdNo].datastart);
			DBG_PRINT(" end:");
			DBG_PRINTLN(command[cmdNo].dataend);
			//if (type==raw) send_raw(&msg_part,buckets);
			//if (type==manchester) send_mc(&msg_part,sendclock);
			//digitalWrite(PIN_SEND, LOW); // turn off transmitter
			//digitalLow(PIN_SEND);
		} else if(msg_part.charAt(0) == 'C' && msg_part.charAt(1) == '=')
		{
			//sendclock = msg_part.substring(2).toInt();
			command[cmdNo].sendclock = msg_part.substring(2).toInt();
			DBG_PRINTLN("adding sendclock");
		} else if(msg_part.charAt(0) == 'F' && msg_part.charAt(1) == '=')
		{
			ccParamAnz = msg_part.length() / 2 - 1;
			
			if (ccParamAnz > 0 && ccParamAnz <= 5 && hasCC1101) {
				uint8_t hex;
				DBG_PRINTLN("write new ccreg  ");
				for (uint8_t i=0;i<ccParamAnz;i++)
				{
					ccReg[i] = cc1101::readReg(0x0d + i, 0x80);    // alte Registerwerte merken
					hex = (uint8_t)msg_part.charAt(2 + i*2);
					val = cc1101::hex2int(hex) * 16;
					hex = (uint8_t)msg_part.charAt(3 + i*2);
					val = cc1101::hex2int(hex) + val;
					cc1101::writeReg(0x0d + i, val);            // neue Registerwerte schreiben
					printHex2(val);
				}
				DBG_PRINTLN("");
			}
		}
	}

	#ifdef CMP_CC1101
	if (hasCC1101) cc1101::setTransmitMode();	
	#endif


	if (command[0].type == combined && command[0].repeats > 0) {
		repeats = command[0].repeats;
	}
	for (uint8_t i = 0;i < repeats; i++)
	{
		DBG_PRINT("repeat "); DBG_PRINT(i); DBG_PRINT("/"); DBG_PRINT(repeats);
		
		for (uint8_t c = 0;c <= cmdNo ;c++)
		{
			DBG_PRINT(" cmd "); DBG_PRINT(c); DBG_PRINT("/"); DBG_PRINT(cmdNo);
			DBG_PRINT(" reps "); DBG_PRINT(command[c].repeats);

			if (command[c].type == raw) { for (uint8_t rep = 0; rep < command[c].repeats; rep++) send_raw(command[c].datastart, command[c].dataend, command[c].buckets); }
			else if (command[c].type == manchester) { for (uint8_t rep = 0; rep < command[c].repeats; rep++)send_mc(command[c].datastart, command[c].dataend, command[c].sendclock); }
			digitalLow(PIN_SEND);
			DBG_PRINT(".");

		}
		DBG_PRINTLN(" ");

		if (extraDelay) delay(1);
	}

	if (ccParamAnz > 0) {
		DBG_PRINT("ccreg write back ");
		for (uint8_t i=0;i<ccParamAnz;i++)
		{
			val = ccReg[i];
			printHex2(val);
			cc1101::writeReg(0x0d + i, val);    // gemerkte Registerwerte zurueckschreiben
		}
		DBG_PRINTLN("");
	}

	MSG_PRINTLN(cmdstring); // echo
	musterDec.reset();
	enableReceive();	// enable the receiver
}





//================================= Kommandos ======================================
void IT_CMDs();

void HandleCommand()
{
  uint8_t reg;
  uint8_t val;
  
  #define  cmd_Version 'V'
  #define  cmd_freeRam 'R'
  #define  cmd_uptime 't'
  #define  cmd_changeReceiver 'X'
  #define  cmd_space ' '
  #define  cmd_help '?'
  #define  cmd_changeFilter 'F'
  #define  cmd_send 'S'
  #define  cmd_ping 'P'
  #define  cmd_config 'C'     // CG get config, set config, C<reg> get CC1101 register
  #define  cmd_write 'W'      // write EEPROM und write CC1101 register
  #define  cmd_read  'r'      // read EEPROM
  #define  cmd_patable 'x' 
  #define  cmd_ccFactoryReset 'e'  // EEPROM / factory reset


  if (cmdstring.charAt(0) == cmd_ping){
	getPing();
  }  // ?: Kommandos anzeigen
  else if (cmdstring.charAt(0) == cmd_help) {
	MSG_PRINT(cmd_help);	MSG_PRINT(F(" Use one of "));
	MSG_PRINT(cmd_Version);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_freeRam);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_uptime);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_changeReceiver);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_changeFilter);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_send);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_ping);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_config);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_read);MSG_PRINT(cmd_space);
	MSG_PRINT(cmd_write);MSG_PRINT(cmd_space);
	if (hasCC1101) {
		MSG_PRINT(cmd_patable);MSG_PRINT(cmd_space);
		MSG_PRINT(cmd_ccFactoryReset);MSG_PRINT(cmd_space);
	}
	MSG_PRINTLN("");
  }
  // V: Version
  else if (cmdstring.charAt(0) == cmd_Version) {
	  MSG_PRINT("V " PROGVERS " SIGNALduino ");
	  if (hasCC1101) {
		MSG_PRINT(F("cc1101 "));
	    #ifdef PIN_MARK433
	    MSG_PRINT("(");
	    MSG_PRINT(isLow(PIN_MARK433) ? "433" : "868");
	    MSG_PRINT(F("Mhz )"));
	    #endif
      }
	MSG_PRINTLN("- compiled at " __DATE__ " " __TIME__)

  }
  // R: FreeMemory
  else if (cmdstring.charAt(0) == cmd_freeRam) {
    MSG_PRINTLN(freeRam());
  }
  else if (cmdstring.charAt(0) == cmd_send) {
  	if (musterDec.getState() != searching )
	{
		command_available=true;
	} else {
		send_cmd(); // Part of Send
	}
  }
    // t: Uptime
  else if (cmdstring.charAt(0) == cmd_uptime) {
		MSG_PRINTLN(getUptime());
  }
  // XQ disable receiver
  else if (cmdstring.charAt(0) == cmd_changeReceiver) {
    changeReceiver();
  }
  else if (cmdstring.charAt(0) == cmd_changeFilter) {
  }
  else if (cmdstring.charAt(0) == cmd_config) {
	  if (cmdstring.charAt(1) == 'G') {
		  getConfig();
	  }
	  else if (cmdstring.charAt(1) == 'E' || cmdstring.charAt(1) == 'D') {  //Todo:  E und D sind auch hexadezimal, werden hier aber abgefangen
		  configCMD();
	  }
	  else if (cmdstring.charAt(1) == 'S') {
		  configSET();
	  }
      else if (isHexadecimalDigit(cmdstring.charAt(1)) && isHexadecimalDigit(cmdstring.charAt(2)) && hasCC1101) {
		reg = cmdstringPos2int(1);
		cc1101::readCCreg(reg);
      }
    else {
      MSG_PRINTLN(F("Unsupported command"));
    }
  }
  else if (cmdstring.charAt(0) == cmd_write) {            // write EEPROM und CC11001 register
    if (cmdstring.charAt(1) == 'S' && cmdstring.charAt(2) == '3' && hasCC1101)  {       // WS<reg>  Command Strobes
        cc1101::commandStrobes();
    } else if (isHexadecimalDigit(cmdstring.charAt(1)) && isHexadecimalDigit(cmdstring.charAt(2)) && isHexadecimalDigit(cmdstring.charAt(3)) && isHexadecimalDigit(cmdstring.charAt(4))) {
         reg = cmdstringPos2int(1);
         val = cmdstringPos2int(3);
         EEPROM.write(reg, val);  
         if (hasCC1101) {
           cc1101::writeCCreg(reg, val);
         }
    } else {
         MSG_PRINTLN(F("Unsupported command"));
    }
  }
  // R<adr>  read EEPROM
  else if (cmdstring.charAt(0) == cmd_read && isHexadecimalDigit(cmdstring.charAt(1)) && isHexadecimalDigit(cmdstring.charAt(2))) {             // R<adr>  read EEPROM
     reg = cmdstringPos2int(1);
     MSG_PRINT(F("EEPROM "));
     printHex2(reg);
     if (cmdstring.charAt(3) == 'n') {
         MSG_PRINT(F(" :"));
         for (uint8_t i = 0; i < 16; i++) {
             MSG_PRINT(" ");
             printHex2(EEPROM.read(reg + i));
         }
     } else {
        MSG_PRINT(F(" = "));
        printHex2(EEPROM.read(reg));
     }
     MSG_PRINTLN("");
  }
  else if (cmdstring.charAt(0) == cmd_patable && isHexadecimalDigit(cmdstring.charAt(1)) && isHexadecimalDigit(cmdstring.charAt(2)) && hasCC1101) {
     val = cmdstringPos2int(1);
     cc1101::writeCCpatable(val);
     MSG_PRINT(F("Write "));
     printHex2(val);
     MSG_PRINTLN(F(" to PATABLE done"));
  }
  else if (cmdstring.charAt(0) == cmd_ccFactoryReset && hasCC1101) { 
     cc1101::ccFactoryReset();
     cc1101::CCinit();
  }
  else {
	  MSG_PRINTLN(F("Unsupported command"));
  }
}


uint8_t cmdstringPos2int(uint8_t pos) {
  uint8_t val;
  uint8_t hex;
  
       hex = (uint8_t)cmdstring.charAt(pos);
       val = cc1101::hex2int(hex) * 16;
       hex = (uint8_t)cmdstring.charAt(pos+1);
       val = cc1101::hex2int(hex) + val;
       return val;
}


inline void getConfig()
{
   MSG_PRINT(F("MS="));
   MSG_PRINT(musterDec.MSenabled,DEC);
   MSG_PRINT(F(";MU="));
   MSG_PRINT(musterDec.MUenabled, DEC);
   MSG_PRINT(F(";MC="));
   MSG_PRINT(musterDec.MCenabled, DEC);
   MSG_PRINT(F(";Mred="));
   MSG_PRINTLN(musterDec.MredEnabled, DEC);
}


inline void configCMD()
{
  bool *bptr;

  if (cmdstring.charAt(2) == 'S') {  	  //MS
	bptr=&musterDec.MSenabled;
  }
  else if (cmdstring.charAt(2) == 'U') {  //MU
	bptr=&musterDec.MUenabled;
  }
  else if (cmdstring.charAt(2) == 'C') {  //MC
	bptr=&musterDec.MCenabled;
  }
  else if (cmdstring.charAt(2) == 'R') {  //Mreduce
	  bptr = &musterDec.MredEnabled;
  }

  if (cmdstring.charAt(1) == 'E') {   // Enable
	*bptr=true;
  }
  else if (cmdstring.charAt(1) == 'D') {  // Disable
	*bptr=false;
  } else {
	return;
  }
  storeFunctions(musterDec.MSenabled, musterDec.MUenabled, musterDec.MCenabled, musterDec.MredEnabled);
}

inline void configSET()
{ 
	//MSG_PRINT(cmdstring.substring(2, 8));
	if (cmdstring.substring(2,8) == "mcmbl=")    // mc min bit len
	{	
		musterDec.mcMinBitLen = cmdstring.substring(8).toInt(); 
		MSG_PRINT(musterDec.mcMinBitLen); MSG_PRINT(" bits set");
	}
}

void serialEvent()
{
  while (MSG_PRINTER.available())
  {
    char inChar = (char)MSG_PRINTER.read(); 
    switch(inChar)
    {
    case '\n':
    case '\r':
    case '\0':
    case '#':
		command_available=true;
		break;
    default:
      cmdstring += inChar;
    }
  }
}


int freeRam () {
#ifdef CMP_MEMDBG

 check_mem();

 MSG_PRINT("\nheapptr=[0x"); MSG_PRINT( (int) heapptr, HEX); MSG_PRINT("] (growing upward, "); MSG_PRINT( (int) heapptr, DEC); MSG_PRINT(" decimal)");

 MSG_PRINT("\nstackptr=[0x"); MSG_PRINT( (int) stackptr, HEX); MSG_PRINT("] (growing downward, "); MSG_PRINT( (int) stackptr, DEC); MSG_PRINT(" decimal)");

 MSG_PRINT("\ndifference should be positive: diff=stackptr-heapptr, diff=[0x");
 diff=stackptr-heapptr;
 MSG_PRINT( (int) diff, HEX); MSG_PRINT("] (which is ["); MSG_PRINT( (int) diff, DEC); MSG_PRINT("] (bytes decimal)");


 MSG_PRINT("\n\nLOOP END: get_free_memory() reports [");
 MSG_PRINT( get_free_memory() );
 MSG_PRINT("] (bytes) which must be > 0 for no heap/stack collision");


 // ---------------- Print memory profile -----------------
 MSG_PRINT("\n\n__data_start=[0x"); MSG_PRINT( (int) &__data_start, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) &__data_start, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__data_end=[0x"); MSG_PRINT((int) &__data_end, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) &__data_end, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__bss_start=[0x"); MSG_PRINT((int) & __bss_start, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) &__bss_start, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__bss_end=[0x"); MSG_PRINT( (int) &__bss_end, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) &__bss_end, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__heap_start=[0x"); MSG_PRINT( (int) &__heap_start, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) &__heap_start, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__malloc_heap_start=[0x"); MSG_PRINT( (int) __malloc_heap_start, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) __malloc_heap_start, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__malloc_margin=[0x"); MSG_PRINT( (int) &__malloc_margin, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) &__malloc_margin, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\n__brkval=[0x"); MSG_PRINT( (int) __brkval, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) __brkval, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\nSP=[0x"); MSG_PRINT( (int) SP, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) SP, DEC); MSG_PRINT("] bytes decimal");

 MSG_PRINT("\nRAMEND=[0x"); MSG_PRINT( (int) RAMEND, HEX ); MSG_PRINT("] which is ["); MSG_PRINT( (int) RAMEND, DEC); MSG_PRINT("] bytes decimal");

 // summaries:
 ramSize   = (int) RAMEND       - (int) &__data_start;
 dataSize  = (int) &__data_end  - (int) &__data_start;
 bssSize   = (int) &__bss_end   - (int) &__bss_start;
 heapSize  = (int) __brkval     - (int) &__heap_start;
 stackSize = (int) RAMEND       - (int) SP;
 freeMem1  = (int) SP           - (int) __brkval;
 freeMem2  = ramSize - stackSize - heapSize - bssSize - dataSize;
 MSG_PRINT("\n--- section size summaries ---");
 MSG_PRINT("\nram   size=["); MSG_PRINT( ramSize, DEC ); MSG_PRINT("] bytes decimal");
 MSG_PRINT("\n.data size=["); MSG_PRINT( dataSize, DEC ); MSG_PRINT("] bytes decimal");
 MSG_PRINT("\n.bss  size=["); MSG_PRINT( bssSize, DEC ); MSG_PRINT("] bytes decimal");
 MSG_PRINT("\nheap  size=["); MSG_PRINT( heapSize, DEC ); MSG_PRINT("] bytes decimal");
 MSG_PRINT("\nstack size=["); MSG_PRINT( stackSize, DEC ); MSG_PRINT("] bytes decimal");
 MSG_PRINT("\nfree size1=["); MSG_PRINT( freeMem1, DEC ); MSG_PRINT("] bytes decimal");
 MSG_PRINT("\nfree size2=["); MSG_PRINT( freeMem2, DEC ); MSG_PRINT("] bytes decimal");
#else
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
#endif // CMP_MEMDBG

 }

inline unsigned long getUptime()
{
	unsigned long now = millis();
	static uint16_t times_rolled = 0;
	static unsigned long last = 0;
	// If this run is less than the last the counter rolled
	unsigned long seconds = now / 1000;
	if (now < last) {
		times_rolled++;
		seconds += (( long(4294967295) / 1000 )*times_rolled);
	}
	last = now;
	return seconds;
}

inline void getPing()
{
	MSG_PRINTLN("OK");
	delayMicroseconds(500);
}

inline void changeReceiver() {
  if (cmdstring.charAt(1) == 'Q')
  {
  	disableReceive();
  }
  if (cmdstring.charAt(1) == 'E')
  {
  	enableReceive();
  }
}

  void printHex2(const byte hex) {   // Todo: printf oder scanf nutzen
    if (hex < 16) {
      MSG_PRINT("0");
    }
    MSG_PRINT(hex, HEX);
  }



//================================= EEProm commands ======================================

void storeFunctions(const int8_t ms, int8_t mu, int8_t mc, int8_t red)
{
	mu=mu<<1;
	mc=mc<<2;
	red = red << 3;

	int8_t dat = ms | mu | mc | red;
	EEPROM.write(addr_features,dat);
}

void getFunctions(bool *ms,bool *mu,bool *mc, bool *red)
{
    int8_t dat = EEPROM.read(addr_features);

    *ms=bool (dat &(1<<0));
    *mu=bool (dat &(1<<1));
    *mc=bool (dat &(1<<2));
	*red = bool(dat &(1 << 3));


}

void initEEPROM(void) {

  if (EEPROM.read(EE_MAGIC_OFFSET) == VERSION_1 && EEPROM.read(EE_MAGIC_OFFSET+1) == VERSION_2) {
    DBG_PRINTLN("Reading values fom eeprom");
  } else {
    storeFunctions(1, 1, 1,1);    // Init EEPROM with all flags enabled
    //hier fehlt evtl ein getFunctions()
    MSG_PRINTLN(F("Init eeprom to defaults after flash"));
    EEPROM.write(EE_MAGIC_OFFSET, VERSION_1);
    EEPROM.write(EE_MAGIC_OFFSET+1, VERSION_2);
    #ifdef CMP_CC1101
       cc1101::ccFactoryReset();
    #endif
  }
  getFunctions(&musterDec.MSenabled, &musterDec.MUenabled, &musterDec.MCenabled,&musterDec.MredEnabled);

}



