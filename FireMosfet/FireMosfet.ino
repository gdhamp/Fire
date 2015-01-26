// simple program to run a wifire16 v1 board off a MiniSSC relay controller

#include <SPI.h>
#include "FireControl.h"

// pin definitions
#define CLEARPIN 4    // master clear for 74HC595 shift registers
#define LATCHPIN 5    // latch for 74HC595 shift registers
#define OEPIN    6    // output enable for 74HC595 shift registers
#define ARMEDPIN 7    // optoisolator connected to load power
#define DATAPIN  11   // data for 74HC595 shift registers
#define CLOCKPIN 13   // clock for 74HC595 shift registers 

#define bitFlip(x,n)  bitRead(x,n) ? bitClear(x,n) : bitSet(x,n)

void SendCommand(CmdRsp_t* a_CmdRspOut);
void doSPITransfer(byte bank0, byte bank1);
void allOff(void);
void FatalError(byte thing);

byte g_Bank0 = 0, g_Bank1 = 0;
CmdRsp_t g_CmdRspIn;
CmdRsp_t g_CmdRspOut;


bool g_HeartBeatSeen;
unsigned long g_HeartBeatTime;

// setup
void setup()
{
  
	// set all output pins
	SPI.begin(); // handles DATAPIN and CLOCKPIN
	pinMode(LATCHPIN, OUTPUT);
	pinMode(OEPIN, OUTPUT);
	pinMode(CLEARPIN, OUTPUT);

	// make sure no lines go active until data is shifted out
	digitalWrite(CLEARPIN, HIGH);
	digitalWrite(OEPIN, LOW);

	// Clear HeartBeat Flag
	g_HeartBeatSeen = false;

	// clear any lines that were left active
	allOff();
  
	// activate built-in pull-up resistor 
	digitalWrite(ARMEDPIN, HIGH);

	// start the serial communication with the xbee
	Serial.begin(9600);
}


// main loop
void loop()
{
	unsigned long l_TimeNow;


	if (g_HeartBeatSeen)
	{
		l_TimeNow = millis();
		if ((l_TimeNow - g_HeartBeatTime) > 1000)
		{
			allOff();
			g_HeartBeatSeen = false;
			g_HeartBeatTime = l_TimeNow;
		}
	}

	if(Serial.available() >= 3)
	{
		byte c; 
		
		c = Serial.read();
		if (!g_HeartBeatSeen)
		{
			if (c != TAG)
				goto NotSync;
		}

		g_CmdRspIn.tag = c;
		g_CmdRspIn.command = Serial.read();
		g_CmdRspIn.arg = Serial.read();

		//Validity Checking
		if (g_CmdRspIn.tag != TAG)
			FatalError(0x01);

		if (g_CmdRspIn.command >= cmd_Max)
			FatalError(g_CmdRspIn.command);

		switch (g_CmdRspIn.command)
		{
			case cmd_Heartbeat:
				g_CmdRspOut.tag = TAG;
				g_CmdRspOut.command = cmd_HeartbeatAck;
				g_CmdRspOut.arg = g_CmdRspIn.arg;
				SendCommand(&g_CmdRspOut);
				g_HeartBeatSeen = true;
				g_HeartBeatTime = millis();
				break;

			case cmd_FETOn:
				if (!g_HeartBeatSeen)
					break;
				if (g_CmdRspIn.arg > 15)
					FatalError(0x04);
				if (g_CmdRspIn.arg < 8)
					bitSet(g_Bank0, g_CmdRspIn.arg);
				else
					bitSet(g_Bank1, g_CmdRspIn.arg - 8);

				doSPITransfer(g_Bank0, g_Bank1);
				break;

			case cmd_FETOff:
				if (!g_HeartBeatSeen)
					break;

				if (g_CmdRspIn.arg > 15)
					FatalError(0x08);
				if (g_CmdRspIn.arg < 8)
					bitClear(g_Bank0, g_CmdRspIn.arg);
				else
					bitClear(g_Bank1, g_CmdRspIn.arg - 8);

				doSPITransfer(g_Bank0, g_Bank1);
				break;

			case cmd_AllOff:
				allOff();
				break;


			// both of these should not be seen
			// so stop everything
			case cmd_HeartbeatAck:
			default:
				FatalError(0x10);
		}
	}
NotSync:
	;
}

void FatalError(byte thing)
{
//	allOff();
//	PlayTone();
	while(1)
	{
		doSPITransfer(0, 0);
		delay(250);
		doSPITransfer(0, thing);
		delay(250);
	}
}

void SendCommand(CmdRsp_t* a_CmdRspOut)
{
	Serial.write(a_CmdRspOut->tag);
	Serial.write(a_CmdRspOut->command);
	Serial.write(a_CmdRspOut->arg);
}
void doSPITransfer(byte a_Bank0, byte a_Bank1)
{
	digitalWrite(LATCHPIN, LOW);
   	digitalWrite(OEPIN, HIGH);
   	SPI.transfer(a_Bank1);
   	SPI.transfer(a_Bank0);
   	digitalWrite(LATCHPIN, HIGH);
   	digitalWrite(OEPIN, LOW);
}

void allOff(void)
{
	g_Bank0 = 0;
	g_Bank1 = 0;
	doSPITransfer(g_Bank0, g_Bank1);
}
