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

int igniterPin=1;
int valve0Pin=3;
int valve1Pin=4;
int valve2Pin=5;
int valve3Pin=6;
byte g_FETCounter;

int igniterState = 0;
int valve0State = 0;
int valve1State = 0;
int valve2State = 0;
int valve3State = 0;

// setup
void setup()
{
	// init this to say we are starting
	g_HeartBeatAckSeen = false;
	g_HeartBeatTime = millis();
	g_HeartBeatAckTime = millis();

	g_FETCounter = 0;

	pinMode(igniterPin, INPUT_PULLUP);
	pinMode(valve0Pin, INPUT_PULLUP);
	pinMode(valve1Pin, INPUT_PULLUP);
	pinMode(valve2Pin, INPUT_PULLUP);
	pinMode(valve3Pin, INPUT_PULLUP);

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
	int val;

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
	val = analogRead(igniterPin);
	if (val < 300)
	{
		if (igniterState != 0)
		{
			igniterState = 0;
			SetFET(8, 0);
			SetFET(10, 0);
			SetFET(12, 0);
			SetFET(14, 0);
		}
	}
	else if (val > 723)
	{
		if (igniterState == 0)
		{
			igniterState = 1;
			SetFET(8, 1);
			SetFET(10, 1);
			SetFET(12, 1);
			SetFET(14, 1);
		}
	}

	val = digitalRead(valve0Pin);
	if (val != valve0State)
	{
		valve0State = val;
		SetFET(1, valve0State?0:1);
	}

	val = digitalRead(valve1Pin);
	if (val != valve1State)
	{
		valve1State = val;
		SetFET(3, valve1State?0:1);
	}

	val = digitalRead(valve2Pin);
	if (val != valve2State)
	{
		valve2State = val;
		SetFET(5, valve2State?0:1);
	}

	val = digitalRead(valve3Pin);
	if (val != valve3State)
	{
		valve3State = val;
		SetFET(7, valve3State?0:1);
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
