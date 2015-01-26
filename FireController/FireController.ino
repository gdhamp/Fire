#include <SoftwareSerial.h>
#include "../FireMosfet/FireControl.h"


void FatalError(byte val);
void SendCommand(CmdRsp_t* a_CmdRspOut);
void SetFET(byte l_FET, bool l_OnOff);
void SendHeartBeat(void);

int mosfet = 0;

CmdRsp_t g_CmdRspIn;
CmdRsp_t g_CmdRspOut;

bool g_HeartBeatAckSeen;
unsigned long g_HeartBeatAckTime;
unsigned long g_HeartBeatTime;
unsigned long g_FETTime;
byte g_HeartBeat;

//#define DEBUG
#undef DEBUG

#ifndef DEBUG
HardwareSerial xbSerial = Serial;
#else
SoftwareSerial xbSerial = SoftwareSerial(2,3);
HardwareSerial DebugSerial = Serial;
#endif

byte g_FETCounter;

// setup
void setup()
{
	// init this to say we are starting
	g_HeartBeatAckSeen = false;
	g_HeartBeatTime = millis();
	g_HeartBeatAckTime = millis();

	g_FETCounter = 0;

	// start the serial communication with the xbee
#ifndef DEBUG
	xbSerial.begin(9600);
#else
	xbSerial.begin(9600);
	DebugSerial.begin(38400);
#endif
}


// main loop
void loop()
{
	unsigned long l_TimeNow;
	byte l_FET;
	bool l_OnOff;

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
		g_FETCounter = 0;
	}


	if(xbSerial.available() >= 3)
	{
		byte c;
	
		c = xbSerial.read();
		if (!g_HeartBeatAckSeen)
		{
			if (c != TAG)
				goto NotSync;
		}
		g_CmdRspIn.tag = c;
		g_CmdRspIn.command = xbSerial.read();
		g_CmdRspIn.arg = xbSerial.read();
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
	l_TimeNow = millis();
#define FETDuration 200
	if (g_HeartBeatAckSeen)
	{
		if ((l_TimeNow - g_FETTime) > FETDuration)
		{
			g_FETTime = l_TimeNow - (l_TimeNow % FETDuration);
#ifdef DEBUG
			DebugSerial.println(g_FETTime);
#endif
			l_FET = g_FETCounter % 16;
			l_OnOff = ((g_FETCounter / 16) % 2) == 0;
			SetFET(l_FET, l_OnOff);
			++g_FETCounter;
		}
	}
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
	xbSerial.write(a_CmdRspOut->tag);
	xbSerial.write(a_CmdRspOut->command);
	xbSerial.write(a_CmdRspOut->arg);
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
