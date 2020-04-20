/**
 * The CAT protocol is used by many radios to provide remote control to comptuers through
 * the serial port.
 * 
 * This is very much a work in progress. Parts of this code have been liberally
 * borrowed from other GPLicensed works like hamlib.
 * 
 * WARNING : This is an unstable version and it has worked with fldigi, 
 * it gives time out error with WSJTX 1.8.0  
 */


/** 
 *  The second set of 16 pins on the Raduino's bottom connector are have the three clock outputs and the digital lines to control the rig.
 *  This assignment is as follows :
 *    Pin   1   2    3    4    5    6    7    8    9    10   11   12   13   14   15   16
 *         GND +5V CLK0  GND  GND  CLK1 GND  GND  CLK2  GND  D2   D3   D4   D5   D6   D7  
 *  These too are flexible with what you may do with them, for the Raduino, we use them to :
 *  - TX_RX line : Switches between Transmit and Receive after sensing the PTT or the morse keyer
 *  - CW_KEY line : turns on the carrier for CW
 */

#define TX_RX (7)
#define CW_TONE (6)
#define TX_LPF_A (5)
#define TX_LPF_B (4)
#define TX_LPF_C (3)
#define CW_KEY (2)

/**
 * These are the indices where these user changable settinngs are stored  in the EEPROM
 */
#define MASTER_CAL 0
#define LSB_CAL 4
#define USB_CAL 8
#define SIDE_TONE 12
//these are ids of the vfos as well as their offset into the eeprom storage, don't change these 'magic' values
#define VFO_A 16
#define VFO_B 20
#define CW_SIDETONE 24
#define CW_SPEED 28

//These are defines for the new features back-ported from KD8CEC's software
//these start from beyond 256 as Ian, KD8CEC has kept the first 256 bytes free for the base version
#define VFO_A_MODE  256 // 2: LSB, 3: USB
#define VFO_B_MODE  257

//values that are stroed for the VFO modes
#define VFO_MODE_LSB 2
#define VFO_MODE_USB 3

// handkey, iambic a, iambic b : 0,1,2f
#define CW_KEY_TYPE 358




//these are the two default USB and LSB frequencies. The best frequencies depend upon your individual taste and filter shape
#define INIT_USB_FREQ   (11996500l)
// limits the tuning and working range of the ubitx between 3 MHz and 30 MHz
#define LOWEST_FREQ   (100000l)
#define HIGHEST_FREQ (30000000l)

//we directly generate the CW by programmin the Si5351 to the cw tx frequency, hence, both are different modes
//these are the parameter passed to startTx
#define TX_SSB 0
#define TX_CW 1

char ritOn = 0;
char vfoActive = VFO_A;
int8_t meter_reading = 0; // a -1 on meter makes it invisible
unsigned long vfoA=7150000L, vfoB=14200000L, sideTone=800, usbCarrier;
char isUsbVfoA=0, isUsbVfoB=1;
unsigned long ritRxFrequency, ritTxFrequency;  //frequency is the current frequency on the dial
extern uint32_t  r820t_freq;

uint32_t frequency = r820t_freq;

unsigned long firstIF =   45000000L;

//these are variables that control the keyer behaviour
int cwSpeed = 100; //this is actuall the dot period in milliseconds
extern int32_t calibration;
byte cwDelayTime = 60;
bool Iambic_Key = true;
#define IAMBICB 0x10 // 0 for Iambic A, 1 for Iambic B
unsigned char keyerControl = IAMBICB;


/**
 * Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
 */
boolean txCAT = false;        //turned on if the transmitting due to a CAT command
char inTx = 0;                //it is set to 1 if in transmit mode (whatever the reason : cw, ptt or cat)
char splitOn = 0;             //working split, uses VFO B as the transmit frequency, (NOT IMPLEMENTED YET)
char keyDown = 0;             //in cw mode, denotes the carrier is being transmitted
char isUSB = 0;               //upper sideband was selected, this is reset to the default for the 
                              //frequency when it crosses the frequency border of 10 MHz
byte menuOn = 0;              //set to 1 when the menu is being displayed, if a menu item sets it to zero, the menu is exited
unsigned long cwTimeout = 0;  //milliseconds to go before the cw transmit line is released and the radio goes back to rx mode
unsigned long dbgCount = 0;   //not used now
unsigned char txFilter = 0;   //which of the four transmit filters are in use
boolean modeCalibrate = false;//this mode of menus shows extended menus to calibrate the oscillators and choose the proper
       
static unsigned long rxBufferArriveTime = 0;
static byte rxBufferCheckCount = 0;
#define CAT_RECEIVE_TIMEOUT 100
static byte cat[5]; 
static byte insideCat = 0; 

//for broken protocol
#define CAT_RECEIVE_TIMEOUT 100

#define CAT_MODE_LSB            0x00
#define CAT_MODE_USB            0x01
#define CAT_MODE_CW             0x02
#define CAT_MODE_CWR            0x03
#define CAT_MODE_AM             0x04
#define CAT_MODE_FM             0x08
#define CAT_MODE_DIG            0x0A
#define CAT_MODE_PKT            0x0C
#define CAT_MODE_FMN            0x88

#define ACK 0

unsigned int skipTimeCount = 0;

byte setHighNibble(byte b,byte v) {
  // Clear the high nibble
  b &= 0x0f;
  // Set the high nibble
  return b | ((v & 0x0f) << 4);
}

byte setLowNibble(byte b,byte v) {
  // Clear the low nibble
  b &= 0xf0;
  // Set the low nibble
  return b | (v & 0x0f);
}

byte getHighNibble(byte b) {
  return (b >> 4) & 0x0f;
}

byte getLowNibble(byte b) {
  return b & 0x0f;
}

// Takes a number and produces the requested number of decimal digits, staring
// from the least significant digit.  
//
void getDecimalDigits(unsigned long number,byte* result,int digits) {
  for (int i = 0; i < digits; i++) {
    // "Mask off" (in a decimal sense) the LSD and return it
    result[i] = number % 10;
    // "Shift right" (in a decimal sense)
    number /= 10;
  }
}

// Takes a frequency and writes it into the CAT command buffer in BCD form.
//
void writeFreq(unsigned long freq,byte* cmd) {
  // Convert the frequency to a set of decimal digits. We are taking 9 digits
  // so that we can get up to 999 MHz. But the protocol doesn't care about the
  // LSD (1's place), so we ignore that digit.
  byte digits[9];
  getDecimalDigits(freq,digits,9);
  // Start from the LSB and get each nibble 
  cmd[3] = setLowNibble(cmd[3],digits[1]);
  cmd[3] = setHighNibble(cmd[3],digits[2]);
  cmd[2] = setLowNibble(cmd[2],digits[3]);
  cmd[2] = setHighNibble(cmd[2],digits[4]);
  cmd[1] = setLowNibble(cmd[1],digits[5]);
  cmd[1] = setHighNibble(cmd[1],digits[6]);
  cmd[0] = setLowNibble(cmd[0],digits[7]);
  cmd[0] = setHighNibble(cmd[0],digits[8]);  
}

// This function takes a frquency that is encoded using 4 bytes of BCD
// representation and turns it into an long measured in Hz.
//
// [12][34][56][78] = 123.45678? Mhz
//
unsigned long readFreq(byte* cmd) {
    // Pull off each of the digits
    byte d7 = getHighNibble(cmd[0]);
    byte d6 = getLowNibble(cmd[0]);
    byte d5 = getHighNibble(cmd[1]);
    byte d4 = getLowNibble(cmd[1]); 
    byte d3 = getHighNibble(cmd[2]);
    byte d2 = getLowNibble(cmd[2]); 
    byte d1 = getHighNibble(cmd[3]);
    byte d0 = getLowNibble(cmd[3]); 
    return  
      (unsigned long)d7 * 100000000L +
      (unsigned long)d6 * 10000000L +
      (unsigned long)d5 * 1000000L + 
      (unsigned long)d4 * 100000L + 
      (unsigned long)d3 * 10000L + 
      (unsigned long)d2 * 1000L + 
      (unsigned long)d1 * 100L + 
      (unsigned long)d0 * 10L; 
}

//void ReadEEPRom_FT817(byte fromType)
void catReadEEPRom(void)
{
  //for remove warnings
  byte temp0 = cat[0];
  byte temp1 = cat[1];
/*
  itoa((int) cat[0], b, 16);
  strcat(b, ":");
  itoa((int) cat[1], c, 16);
  strcat(b, c);
  printLine2(b);
*/

  cat[0] = 0;
  cat[1] = 0;
  //for remove warnings[1] = 0;
  
  switch (temp1)
  {
    case 0x45 : //
      if (temp0 == 0x03)
      {
        cat[0] = 0x00;
        cat[1] = 0xD0;
      }
      break;
    case 0x47 : //
      if (temp0 == 0x03)
      {
        cat[0] = 0xDC;
        cat[1] = 0xE0;
      }
      break;
    case 0x55 :
      //0 : VFO A/B  0 = VFO-A, 1 = VFO-B
      //1 : MTQMB Select  0 = (Not MTQMB), 1 = MTQMB ("Memory Tune Quick Memory Bank")
      //2 : QMB Select  0 = (Not QMB), 1 = QMB  ("Quick Memory Bank")
      //3 :
      //4 : Home Select  0 = (Not HOME), 1 = HOME memory
      //5 : Memory/MTUNE select  0 = Memory, 1 = MTUNE
      //6 :
      //7 : MEM/VFO Select  0 = Memory, 1 = VFO (A or B - see bit 0)
      cat[0] = 0x80 + (vfoActive == VFO_B ? 1 : 0);
      cat[1] = 0x00;
      break;
    case 0x57 : //
      //0 : 1-0  AGC Mode  00 = Auto, 01 = Fast, 10 = Slow, 11 = Off
      //2  DSP On/Off  0 = Off, 1 = On  (Display format)
      //4  PBT On/Off  0 = Off, 1 = On  (Passband Tuning)
      //5  NB On/Off 0 = Off, 1 = On  (Noise Blanker)
      //6  Lock On/Off 0 = Off, 1 = On  (Dial Lock)
      //7  FST (Fast Tuning) On/Off  0 = Off, 1 = On  (Fast tuning)

      cat[0] = 0xC0;
      cat[1] = 0x40;
      break;
    case 0x59 : //  band select VFO A Band Select  0000 = 160 M, 0001 = 75 M, 0010 = 40 M, 0011 = 30 M, 0100 = 20 M, 0101 = 17 M, 0110 = 15 M, 0111 = 12 M, 1000 = 10 M, 1001 = 6 M, 1010 = FM BCB, 1011 = Air, 1100 = 2 M, 1101 = UHF, 1110 = (Phantom)
      //http://www.ka7oei.com/ft817_memmap.html
      //CAT_BUFF[0] = 0xC2;
      //CAT_BUFF[1] = 0x82;
      break;
    case 0x5C : //Beep Volume (0-100) (#13)
      cat[0] = 0xB2;
      cat[1] = 0x42;
      break;
    case 0x5E :
      //3-0 : CW Pitch (300-1000 Hz) (#20)  From 0 to E (HEX) with 0 = 300 Hz and each step representing 50 Hz
      //5-4 :  Lock Mode (#32) 00 = Dial, 01 = Freq, 10 = Panel
      //7-6 :  Op Filter (#38) 00 = Off, 01 = SSB, 10 = CW
      //CAT_BUFF[0] = 0x08;
      cat[0] = (sideTone - 300)/50;
      cat[1] = 0x25;
      break;
    case 0x61 : //Sidetone (Volume) (#44)
      cat[0] = sideTone % 50;
      cat[1] = 0x08;
      break;
    case  0x5F : //
      //4-0  CW Weight (1.:2.5-1:4.5) (#22)  From 0 to 14 (HEX) with 0 = 1:2.5, incrementing in 0.1 weight steps
      //5  420 ARS (#2)  0 = Off, 1 = On
      //6  144 ARS (#1)  0 = Off, 1 = On
      //7  Sql/RF-G (#45)  0 = Off, 1 = On
      cat[0] = 0x32;
      cat[1] = 0x08;
      break;
    case 0x60 : //CW Delay (10-2500 ms) (#17)  From 1 to 250 (decimal) with each step representing 10 ms
      cat[0] = cwDelayTime;
      cat[1] = 0x32;
      break;
    case 0x62 : //
      //5-0  CW Speed (4-60 WPM) (#21) From 0 to 38 (HEX) with 0 = 4 WPM and 38 = 60 WPM (1 WPM steps)
      //7-6  Batt-Chg (6/8/10 Hours (#11)  00 = 6 Hours, 01 = 8 Hours, 10 = 10 Hours
      //CAT_BUFF[0] = 0x08;
      cat[0] = 1200 / cwSpeed - 4;
      cat[1] = 0xB2;
      break;
    case 0x63 : //
      //6-0  VOX Gain (#51)  Contains 1-100 (decimal) as displayed
      //7  Disable AM/FM Dial (#4) 0 = Enable, 1 = Disable
      cat[0] = 0xB2;
      cat[1] = 0xA5;
      break;
    case 0x64 : //
      break;
    case 0x67 : //6-0  SSB Mic (#46) Contains 0-100 (decimal) as displayed
      cat[0] = 0xB2;
      cat[1] = 0xB2;
      break;      case 0x69 : //FM Mic (#29)  Contains 0-100 (decimal) as displayed
    case 0x78 :
      if (isUSB)
        cat[0] = CAT_MODE_USB;
      else
        cat[0] = CAT_MODE_LSB;
        
      if (cat[0] != 0) cat[0] = 1 << 5;
      break;
    case  0x79 : //
      //1-0  TX Power (All bands)  00 = High, 01 = L3, 10 = L2, 11 = L1
      //3  PRI On/Off  0 = Off, 1 = On
      //DW On/Off  0 = Off, 1 = On
      //SCN (Scan) Mode  00 = No scan, 10 = Scan up, 11 = Scan down
      //ART On/Off  0 = Off, 1 = On
      cat[0] = 0x00;
      cat[1] = 0x00;
      break;
    case 0x7A : //SPLIT
      //7A  0 HF Antenna Select 0 = Front, 1 = Rear
      //7A  1 6 M Antenna Select  0 = Front, 1 = Rear
      //7A  2 FM BCB Antenna Select 0 = Front, 1 = Rear
      //7A  3 Air Antenna Select  0 = Front, 1 = Rear
      //7A  4 2 M Antenna Select  0 = Front, 1 = Rear
      //7A  5 UHF Antenna Select  0 = Front, 1 = Rear
      //7A  6 ? ?
      //7A  7 SPL On/Off  0 = Off, 1 = On

      cat[0] = (splitOn ? 0xFF : 0x7F);
      break;
    case 0xB3 : //
      cat[0] = 0x00;
      cat[1] = 0x4D;
      break;

  }

  // sent the data
  Serial.write(cat, 2);
}

void processCATCommand2(byte* cmd) {
  byte response[5];
  uint32_t f;
  byte b;
  switch(cmd[4]){
/*  case 0x00:
    response[0]=0;
    Serial.write(response, 1);
    break;
*/    
  case 0x01:
    //set frequency
    f = readFreq(cmd);
    //setFrequency(f); 
    R820T2_set_frequency(f);  
    //updateDisplay();
    response[0]=0;
    Serial.write(response, 1);
  //  sprintf(b, "set:%ld", f); 
  //  printLine2(b);
    break;

  case 0x02:
    //split on 
    splitOn =  1;
    break;
  case 0x82:
    //split off
    splitOn = 0;
    break;
    
  case 0x03:
    writeFreq(r820t_freq,response); // Put the frequency into the buffer
    if (isUSB)
      response[4] = 0x01; //USB
    else
      response[4] = 0x00; //LSB
    Serial.write(response,5);
    //printLine2("cat:getfreq");
    break;
    
  case 0x07: // set mode
    if (cmd[0] == 0x00 || cmd[0] == 0x03)
      isUSB = 0;
    else
      isUSB = 1;
    response[0] = 0x00;
    Serial.write(response, 1);
    //setFrequency(frequency);
    R820T2_set_frequency(r820t_freq);
      //printLine2("cat: mode changed");
    //updateDisplay();
    break;   
 
  case 0x08: // PTT On
    if (!inTx) {
      response[0] = 0;
      txCAT = true;
    //  startTx(TX_SSB);
    //  updateDisplay();
    } else {
      response[0] = 0xf0;
    } 
    Serial.write(response,1);
    //updateDisplay();
    break;

  case 0x88 : //PTT OFF
    if (inTx) {
    //  stopTx();
      txCAT = false;
    }
    response[0] = 0;
    Serial.write(response,1);
   // updateDisplay();
    break;

  case 0x81:
    //toggle the VFOs
    response[0] = 0;
  //  menuVfoToggle(1); // '1' forces it to change the VFO
    Serial.write(response,1);
   // updateDisplay();
    break;

 case 0xBB:  //Read FT-817 EEPROM Data  (for comfirtable)
    catReadEEPRom();
    break;

  case 0xe7 : 
    // get receiver status, we have hardcoded this as
    //as we dont' support ctcss, etc.
    response[0] = 0x09;
    Serial.write(response,1);
    break;
       
  case 0xf7:
    {
      boolean isHighSWR = false;
      boolean isSplitOn = false;
  
      /*
        Inverted -> *ptt = ((p->tx_status & 0x80) == 0); <-- souce code in ft817.c (hamlib)
      */
      response[0] = ((inTx ? 0 : 1) << 7) +
        ((isHighSWR ? 1 : 0) << 6) +  //hi swr off / on
        ((isSplitOn ? 1 : 0) << 5) + //Split on / off
        (0 << 4) +  //dummy data
        0x08;  //P0 meter data

      Serial.write(response, 1);
    }
    break;
    
  default:
    //somehow, get this to print the four bytes
//    ultoa(*((unsigned long *)cmd), c, 16);
//    itoa(cmd[4], b, 16);
//    strcat(b, ">");
//    strcat(b, c);
//    printLine2(b);
    response[0] = 0x00;
    Serial.write(response[0]);
  }

  insideCat = false;
}

int catCount = 0;
void checkCAT(){
  byte i;

  //Check Serial Port Buffer
  if (Serial.available() == 0) {      //Set Buffer Clear status
    rxBufferCheckCount = 0;
    return;
  }
  else if (Serial.available() < 5) {                         //First Arrived
    if (rxBufferCheckCount == 0){
      rxBufferCheckCount = Serial.available();
      rxBufferArriveTime = millis() + CAT_RECEIVE_TIMEOUT;  //Set time for timeout
    }
    else if (rxBufferArriveTime < millis()){                //Clear Buffer
      for (i = 0; i < Serial.available(); i++)
        rxBufferCheckCount = Serial.read();
      rxBufferCheckCount = 0;
    }
    else if (rxBufferCheckCount < Serial.available()){      // Increase buffer count, slow arrive
      rxBufferCheckCount = Serial.available();
      rxBufferArriveTime = millis() + CAT_RECEIVE_TIMEOUT;  //Set time for timeout
    }
    return;
  }

    
  //Arived CAT DATA
  for (i = 0; i < 5; i++)
    cat[i] = Serial.read();


  //this code is not re-entrant.
  if (insideCat == 1)
    return;
  insideCat = 1;

/**
 *  This routine is enabled to debug the cat protocol
  catCount++;

  if (cat[4] != 0xf7 && cat[4] != 0xbb && cat[4] != 0x03){
    sprintf(b, "%d %02x %02x%02x%02x%02x", catCount, cat[4],cat[0], cat[1], cat[2], cat[3]);  
    printLine2(b);  
  }
  */
  processCATCommand2(cat);
  insideCat = 0;
}
