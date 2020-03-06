/*
  alarmdecoder

  Simple emulator for the AlarmDecoder device for the Friedland/SP1 433MHZ alarm.

  Understands the following on the serial interface at 115200 baud implement config, version, arm/disarm transmit
  and rx receive

  Setup Complete !Ready
  Version - send V\r get !VER:ffffffff,v1.00f,TX;RF
  Config -  send C\r get !CONFIG>ADDRESS=18&CONFIGBITS=ff00&LRR=N&EXP=NNNNN&REL=NNNN&MASK=ffffffff&DEDUPLICATE=N
  Received - !RFX:nnnnnnn:00 (could be 80 if this is loop 1 then 00 to clear)
  Arm - code and 1  
  Disarm - code and 2

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
*/


// Pin 13 has an LED connected on most Arduino boards.
// give it a name:
int led = 13;
// This is the port for the transfer of data
int rftx = 2;
String VERSION = "V";
String CONFIG = "C";
unsigned int armdiscode = 0;
int versionstate = 0;
int configstate = 0;
int armcode = + B00001;
int disarmcode = B00011;
int codelen = 13;
int cr;

// Receive code logic
int rfrx = 3;     // Receive pin number
#define NEDGES 25 // Number of edges in any of the sequences that we care about
#define AMOVE 9000L  // Longer than this is a sensor trigger
#define AREMOTE 10500L // Longer than this is a remote control command
#define AREPEAT 11500L // Longer than this is a disarm repeat sequence
#define ARESET 13000L // Maximum length of any guard we care about
#define GLITCH 150L  // Anything less than this is a glitch - ignore
#define PSHORTS 200L // Shortest short
#define PSHORTL 450L // Longest Short
#define PLONGS 550L  // Shortest long
#define PLONGL 800L  // Longest long
#define PLEN 1000L  // overall bit length (2 pulses apart from the hidden first)
#define RSIZE NEDGES * 3  // ring buffer size that is used to store the pulses can in theory store 3 sequences
#define CODETIMEOUT 10000L // number of milliseconds before codes receipt is not suppressed - 10 seconds
unsigned long pmicro = 0L, pglitch = 0L;
unsigned long edges[RSIZE];
unsigned long mtime = 0L, pmtime = 0L, code = 0L, pcode = 0L;
int pos = 0, ppos = 0, pguard = 0;

/* Emulate the version response - very few capabilities as these are all send and RF */

void send_version() {
  Serial.println("!VER:ffffffff,v1.0.0,TX;RF");
}

/* Emulate the configuration response - all hard coded */

void send_config() {
  Serial.println("!CONFIG>ADDRESS=18&CONFIGBITS=ff00&LRR=N&EXP=NNNNN&REL=NNNN&MASK=ffffffff&DEDUPLICATE=N");
}

/* Return a state based upon the characters read from the serial and the
   character. The state is a number of the character matched to date, 
   -1 if matched or -2 if no match until a character less than 32 (space)
 */
 
int matstr(int b, int state, String mat) {
    int ret = 0;
    ret = state;
    // Serial.print("In matstr with input char ");
    // Serial.print(b);
    // Serial.print(" and in state ");
    // Serial.print(state);
    // Serial.print(" for string ");
    // Serial.print(mat);
    // Serial.print(" and out state ");
    
    if((ret == mat.length())&&(b <= 32)) {
        // Serial.println("MATCH");
        return -1;
    }
    if(b <= 32) {
        // Serial.println("0");
        return 0;
    }
    if(ret >= mat.length()) {
        // Serial.println("NOMATCH");
        return -2;
    }
    if(b != mat[ret]) {
        // Serial.println("NOMATCH");
        return -2;
    }
    ret ++;
    // Serial.println(ret);
    return ret;
}

/* Match a number sequence of up to four digits some of which is in the state already
   Then if the sequence is broken then set the return to zero.
   Use an unsigned int for the value where the first digit (10,000s) is the number of digits decoded. */

unsigned int matnum(int b, int state) {
    unsigned int ret = 0, lth = 0, code = 0;
    lth = state / 10000;
    code = state % 10000;
    if((b >= '0') && (b <= '9')) {
	lth += 1;
        if(lth > 4) {
             lth = 4;
        }
        code = (code * 10 + b - '0') % 10000;
    } else {
      code = 0;
      lth = 0;
    }
    ret = code + lth * 10000;
    return ret;
}


/* Transmit a value over an IR pin with a guard, short and long pulse lengths of a length of bits 
 */

void tx(int pin, int guard, int sh, int lo, int val, int bits) {
    int rev = 0; // reverse the input for transmission
    int nval = 0;
    int bt = 0;
    int pr = (sh + lo)/2;
    int srev = 0;
    // reverse the bit order for easier processing
    nval = val;
    noInterrupts();
    // Serial.print("val: ");
    // Serial.println(nval);
    for(int i = 0; i < bits; i++) {
        srev = srev << 1;
        srev |= (nval & 1);
        nval = nval >> 1;
    } 
    // Serial.print("srev: ");
    // Serial.println(srev);
    // send 12x
    digitalWrite(led, HIGH);
    for(int j = 0; j < 12; j ++) {
        rev = srev;
        // Guard
        digitalWrite(pin, LOW);
        delayMicroseconds(guard);
        for(int i = 0;i < bits; i++) {
            bt = rev & 1;
            rev = rev >> 1;
            if(bt == 1) {
                digitalWrite(pin, LOW);
                delayMicroseconds(lo);
                digitalWrite(pin, HIGH);
                delayMicroseconds(sh);
            } else {
                digitalWrite(pin, LOW);
                delayMicroseconds(sh);
                digitalWrite(pin, HIGH);
                delayMicroseconds(lo);
            }
        }
    }
    digitalWrite(pin, HIGH);
    delayMicroseconds(guard);
    digitalWrite(pin, LOW);
    digitalWrite(led, LOW);
    interrupts();
}

void edgefound() {
  // if the time is greater than the glitch window write the output
  unsigned long cmicro;
  cmicro = micros();
  if(cmicro - pglitch > GLITCH) {
    edges[pos] = cmicro - pmicro;
    pmicro = cmicro;
    pos ++;
    if(pos >= RSIZE) {
      pos = 0;
    }
  }
  pglitch = cmicro;  
}

unsigned long decpos() {
  unsigned long ret = 0L; // a 6 digit number to emulate the alarm decoder sensor number including a 1 digit sensor type, 3 digits house and 2 sensor
  int cp, p;
  int house = 0, sensor = 0, pcnt = 0;
  unsigned long gl = 0L;
  int plstate = 0; // state for the pulse read - 0 - at guard, 1 - first pulse (the rest is lost in the guard), 2 - remaining pulses

  // if no pulses received then return
  if(pos == ppos) {
    return ret;
  }
  cp = pos;
  ppos = pos; // clear for the next pulse

  // see if we can find guard pulse
  while(pguard != cp) {
    if((edges[pguard] >= AMOVE)&&(edges[pguard] <= ARESET)) {
      break;
    }
    pguard ++;
    if(pguard >= RSIZE) {
      pguard -= RSIZE;
    }
  }
  
  // deal with the ring to work out how long the buffer is
  p = cp;
  if(p < pguard) {
    p += RSIZE;
  }
  
  // needs to be at least the number of edges and the next guard in the buffer
  if(p - pguard < NEDGES + 2) {
    return ret;
  }

  // OK so is there a guard at the next point?
  p = pguard + NEDGES + 1;
  if(p >= RSIZE) {
    p -= RSIZE;
  }
  // guard found
  if(edges[p] >= AMOVE) {
    plstate = 0;
    gl = 0L;
    house = 0;
    sensor = 0;
    pcnt = 0;
    while(pguard != p) {
      if(plstate == 2) {
        if(pcnt % 2 == 1) {
          if(pcnt < 16) {
            house = house << 1;
            if(edges[pguard] <= PSHORTL) {
              house |= 1;
            }
          } else {
            sensor = sensor << 1;
            if(edges[pguard] <= PSHORTL) {
              sensor |= 1;
            }
          }
        }
      }
      if(plstate == 1) {
        if(edges[pguard] <= PSHORTL) {
          house = 1;
        }
        gl -= PLEN - edges[pguard];
        plstate = 2;
      }
      if(plstate == 0) {
        gl = edges[pguard];
        plstate = 1;
      }
      pcnt ++;
      // Serial.print(edges[pguard]);
      // Serial.print(" ");
      pguard ++;
      if(pguard >= RSIZE) {
        pguard -= RSIZE;
      }
    }
    ret = 300000L; // rePeat
    if(gl < AREMOTE) {
      ret = 100000L; // Move
    } else if(gl <= AREPEAT) {
      ret = 200000L; // Remote
    }
    ret += house * 100L + sensor;
  } else {
    pguard ++;
    if(pguard >= RSIZE) {
      pguard -= RSIZE;
    }
  }
  return ret;
}

/* Emulate the RF trigger of a sensor,
 *
 * Takes the sensor id and a boolean for the start and end
 */

void send_rfx(unsigned long id, bool start)
{
  Serial.print("!RFX:0");
  Serial.print(id);
  if(start) {
    Serial.println(",80");
  } else {
    Serial.println(",00");
  }
}

// the setup routine runs once when you press reset:
void setup() {
  // initialize the digital pin as an output.
  pinMode(led, OUTPUT);
  pinMode(rftx, OUTPUT);
  pinMode(rfrx, INPUT);
  Serial.begin(115200);
  attachInterrupt(digitalPinToInterrupt(rfrx), edgefound, CHANGE);
  Serial.println("!Ready");
}

// the loop routine runs over and over again forever:
void loop() {
    if(Serial.available() > 0) {
        cr = Serial.read();
        armdiscode = matnum(cr, armdiscode);
        versionstate = matstr(cr, versionstate, VERSION);
        configstate = matstr(cr, configstate, CONFIG);
    }
    // match for 4 numbers found
    if(armdiscode > 40000) {
	if(armdiscode % 10 == 2) {
	    tx(rftx, 10700, 325, 650, ((armdiscode % 10000)/10) * 32 + armcode, codelen);
	    // Serial.println("Armed");
	    // digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
	}
	if(armdiscode % 10 == 1) {
	    tx(rftx, 10700, 325, 650, ((armdiscode % 10000)/10) * 32 + disarmcode, codelen);
	    // Serial.println("Disarmed");
	    // digitalWrite(led, LOW);   // turn the LED off (LOW is the voltage level)
        }
        armdiscode = 0;
    }
    if(versionstate == -1) {
      send_version();
      versionstate = 0;
    }
    if(configstate == -1) {
      send_config();
      configstate = 0;
    }
    code = decpos();
    mtime = millis(); // get current time

    // this checks to see if a code is recieved and if it is a leading edge, repeat or change
    if(code > 0L) {
      // change of code mid stream
      if((pcode != 0L)&&(pcode != code))  {
        send_rfx(pcode, false);
        pcode = 0L;
        pmtime = mtime;
      }
      // start of a new stream of pulses
      if(pcode == 0L) {
        send_rfx(code, true);
        pcode = code;
        pmtime = mtime;
      }
    }
    // reset pulse if code not seen for the timeout and deal with time roll over too
    if((pcode != 0L)&&(mtime - pmtime > CODETIMEOUT)) {
       send_rfx(pcode, false);
       pcode = 0L;
    }
}
