
#include <avr/sleep.h>
#include <avr/power.h>
#include "../FireMosfet/FireControl.h"
#include <MemoryFree.h>


#define DoSerial false

#define NUM_DIGITS 6
#define NUM_COLUMNS 4
#define NUM_ROWS 4

void SendKeyEvent(int key, bool onOff, unsigned long timeStamp);
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
void SendHeartBeat(void);


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
	STATE_PLAYING,
	STATE_ERROR,
} state_t;

typedef struct
{
	byte key;
	bool onOff;
	unsigned int timestamp;
} event_t;



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

#if DoSerial
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

	// generate HeartBeat
	l_TimeNow = millis();
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

	UpdateDisplayCheck();                                    // Check for button press and Update Display LEDs

#if 0
	if(ErrorFlag)
	{
		currentState = STATE_ERROR;
	}
#endif

	switch (currentState)                                            // Calculator working statemachine
	{

		case STATE_IDLE:
			break;
		case STATE_RECORDING:
			break;
		case STATE_READY:
			break;
		case STATE_PLAYING:
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
	switch(key)
	{
		case KEY_1:
				disp[4] = onOff?1:10;
				SetFET(VALVE_4, onOff);
			break;
		case KEY_2:
				disp[3] = onOff?2:10;
				SetFET(VALVE_0, onOff);
			break;
		case KEY_3:
				disp[2] = onOff?3:10;
				SetFET(VALVE_1, onOff);
			break;
		case KEY_4:
				disp[1] = onOff?4:10;
				SetFET(VALVE_2, onOff);
			break;
		case KEY_5:
				disp[0] = onOff?5:10;
				SetFET(VALVE_3, onOff);
			break;
		case KEY_START:
				SetFET(IGNITER_0, onOff);
			break;
		case KEY_STOP:
				SetFET(IGNITER_1, onOff);
			break;
		case KEY_PLAY:
				SetFET(IGNITER_2, onOff);
			break;
		case KEY_PANIC:
				SetFET(IGNITER_3, onOff);
			break;
		case KEY_SPARE_4:
				SetFET(IGNITER_4, onOff);
			break;
	}
	disp[5] = currentState;
#if DoSerial
	unsigned long timeNow;
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

	column = column + 1;
	if(column > 7)                                 
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
#if DoSerial
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


	MCUSR = 0;                                    // ATmega error flag bits
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

