
#define FETOn true
#define FETOff false

#define TAG 0xDB

typedef enum
{
	IGNITER_0,
	VALVE_0,
	IGNITER_1,
	VALVE_1,
	IGNITER_2,
	VALVE_2,
	IGNITER_3,
	VALVE_3,
	IGNITER_4,
	VALVE_4,
	IGNITER_5,
	VALVE_5,
	IGNITER_6,
	VALVE_6,
	IGNITER_7,
	VALVE_7
} oututFETS_t;


enum command
{
	cmd_Heartbeat,
	cmd_HeartbeatAck,
	cmd_FETOn,
	cmd_FETOff,
	cmd_AllOff,
	cmd_Max
};


typedef struct
{
	byte tag;
	byte command;
	byte arg;
} CmdRsp_t;
