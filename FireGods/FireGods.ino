
#include <avr/sleep.h>
#include <avr/power.h>
#include "../FireMosfet/FireControl.h"
#include <MemoryFree.h>


//#define DoSerial 
#undef DoSerial

#define NUM_FIRES 5

#define NUM_DIGITS 6
#define NUM_COLUMNS 4
#define NUM_ROWS 4
#define MIN_VALVE_ON_TIME 200
#define GAS_DURATION_TIME 20000
#define IGNITER_STAGGER 1000

#define PLAY_WAIT_TIME 10
#define PLAY_WAIT_TIME_GODS 7
#define TANK_RECHARGE_TIME 300
#define TANK_RECHARGE_TIME_SHORT 3

void SendKeyEvent(int key, bool onOff, unsigned long timeStamp);
void StoreEvent(int key, bool onOff, unsigned int timeStamp);
void PlayEvent(int key, bool onOff);
void UpdateDisplayCheck();
void SegmentDecode();
void decodeKeyPress();
void BlinkFeedback();
void BlankDisplay();
void ClearAllKeys();
void ResetClearAll();
void FatalError(byte val);
void SendCommand(CmdRsp_t* a_CmdRspOut);
void SetFET(byte l_FET, bool l_OnOff);
void AllOff();
void SendHeartBeat(void);
void DisplaySeconds(int secs);

void StartIgniters(unsigned long deltaTime);


typedef enum
{
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_START,
	KEY_STOP,
	KEY_PLAY,
	KEY_PANIC,
	KEY_SPARE_1,
	KEY_SPARE_2,
	KEY_SPARE_3,
	KEY_SPARE_4,
	KEY_SPARE_5,
	KEY_SPARE_6,
	KEY_SPARE_7,
	KEY_SPARE_8
}keyval_t;

typedef enum
{
	STATE_IDLE,
	STATE_RECORDING,
	STATE_READY,
	STATE_WAIT_PLAY,
	STATE_PLAYING,
	STATE_TANK_RECHARGE,
	STATE_ERROR,
} state_t;

typedef struct
{
	byte key;
	bool onOff;
	unsigned int timestamp;
} event_t;


unsigned int lastKeyOn[KEY_5 + 1];

byte keyDecode[NUM_ROWS][NUM_COLUMNS] = 
{
	{ KEY_4, KEY_SPARE_8, KEY_SPARE_4, KEY_PANIC },
	{ KEY_3, KEY_SPARE_7, KEY_SPARE_3, KEY_PLAY },
	{ KEY_2, KEY_SPARE_6, KEY_SPARE_2, KEY_STOP },
	{ KEY_1, KEY_SPARE_5, KEY_SPARE_1, KEY_START },
};



volatile int currentState = STATE_IDLE;                                        // Must be int, <0 is used

volatile int tempINT = 0;
byte i = 0;
byte currentKey = 0;

#define MAX_EVENTS 200
int recIndex;
int playIndex;
event_t events[MAX_EVENTS];


bool godsSpeaking = false;
bool godsPleased = false;


byte disp[NUM_DIGITS] =
{
	10, 10, 10, 10, 10, 0
};                             // 0 to 9 show, 10 = display off, 11 = "-" symbol, 12 = "E" for error

byte column = 0;

byte rawKeys = 0;
byte lastRawKeys[NUM_COLUMNS];
boolean key5Val = false;
boolean lastKey5Val = false;

volatile byte ErrorCode = 0;                         // Where 1 = number too big (ie more than 6 digits)
//       2 = WDT overflow
//       3 = Brown-out
//       4 = Switch case jump too high
//       5 = External reset
//       6 = Power On reset

volatile boolean ErrorFlag = false;

CmdRsp_t g_CmdRspIn;
CmdRsp_t g_CmdRspOut;

bool g_HeartBeatAckSeen;
unsigned long g_HeartBeatAckTime;
unsigned long g_HeartBeatTime;
unsigned long g_FETTime;
byte g_HeartBeat;

unsigned long recStartTime;
unsigned long playStartTime;
unsigned long waitStartTime;
unsigned int totalOnTime;

byte igniters[NUM_FIRES] =	 {	IGNITER_0,	IGNITER_1,	IGNITER_2,	IGNITER_3,	IGNITER_4	}; 
bool igniterState[NUM_FIRES];
byte valves[NUM_FIRES] =	 { 	VALVE_0,	VALVE_1,	VALVE_2,	VALVE_3,	VALVE_4		}; 

void setup()
{
	pinMode(2, INPUT);                                 // CLEAR / ON BUTTON (USED TO WAKE CALCULATOR)
	//  pinMode(3, INPUT_PULLUP);                          // 2nd Function button selector (This pin is currently not connected)

	DDRB = DDRB | 1 << PORTB0;                         // SEGMENT A
	DDRB = DDRB | 1 << PORTB1;                         // SEGMENT B
	DDRB = DDRB | 1 << PORTB2;                         // SEGMENT C
	DDRB = DDRB | 1 << PORTB3;                         // SEGMENT D
	DDRB = DDRB | 1 << PORTB4;                         // SEGMENT E
	DDRB = DDRB | 1 << PORTB5;                         // SEGMENT F
	DDRB = DDRB | 1 << PORTB6;                         // SEGMENT G
	DDRB = DDRB | 1 << PORTB7;                         // SEGMENT DECIMAL PLACE

	pinMode(4, INPUT);                                 // KEYPAD ROW 0 (TOP ROW)
	pinMode(5, INPUT);                                 // KEYPAD ROW 1
	pinMode(6, INPUT);                                 // KEYPAD ROW 2
	pinMode(7, INPUT);                                 // KEYPAD ROW 3 (BOTTOM ROW)

	digitalWrite(4,LOW);                               // This is just an attemp to reduce power use
	digitalWrite(5,LOW);                               // These pins are pulled down to GND with 10K resistors
	digitalWrite(6,LOW);
	digitalWrite(7,LOW);

	pinMode(A0, OUTPUT);                               // KEYPAD COLUMN 0 (RIGHT MOST) AND DIGIT 0 (RIGHT MOST)
	pinMode(A1, OUTPUT);                               // KEYPAD COLUMN 1 AND DIGIT 1
	pinMode(A2, OUTPUT);                               // KEYPAD COLUMN 2 AND DIGIT 2
	pinMode(A3, OUTPUT);                               // KEYPAD COLUMN 3 AND DIGIT 3
	pinMode(A4, OUTPUT);                               // DIGIT 4
	pinMode(A5, OUTPUT);                               // DIGIT 5

#ifdef DoSerial
	Serial.begin(38400);
#else
	Serial.begin(9600);
#endif

	ClearAllKeys();

	// Error reporting ------------------------------------------------------------------------------- S
	// Where 1 = number too big (ie more than 6 digits)
	//       2 = WDT overflow
	//       3 = Brown-out
	//       4 = Switch case jump too high
	//       5 = External reset
	//       6 = Power On reset
	//       7 = WDT System reset
	//       9 = Multiple Errors

	tempINT = MCUSR & B00001111;                        // RES,RES,RES,RES,WDRF,BORF,EXTRF,PORF

	if(tempINT > 0)
	{
		ErrorFlag = true;
		switch(tempINT)
		{
		case 1:
			ErrorCode = 6;
			break;

		case 2:
			ErrorCode = 5;
			break;

		case 4:
			ErrorCode = 3;
			break;

		case 8:
			ErrorCode = 7;
			break;

		default:
			//      ErrorCode = 9;
			if(bitRead(tempINT,0))
			{
				ErrorCode = 6;                              // Power on
			}
			else
			{
				ErrorCode = 9;                              // Other multiple errors
			}
			break;
		}
	}

	if(bitRead(WDTCSR, WDIF))
	{
		ErrorFlag = true;
		ErrorCode = 7;
		bitSet(WDTCSR, WDIF);                        // Write a 1 to clear ???
	}

	MCUSR = 0;
	// Error reporting ------------------------------------------------------------------------------- E


	// init this to say we are starting
	g_HeartBeatAckSeen = false;
	g_HeartBeatTime = millis();
	g_HeartBeatAckTime = millis();
}



void loop()
{
	unsigned long l_TimeNow;
	unsigned int deltaTime;
	unsigned int secsToGo;

	// generate HeartBeat
	l_TimeNow = millis();
#ifndef DoSerial
	if ((l_TimeNow - g_HeartBeatTime) > 300)
	{
		SendHeartBeat();
		g_HeartBeatTime = l_TimeNow;
	}

	// check for HeartBeat Ack
	if ((l_TimeNow - g_HeartBeatAckTime) > 1000)
	{
		g_HeartBeatAckSeen = false;
		g_HeartBeatAckTime = l_TimeNow;
	}


	if(Serial.available() >= 3)
	{
		byte c;
	
		c = Serial.read();
		if (!g_HeartBeatAckSeen)
		{
			if (c != TAG)
				goto NotSync;
		}
		g_CmdRspIn.tag = c;
		g_CmdRspIn.command = Serial.read();
		g_CmdRspIn.arg = Serial.read();
#ifdef DEBUG
		DebugSerial.print("RSP ");
		DebugSerial.print(g_CmdRspIn.tag);
		DebugSerial.print(" ");
		DebugSerial.print(g_CmdRspIn.command);
		DebugSerial.print(" ");
		DebugSerial.println(g_CmdRspIn.arg);
#endif

		//Validity Checking
		if (g_CmdRspIn.tag != TAG)
			FatalError(0xff);

//		if (g_CmdRspIn.command >= cmd_Max)
//			FatalError(cmd_Max);

		switch (g_CmdRspIn.command)
		{
			case cmd_HeartbeatAck:
				g_HeartBeatAckSeen = true;
				g_HeartBeatAckTime = millis();;
#ifdef DEBUG
				DebugSerial.println("ACK");
#endif
				break;

			// these should not be seen
			// so stop everything
			case cmd_Heartbeat:
			case cmd_FETOn:
			case cmd_FETOff:
			case cmd_AllOff:
			default:
				break;
//				FatalError(g_CmdRspIn.command);
		}
	}
NotSync:
#endif

	UpdateDisplayCheck();                                    // Check for button press and Update Display LEDs

#if 0
	if(ErrorFlag)
	{
		currentState = STATE_ERROR;
	}
#endif

	disp[5] = currentState;
	l_TimeNow = millis();
	switch (currentState)                                            // Calculator working statemachine
	{

		case STATE_IDLE:
			break;
		case STATE_RECORDING:
			if ((l_TimeNow - recStartTime) > 60000)
			{
				if (recIndex == 0)
					currentState = STATE_IDLE;
				else 
					currentState = STATE_READY;

			}
			break;
		case STATE_READY:
			break;

		case STATE_WAIT_PLAY:
			deltaTime = (unsigned short)((l_TimeNow - waitStartTime) / 1000);
			StartIgniters(l_TimeNow - waitStartTime);
			if (godsSpeaking)
				secsToGo = PLAY_WAIT_TIME_GODS - deltaTime;
			else
				secsToGo = PLAY_WAIT_TIME - deltaTime;
			
			if (secsToGo == 0)
			{
				disp[0] = 10;
				playStartTime = millis();
				totalOnTime = 0;
				playIndex = 0;
				currentState = STATE_PLAYING;
				for (i=0; i<= KEY_5; i++)
					lastKeyOn[i] = 0;
			}
			DisplaySeconds(secsToGo);
			break;

		case STATE_PLAYING:
			while ((l_TimeNow - playStartTime) > events[playIndex].timestamp)
			{
				PlayEvent(events[playIndex].key, events[playIndex].onOff);
				++playIndex;
				if (playIndex >= recIndex)
				{
					AllOff();
					currentState = STATE_TANK_RECHARGE;
					waitStartTime = millis();
					break;
				}
			}

			break;
		case STATE_TANK_RECHARGE:
			deltaTime = (unsigned short)((l_TimeNow - waitStartTime) / 1000);
			if (godsPleased)
				secsToGo = TANK_RECHARGE_TIME_SHORT - deltaTime;
			else
				secsToGo = TANK_RECHARGE_TIME - deltaTime;

			if (secsToGo == 0)
			{
				disp[0] = 10;
				godsSpeaking = false;
				godsPleased = false;
				currentState = STATE_IDLE;
			}
			DisplaySeconds(secsToGo);

			break;
		case STATE_ERROR:
			break;
#if 0
		switch(ErrorCode)
		{
		case 1:                                                      // Number too big or small
			disp[0] = 11;                                              // rAnGE
			disp[1] = 21;
			disp[2] = 23;
			disp[3] = 16;
			disp[4] = 12;
			disp[5] = 10;

			break;

		case 6:                                                      // Power ON Splash Spells out "CAL_1.1"
			disp[0] = 1;
			disp[1] = 1;
			disp[2] = 10;
			disp[3] = 15;
			disp[4] = 16;
			disp[5] = 18;
			break;

		case 3:                                                      // Brown-out = battery low
			disp[0] = 20;
			disp[1] = 15;
			disp[2] = 10;
			disp[3] = 19;
			disp[4] = 16;
			disp[5] = 17;
			break;

		case 5:                                                      // Reset Pin
			disp[0] = 19;
			disp[1] = 11;
			disp[2] = 5;
			disp[3] = 11;
			disp[4] = 12;
			disp[5] = 10;
			break;

		default:                                                      // Show other error number
			disp[0] = ErrorCode;
			disp[1] = 10;
			disp[2] = 10;
			disp[3] = 12;
			disp[4] = 12;
			disp[5] = 11;
			break;
		}
#endif
		break;

		default:                                                      // Should never get here
			ResetClearAll();
			ErrorFlag = true;
			ErrorCode = 4;
			break;

	}
}

void SendKeyEvent(int key, bool onOff, unsigned long timeStamp)
{
	unsigned long timeNow = millis();
	unsigned int deltaTime = (unsigned int) (timeNow - recStartTime);
	switch(key)
	{
		case KEY_1:
		case KEY_2:
		case KEY_3:
		case KEY_4:
		case KEY_5:
			if (currentState == STATE_RECORDING)
			{
				disp[KEY_5 - key] = onOff?key+1:10;
				StoreEvent(key, onOff, deltaTime);
			}
			break;
		case KEY_START:
			if (onOff && (currentState == STATE_IDLE))
			{
				currentState = STATE_RECORDING;
				recStartTime = millis();
				recIndex = 0;
			}
			break;
		case KEY_STOP:
			if (onOff && (currentState == STATE_RECORDING))
			{
				currentState = STATE_READY;
			}
			break;
		case KEY_PLAY:
			if (onOff && (currentState == STATE_READY))
			{
				DisplaySeconds(PLAY_WAIT_TIME);
				currentState = STATE_WAIT_PLAY;
				waitStartTime = millis();
			}
			break;
		case KEY_PANIC:
			if (onOff)
			{
				AllOff();
				if (currentState != STATE_TANK_RECHARGE)
					currentState = STATE_IDLE;
				recIndex = 0;
				playIndex = 0;
			}
			break;
		case KEY_SPARE_1:
			if (onOff)
			{
				AllOff();
				currentState = STATE_IDLE;
				recIndex = 0;
				playIndex = 0;
			}
			break;
		case KEY_SPARE_4:
			if (onOff && (currentState == STATE_IDLE))
			{
				recIndex = 0;
				godsSpeaking = true;
				godsPleased = true;

				// if the volcano gods are happy setup the
				// mystical volcano gods approval fire pattern
				StoreEvent(KEY_1, true, 100);
				StoreEvent(KEY_1, false, 200);

				StoreEvent(KEY_2, true, 200);
				StoreEvent(KEY_2, false, 300);

				StoreEvent(KEY_3, true, 300);
				StoreEvent(KEY_3, false, 400);

				StoreEvent(KEY_4, true, 400);
				StoreEvent(KEY_4, false, 500);

				StoreEvent(KEY_5, true, 500);
				StoreEvent(KEY_5, false, 600);

				StoreEvent(KEY_1, true, 700);
				StoreEvent(KEY_3, true, 700);
				StoreEvent(KEY_5, true, 700);

				StoreEvent(KEY_1, false, 1000);
				StoreEvent(KEY_3, false, 1000);
				StoreEvent(KEY_5, false, 1000);

				// tell the world about the gods approval
				DisplaySeconds(PLAY_WAIT_TIME_GODS);
				currentState = STATE_WAIT_PLAY;
				waitStartTime = millis();
				
			}
			break;
		case KEY_SPARE_8:
			if (onOff && (currentState == STATE_IDLE))
			{
				// the volcano gods are very displeased
				godsSpeaking = true;
				godsPleased = false;

				DisplaySeconds(PLAY_WAIT_TIME_GODS);
				currentState = STATE_WAIT_PLAY;
				waitStartTime = millis();
			}
			break;
	}
	disp[5] = currentState;
#ifdef DoSerial
	Serial.print("Key Event: ");
	Serial.print(key);
	Serial.print(onOff?" On  ":" Off ");
	Serial.print(currentState);
	Serial.print(" ");
	timeNow = millis();
	Serial.println(timeNow);
#endif
}

void UpdateDisplayCheck()
{

	digitalWrite(column+14, LOW);                              // Turn off current Digit
	PORTB = B11111111;

	column = (column + 1);
	if (column >= NUM_DIGITS)
		   column = 0;

	digitalWrite(column+14, HIGH);                             // Power-up Column (both for buttons and LEDs)
	delayMicroseconds(10);                                     // Let keymatrix settle down ?
	delayMicroseconds(600);                                   // Better debouncing ? (Dimmer display ?) 1000 is good


	rawKeys = 0;
	if(column < NUM_COLUMNS)
	{
		rawKeys = (PIND & B11110000) >>4;                        // Test here for button Press - Read keys port value
		decodeKeyPress();
	}
	SegmentDecode();
	//  delayMicroseconds(250);                                 // Better debouncing ? (Dimmer display ?)



	delayMicroseconds(800);                                      // For a brighter display ? 400 is good



	// Test for Key5 press
	key5Val = digitalRead(2);
	if (key5Val != lastKey5Val)
	{
		SendKeyEvent(KEY_5, key5Val, millis());
		lastKey5Val = key5Val; 
	}

}

void SegmentDecode()
{
	switch (disp[column])
	{
	case 0:
		PORTB =  B11000000;
		break;

	case 1:
		PORTB =  B11111001;
		break;

	case 2:
		PORTB =  B10100100;
		break;

	case 3:
		PORTB =  B10110000;
		break;

	case 4:
		PORTB =  B10011001;
		break;

	case 5:
		PORTB =  B10010010;
		break;

	case 6:
		PORTB =  B10000011;
		break;

	case 7:
		PORTB =  B11111000;
		break;

	case 8:
		PORTB =  B10000000;
		break;

	case 9:
		PORTB =  B10011000;
		break;

	case 10:                                                      // Blank space
		PORTB =  B11111111;
		break;

	case 11:                                                      // "E"
		PORTB =  B10000110;
		break;

	case 12:                                                      // "r"
//    PORTB =  B10101111;
		PORTB =  B11001110;
		break;

	case 13:                                                      // "S"
		PORTB =  B10010010;
		break;

	case 14:                                                      // "P"
		PORTB =  B10001100;
		break;

	case 15:                                                      // "L"
		PORTB =  B11000111;
		break;

	case 16:                                                      // "A"
		PORTB =  B10001000;
		break;

	case 17:                                                      // "b"
		PORTB =  B10000011;
		break;

	case 18:
		PORTB = B11000110;                                            // C
		break;

	case 19:
		PORTB = B10000111;                                            // "t"
		break;

	case 20:
		PORTB = B10100011;                                            // "o"
		break;

	case 21:
		//    PORTB = B10010000;                                      // "g"
		PORTB = B11000010;                                            // 'G"
		break;

	case 22:
		PORTB = B11101111;                                     // "i"
		break;

	case 23:
//       PORTB = B10101011;                                     // "n"
		PORTB = B11001000;                                      // N
		break;

	default:                                                      // Should never get here
		PORTB =  B11111111;
		ErrorFlag = true;
		ErrorCode = 4;
		break;
	}

}




void decodeKeyPress()
{
	byte keysChanged;
	// KeyPad Layout
	//
	// Columns      Rows
	//   3 2 1 0
	//
	//   7 8 9 /    0
	//   4 5 6 *    1
	//   1 2 3 -    2
	//   . 0 = +    3

	// Value mapping for decoding
	//     12  8  4  0
	//     13  9  5  1
	//     14  10 6  2
	//     15  11 7  3


		

	keysChanged = rawKeys ^ lastRawKeys[column];		// bits set are keys whose state has changed
	if (keysChanged)
	{
#ifdef DoSerial
#if 0
		Serial.print("column ");
		Serial.print(column,HEX);
		Serial.print(" rawKeys ");
		Serial.print(rawKeys,HEX);
		Serial.print(" lastRawKeys ");
		Serial.print(lastRawKeys[column],HEX);
		Serial.print(" keysChanged ");
		Serial.print(keysChanged,HEX);
		Serial.println(millis());
#endif
#endif
		for (i=0; i < NUM_ROWS; i++)                                                // This decodes the Rows
		{
			if(bitRead(keysChanged, i))
			{
				currentKey = keyDecode[i][column];
				SendKeyEvent(currentKey, bitRead(rawKeys,i), millis());
			}
		}
	}
	lastRawKeys[column] = rawKeys;
}







void BlinkFeedback()
{
	PORTB = B11111111;
	delay(100);
}


void BlankDisplay()
{
	PORTB = B11111111;
	for(i = 0; i < NUM_DIGITS; i ++)
	{
		disp[i] = 10;
	}
}


void ClearAllKeys()
{
	rawKeys = 0;
	for(i =0; i<NUM_COLUMNS; i++)
	{
		lastRawKeys[i] = 0;
	}
}


void ResetClearAll()
{
	ClearAllKeys();

	BlankDisplay();

	currentState = STATE_IDLE;
	disp[5] = currentState;
	tempINT = 0;
	column = 0;
	rawKeys = 0;
	ErrorFlag = false;
	ErrorCode = 0;
	currentKey = 0;
	godsSpeaking = false;
	godsPleased = false;


	MCUSR = 0;                                    // ATmega error flag bits
}

void StoreEvent(int key, bool onOff, unsigned int timeStamp)
{
	if (onOff)
	{
		lastKeyOn[key] = timeStamp;
	}
	else
	{

	   if ((timeStamp - lastKeyOn[key]) < MIN_VALVE_ON_TIME)
		   timeStamp = lastKeyOn[key] + MIN_VALVE_ON_TIME;
	   totalOnTime += (timeStamp - lastKeyOn[key]);
	   if (totalOnTime > GAS_DURATION_TIME)
			currentState = STATE_READY;
	}

	if (recIndex < MAX_EVENTS)
	{
		events[recIndex].key = key;
		events[recIndex].onOff = onOff;
		events[recIndex].timestamp = timeStamp;
	}
	++recIndex;
}

void PlayEvent(int key, bool onOff)
{
	disp[KEY_5 - key] = onOff?key+1:10;
#ifdef DoSerial
	Serial.print(key);
	Serial.print(" ");
	Serial.println(onOff?"On":"Off");
#else
	SetFET(valves[key], onOff);
#endif
}

void FatalError(byte val)
{
#ifdef DEBUG
	DebugSerial.print("Fatal ");
	DebugSerial.println(val);
#endif
	pinMode(13, OUTPUT);
	while(1)
	{
		digitalWrite(13, HIGH);
		delay(250);
		digitalWrite(13, LOW);
		delay(250);
	}
}

void SendCommand(CmdRsp_t* a_CmdRspOut)
{
	Serial.write(a_CmdRspOut->tag);
	Serial.write(a_CmdRspOut->command);
	Serial.write(a_CmdRspOut->arg);
}

void AllOff()
{
#ifndef DoSerial
	g_CmdRspOut.tag = TAG;
	g_CmdRspOut.command = cmd_AllOff;
	g_CmdRspOut.arg = 0;;
	SendCommand(&g_CmdRspOut);
#else
	Serial.println("All Off");
#endif
	for (i=0; i<NUM_FIRES; i++)
	{
		igniterState[i] = false;
#ifdef DoSerial
		Serial.print("Igniter ");
		Serial.print(i);
		Serial.println(" Off");
#endif
	}

	BlankDisplay();
}

void SetFET(byte l_FET, bool l_OnOff)
{
	g_CmdRspOut.tag = TAG;
	g_CmdRspOut.command = l_OnOff?cmd_FETOn:cmd_FETOff;
	g_CmdRspOut.arg = l_FET;
	SendCommand(&g_CmdRspOut);
#ifdef DEBUG
	DebugSerial.print("CMD ");
	DebugSerial.print(g_CmdRspOut.command);
	DebugSerial.print(" ");
	DebugSerial.println(g_CmdRspOut.arg);
#endif
}


void SendHeartBeat(void)
{
	g_CmdRspOut.tag = TAG;
	g_CmdRspOut.command = cmd_Heartbeat;
	g_CmdRspOut.arg = g_HeartBeat++;
	SendCommand(&g_CmdRspOut);
#ifdef DEBUG
	DebugSerial.print("HB ");
	DebugSerial.println(g_CmdRspOut.arg);
#endif
}

void DisplaySeconds(int secs)
{
	disp[0] = secs?(secs % 10):10;
	disp[1] = (secs >= 10)?(secs/10)%10:10;
	disp[2] = (secs >= 100)?(secs/100)%10:10;
}

void StartIgniters(unsigned long deltaTime)
{
	unsigned int igniter;

	igniter = deltaTime / IGNITER_STAGGER;
	if (igniter < NUM_FIRES)
	{
		if (!igniterState[igniter])
		{
#ifndef DoSerial
			SetFET(igniters[igniter], true);
#else
			Serial.print("Igniter ");
			Serial.print(igniter);
			Serial.println(" On");
#endif
			igniterState[igniter] = true;
		}
	}
	else if (igniter > 6)
	{
		// if the volcano gods are not happy then turn everything
		// off once all the igniters have been lit
		if (godsSpeaking && !godsPleased)
		{
			AllOff();
			godsSpeaking = false;
			currentState = STATE_IDLE;
			disp[0] = 10;
		}
	}
}

