# alarmdecoder

Simple emulator for the AlarmDecoder device for the Friedland/SP1 433MHZ alarm.

Understands the following on the serial interface at 115200 baud implement config, version, arm/disarm transmit
and rx receive

- Setup Complete !Ready
- Version - send V\r get !VER:ffffffff,v1.00f,TX;RF
- Config -  send C\r get !CONFIG>ADDRESS=18&CONFIGBITS=ff00&LRR=N&EXP=NNNNN&REL=NNNN&MASK=ffffffff&DEDUPLICATE=N
- Received - !RFX:nnnnnnn:00 (could be 80 if this is loop 1 then 00 to clear)
- Arm - code and 1  
- Disarm - code and 2

This implements enough of an API interface to work with the alarmdecoder python wrapper and send signals to
homeassistant.

In the SP1 IR sensors they transmit a 13 pulse code measured by edges - the length of the guard pulse determines
whether this is a sensor or a button press on the remote or a repeat code which the alarm unit sends when disarmed
I have not looked at the codes sent when the alarm is triggered.

Movement, button presses and disarm repeats are just sent back as !RFX codes literally as detected. The format of
the code is as follows:

- A hard coded 0
- One of: 1 - A movement sensor, 2 - the remote code, 3 - disarm repeat
- Three numbers containing the decimal value of the house code
- Two numbers containing the sensor number or the commamd (the last digit of this might be a check digit - 
not worked this out) for the remote or repeats 01 arms, 11 disarms.

RFX codes are rate limited with the trigger setting Zone 1 and being cleared after 10 seconds of the same code being repeated
Typically a movement or button press creates around 16 code transmissions one after the other.

The code has been deployed on an Arduino Nano - using standard 433 transmitters and receivers the receiver running at 3.3V.
