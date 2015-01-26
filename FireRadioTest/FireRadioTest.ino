#include <SoftwareSerial.h>
#define rxPin 2
#define txPin 3
#define ledPin 13


SoftwareSerial xbee =  SoftwareSerial(rxPin, txPin);
char c;
int ledStatus;


void setup()  {
        pinMode(rxPin, INPUT);
        pinMode(txPin, OUTPUT);

	pinMode(ledPin,OUTPUT);
	digitalWrite(ledPin,HIGH);
	ledStatus=1;
	Serial.begin(38400);
	Serial.println( "Arduino started sending bytes via XBee" );

	// set the data rate for the SoftwareSerial port
#if 0
	xbee.begin( 38400 );
#else
	xbee.begin( 9600 );
#endif
}

void loop()  {
	if (Serial.available())
	{
		c = Serial.read();
		xbee.write(c);
Serial.write(c);
		ledStatus = ledStatus?0:1;
		digitalWrite(ledPin, ledStatus);
	}

	if (xbee.available())
	{
		c = xbee.read();
		Serial.write(c);
	}
}
