//------------------------------------------------------------------------
//
// Model Railroading with Arduino - NmraDcc.cpp 
//
// Copyright (c) 2008 - 2105 Alex Shepherd
//
// This source file is subject of the GNU general public license 2,
// that is available at the world-wide-web at
// http://www.gnu.org/licenses/gpl.txt
// 
//------------------------------------------------------------------------
//
// file:      NmraDcc.cpp
// author:    Alex Shepherd
// webpage:   http://mrrwa.org/
// history:   2008-03-20 Initial Version
//            2011-06-26 Migrated into Arduino library from OpenDCC codebase
//            2014 Added getAddr to NmraDcc  Geoff Bunza
//            2015-11-06 Martin Pischky (martin@pischky.de):
//                       Experimental Version to support 14 speed steps
//                       and new signature of notifyDccSpeed and notifyDccFunc
//            2015-12-16 Version without use of Timer0 by Franz-Peter Müller
//            2016-07-16 handle glitches on DCC line
//						2016-08-20 added ESP8266 support by Sven (littleyoda) 
//
//------------------------------------------------------------------------
//
// purpose:   Provide a simplified interface to decode NMRA DCC packets
//			  and build DCC Mobile and Stationary Decoders
//
//------------------------------------------------------------------------

#include "NmraDcc.h"

#if defined(ESP8266)
  #include <EEPROM.h>
#else
#include <avr/eeprom.h>
#endif

//------------------------------------------------------------------------
// DCC Receive Routine
//
// Howto:    uses two interrupts: a rising edge in DCC polarity triggers INTx
//           in INTx handler, Timer0 CompareB with a delay of 80us is started.
//           On Timer0 CompareB Match the level of DCC is evaluated and
//           parsed.
//
//                           |<-----116us----->|
//
//           DCC 1: _________XXXXXXXXX_________XXXXXXXXX_________
//                           ^-INTx
//                           |----87us--->|
//                                        ^Timer-INT: reads zero
//
//           DCC 0: _________XXXXXXXXXXXXXXXXXX__________________
//                           ^-INTx
//                           |----------->|
//                                        ^Timer-INT: reads one
//           
// new DCC Receive Routine without Timer0 ........................................................
//
// Howto:    uses only one interrupt at the rising or falling edge of the DCC signal
//           The time between two edges is measured to determine the bit value
//           Synchronising to the edge of the first part of a bit is done after recognizing the start bit
//           During synchronizing each part of a bit is detected ( Interruptmode 'change' )
//
//                           |<-----116us----->|
//           DCC 1: _________XXXXXXXXX_________XXXXXXXXX_________
//                           |<--------146us------>|
//                           ^-INTx            ^-INTx
//                           less than 138us: its a one-Bit
//                                        
//
//                           |<-----------------232us----------->|
//           DCC 0: _________XXXXXXXXXXXXXXXXXX__________________XXXXXXXX__________
//                           |<--------146us------->|
//                           ^-INTx                              ^-INTx
//                           greater than 138us: its a zero bit
//                                        
//                                        
//                                           
//           
//------------------------------------------------------------------------
#define MAX_ONEBITFULL  146
#define MAX_PRAEAMBEL   146 
#define MAX_ONEBITHALF  82
#define MIN_ONEBITFULL  82
#define MIN_ONEBITHALF  35
#define MAX_BITDIFF     18


// Debug-Ports
//#define debug     // Testpulse for logic analyser
#ifdef debug 
    #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        #define MODE_TP1 DDRF |= (1<<2) //pinA2
        #define SET_TP1 PORTF |= (1<<2)
        #define CLR_TP1 PORTF &= ~(1<<2)
        #define MODE_TP2 DDRF |= (1<<3) //pinA3
        #define SET_TP2 PORTF |= (1<<3)
        #define CLR_TP2 PORTF &= ~(1<<3)
        #define MODE_TP3 DDRF |= (1<<4) //pinA4 
        #define SET_TP3 PORTF |= (1<<4) 
        #define CLR_TP3 PORTF &= ~(1<<4) 
        #define MODE_TP4 DDRF |= (1<<5) //pinA5 
        #define SET_TP4 PORTF |= (1<<5) 
        #define CLR_TP4 PORTF &= ~(1<<5) 
    #elif defined(__AVR_ATmega32U4__)
        #define MODE_TP1 DDRF |= (1<<4) //A3
        #define SET_TP1 PORTF |= (1<<4)
        #define CLR_TP1 PORTF &= ~(1<<4)
        #define MODE_TP2 DDRF |= (1<<5) //A2
        #define SET_TP2 PORTF |= (1<<5)
        #define CLR_TP2 PORTF &= ~(1<<5)
        #define MODE_TP3 
        #define SET_TP3 
        #define CLR_TP3 
        #define MODE_TP4 
        #define SET_TP4 
        #define CLR_TP4 
    #elif defined(__AVR_ATmega328P__) 
        #define MODE_TP1 DDRC |= (1<<1) //A1
        #define SET_TP1 PORTC |= (1<<1)
        #define CLR_TP1 PORTC &= ~(1<<1)
        #define MODE_TP2 DDRC |= (1<<2) // A2
        #define SET_TP2 PORTC |= (1<<2)
        #define CLR_TP2 PORTC &= ~(1<<2)
        #define MODE_TP3 DDRC |= (1<<3) //A3
        #define SET_TP3 PORTC |= (1<<3) 
        #define CLR_TP3 PORTC &= ~(1<<3) 
        #define MODE_TP4 DDRC |= (1<<4) //A4 
        #define SET_TP4 PORTC |= (1<<4) 
        #define CLR_TP4 PORTC &= ~(1<<4) 
    #elif defined(__arm__) && (defined(__MK20DX128__) || defined(__MK20DX256__))
        // Teensys 3.x
        #define MODE_TP1 pinMode( A1,OUTPUT )   // A1= PortC, Bit0
        #define SET_TP1  GPIOC_PSOR = 0x01
        #define CLR_TP1  GPIOC_PCOR = 0x01
        #define MODE_TP2 pinMode( A2,OUTPUT )   // A2= PortB Bit0
        #define SET_TP2  GPIOB_PSOR = 0x01
        #define CLR_TP2  GPIOB_PCOR = 0x01
        #define MODE_TP3 pinMode( A3,OUTPUT )   // A3 = PortB Bit1
        #define SET_TP3  GPIOB_PSOR = 0x02
        #define CLR_TP3  GPIOB_PCOR = 0x02
        #define MODE_TP4 pinMode( A4,OUTPUT )   // A4 = PortB Bit3
        #define SET_TP4  GPIOB_PSOR = 0x08
        #define CLR_TP4  GPIOB_PCOR = 0x08
    #elif defined (__SAM3X8E__)
        // Arduino Due
        #define MODE_TP1 pinMode( A1,OUTPUT )   // A1= PA24
        #define SET_TP1  REG_PIOA_SODR = (1<<24)
        #define CLR_TP1  REG_PIOA_CODR = (1<<24)
        #define MODE_TP2 pinMode( A2,OUTPUT )   // A2= PA23
        #define SET_TP2  REG_PIOA_SODR = (1<<23)
        #define CLR_TP2  REG_PIOA_CODR = (1<<23)
        #define MODE_TP3 pinMode( A3,OUTPUT )   // A3 = PA22
        #define SET_TP3  REG_PIOA_SODR = (1<<22)
        #define CLR_TP3  REG_PIOA_CODR = (1<<22)
        #define MODE_TP4 pinMode( A4,OUTPUT )   // A4 = PA6
        #define SET_TP4  REG_PIOA_SODR = (1<<6)
        #define CLR_TP4  REG_PIOA_CODR = (1<<6)
        
    //#elif defined(__AVR_ATmega128__) ||defined(__AVR_ATmega1281__)||defined(__AVR_ATmega2561__)
    #else
        #define MODE_TP1 
        #define SET_TP1 
        #define CLR_TP1 
        #define MODE_TP2 
        #define SET_TP2 
        #define CLR_TP2 
        #define MODE_TP3 
        #define SET_TP3 
        #define CLR_TP3 
        #define MODE_TP4 
        #define SET_TP4 
        #define CLR_TP4 
    
    #endif 
#else
    #define MODE_TP1 
    #define SET_TP1 
    #define CLR_TP1 
    #define MODE_TP2 
    #define SET_TP2 
    #define CLR_TP2 
        //#define MODE_TP2 DDRC |= (1<<2) // A2
        //#define SET_TP2 PORTC |= (1<<2)
        //#define CLR_TP2 PORTC &= ~(1<<2)
    #define MODE_TP3 
    #define SET_TP3 
    #define CLR_TP3 
    #define MODE_TP4 
    #define SET_TP4 
    #define CLR_TP4 
        //#define MODE_TP4 DDRC |= (1<<4) //A4 
        //#define SET_TP4 PORTC |= (1<<4) 
        //#define CLR_TP4 PORTC &= ~(1<<4) 
    
#endif
#ifdef DCC_DBGVAR
struct countOf_t countOf;
#endif

static byte  ISREdge;   // RISING or FALLING
static word  bitMax, bitMin;

typedef enum
{
  WAIT_PREAMBLE = 0,
  WAIT_START_BIT,
  WAIT_DATA,
  WAIT_END_BIT
} 
DccRxWaitState ;

struct DccRx_t
{
  DccRxWaitState  State ;
  uint8_t         DataReady ;
  uint8_t         BitCount ;
  uint8_t         TempByte ;
  DCC_MSG         PacketBuf;
  DCC_MSG         PacketCopy;
} 
DccRx ;

typedef struct
{
  uint8_t   Flags ;
  uint8_t   OpsModeAddressBaseCV ;
  uint8_t   inServiceMode ;
  long      LastServiceModeMillis ;
  uint8_t   PageRegister ;  // Used for Paged Operations in Service Mode Programming
  uint8_t   DuplicateCount ;
  DCC_MSG   LastMsg ;
  uint8_t	ExtIntNum; 
  uint8_t	ExtIntPinNum;
#ifdef DCC_DEBUG
  uint8_t	IntCount;
  uint8_t	TickCount;
  uint8_t   NestedIrqCount;
#endif
} 
DCC_PROCESSOR_STATE ;

DCC_PROCESSOR_STATE DccProcState ;

void ExternalInterruptHandler(void)
{
// Bit evaluation without Timer 0 ------------------------------
    uint8_t DccBitVal;
    static int8_t  bit1, bit2 ;
    static word  lastMicros;
    static byte halfBit, DCC_IrqRunning;
    unsigned int  actMicros, bitMicros;
    if ( DCC_IrqRunning ) {
        // nested DCC IRQ - obviously there are glitches
        // ignore this interrupt and increment glitchcounter
        CLR_TP3;
        #ifdef DCC_DEBUG
            DccProcState.NestedIrqCount++;
        #endif
        SET_TP3;
        return; //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> abort IRQ
    }
    SET_TP3;
    actMicros = micros();
    bitMicros = actMicros-lastMicros;
    if ( bitMicros < bitMin ) {
        // too short - my be false interrupt due to glitch or false protocol -> ignore
        CLR_TP3;
        SET_TP4;
        SET_TP4;
        CLR_TP4;
        return; //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> abort IRQ
    }
    DccBitVal = ( bitMicros < bitMax );
    lastMicros = actMicros;
    //#ifdef debug
    if(DccBitVal) {SET_TP2;} else {CLR_TP2;};
    //#endif
    DCC_IrqRunning = true;
    sei();  // time critical is only the micros() command,so allow nested irq's
#ifdef DCC_DEBUG
    DccProcState.TickCount++;
#endif

  switch( DccRx.State )
  {
  case WAIT_PREAMBLE:
    if( DccBitVal )
    {
        SET_TP1;
      DccRx.BitCount++;
     if( DccRx.BitCount > 10 ) {
        DccRx.State = WAIT_START_BIT ;
        // While waiting for the start bit, detect halfbit lengths. We will detect the correct
        // sync and detect whether we see a false (e.g. motorola) protocol
        attachInterrupt( DccProcState.ExtIntNum, ExternalInterruptHandler, CHANGE);
        halfBit = 0;
        bitMax = MAX_ONEBITHALF;
        bitMin = MIN_ONEBITHALF;
        CLR_TP1;
      }
    } else {
        SET_TP1;
        DccRx.BitCount = 0 ;
        CLR_TP1;
    }
    break;

  case WAIT_START_BIT:
    // we are looking for first half "0" bit after preamble
    switch ( halfBit ) {
      case 0:  //SET_TP1;
        // check first part
        if ( DccBitVal ) {
            // is still 1-bit (Preamble)
            halfBit=1;
            bit1=bitMicros;
        } else {
            // was "0" half bit, maybe the startbit
            halfBit = 4;
        }
        break;
      case 1: //SET_TP1; // previous halfbit was '1'
        if ( DccBitVal ) {
            CLR_TP1;
            // its a '1' halfBit -> we are still in the preamble
            halfBit = 0;
            bit2=bitMicros;
            DccRx.BitCount++;
            if( abs(bit2-bit1) > MAX_BITDIFF ) {
                // the length of the 2 halfbits differ too much -> wrong protokoll
                CLR_TP2;
                CLR_TP3;
                DccRx.State = WAIT_PREAMBLE;
                bitMax = MAX_PRAEAMBEL;
                bitMin = MIN_ONEBITFULL;
                DccRx.BitCount = 0;
                attachInterrupt( DccProcState.ExtIntNum, ExternalInterruptHandler, ISREdge );
                SET_TP3;
            }
        } else {
            // first '0' half detected in second halfBit
            // wrong sync or not a DCC protokoll
            halfBit = 3;
        }
        break;
      case 3: //SET_TP1;  // previous halfbit was '0'  in second halfbit  
        if ( DccBitVal ) {
            // its a '1' halfbit -> we got only a half '0' bit -> cannot be DCC
            DccRx.State = WAIT_PREAMBLE;
            bitMax = MAX_PRAEAMBEL;
            bitMin = MIN_ONEBITFULL;
            DccRx.BitCount = 0;
        } else {
            // we got two '0' halfbits -> it's the startbit
            // but sync is NOT ok, change IRQ edge.
            if ( ISREdge == RISING ) ISREdge = FALLING; else ISREdge = RISING;
            DccRx.State = WAIT_DATA ;
            bitMax = MAX_ONEBITFULL;
            bitMin = MIN_ONEBITFULL;
            DccRx.PacketBuf.Size = 0;
            DccRx.PacketBuf.PreambleBits = 0;
            for(uint8_t i = 0; i< MAX_DCC_MESSAGE_LEN; i++ )
            DccRx.PacketBuf.Data[i] = 0;

            DccRx.PacketBuf.PreambleBits = DccRx.BitCount;
            DccRx.BitCount = 0 ;
            DccRx.TempByte = 0 ;
        }
        attachInterrupt( DccProcState.ExtIntNum, ExternalInterruptHandler, ISREdge );
        CLR_TP1;
        break;
      case 4: SET_TP1; // previous (first) halfbit was 0
        // if this halfbit is 0 too, we got the startbit
        if ( DccBitVal ) {
            // second halfbit is 1 -> unknown protokoll
            DccRx.State = WAIT_PREAMBLE;
            bitMax = MAX_PRAEAMBEL;
            bitMin = MIN_ONEBITFULL;
            DccRx.BitCount = 0;
        } else {
            // we got the startbit
            DccRx.State = WAIT_DATA ;
            bitMax = MAX_ONEBITFULL;
            bitMin = MIN_ONEBITFULL;
            DccRx.PacketBuf.Size = 0;
            DccRx.PacketBuf.PreambleBits = 0;
            for(uint8_t i = 0; i< MAX_DCC_MESSAGE_LEN; i++ )
            DccRx.PacketBuf.Data[i] = 0;

            DccRx.PacketBuf.PreambleBits = DccRx.BitCount;
            DccRx.BitCount = 0 ;
            DccRx.TempByte = 0 ;
        }
        attachInterrupt( DccProcState.ExtIntNum, ExternalInterruptHandler, ISREdge );
        CLR_TP1;
        break;
            
    }        
    break;

  case WAIT_DATA:
    DccRx.BitCount++;
    DccRx.TempByte = ( DccRx.TempByte << 1 ) ;
    if( DccBitVal )
      DccRx.TempByte |= 1 ;

    if( DccRx.BitCount == 8 )
    {
      if( DccRx.PacketBuf.Size == MAX_DCC_MESSAGE_LEN ) // Packet is too long - abort
      {
        DccRx.State = WAIT_PREAMBLE ;
        bitMax = MAX_PRAEAMBEL;
        bitMin = MIN_ONEBITFULL;
        DccRx.BitCount = 0 ;
      }
      else
      {
        DccRx.State = WAIT_END_BIT ;
        DccRx.PacketBuf.Data[ DccRx.PacketBuf.Size++ ] = DccRx.TempByte ;
      }
    }
    break;

  case WAIT_END_BIT:
    DccRx.BitCount++;
    if( DccBitVal ) // End of packet?
    {
      CLR_TP3;
      DccRx.State = WAIT_PREAMBLE ;
      bitMax = MAX_PRAEAMBEL;
      bitMin = MIN_ONEBITFULL;
      DccRx.PacketCopy = DccRx.PacketBuf ;
      DccRx.DataReady = 1 ;
      SET_TP3;
    }
    else  // Get next Byte
      // KGW - Abort immediately if packet is too long.
      if( DccRx.PacketBuf.Size == MAX_DCC_MESSAGE_LEN ) // Packet is too long - abort
      {
        DccRx.State = WAIT_PREAMBLE ;
        bitMax = MAX_PRAEAMBEL;
        bitMin = MIN_ONEBITFULL;
        DccRx.BitCount = 0 ;
      }
      else
      {
        DccRx.State = WAIT_DATA ;

        DccRx.BitCount = 0 ;
        DccRx.TempByte = 0 ;
      }
  }
  CLR_TP1;
  CLR_TP3;
  DCC_IrqRunning = false;
}

void ackCV(void)
{
  if( notifyCVAck )
    notifyCVAck() ;
}

uint8_t readEEPROM( unsigned int CV ) {
  #if defined(ESP8266)
    return EEPROM.read(CV) ;
  #else
    return eeprom_read_byte( (uint8_t*) CV );
  #endif
}

void writeEEPROM( unsigned int CV, uint8_t Value ) {
  #if defined(ESP8266)
    EEPROM.write(CV, Value) ;
    EEPROM.commit();
  #else
    eeprom_write_byte( (uint8_t*) CV, Value ) ;
  #endif
}

bool readyEEPROM() {
  #if defined(ESP8266)
    return true;
  #else
    return eeprom_is_ready();
  #endif
}


uint8_t validCV( uint16_t CV, uint8_t Writable )
{
  if( notifyCVResetFactoryDefault && (CV == CV_MANUFACTURER_ID )  && Writable )
	notifyCVResetFactoryDefault();
	
  if( notifyCVValid )
    return notifyCVValid( CV, Writable ) ;

  uint8_t Valid = 1 ;

  if( CV > MAXCV )
    Valid = 0 ;

  if( Writable && ( ( CV ==CV_VERSION_ID ) || (CV == CV_MANUFACTURER_ID ) ) )
    Valid = 0 ;

  return Valid ;
}

uint8_t readCV( unsigned int CV )
{
  uint8_t Value ;

  if( notifyCVRead )
    return notifyCVRead( CV ) ;

  Value = readEEPROM(CV);
  return Value ;
}

uint8_t writeCV( unsigned int CV, uint8_t Value)
{
  if( notifyCVWrite )
    return notifyCVWrite( CV, Value ) ;

  if( readEEPROM( CV ) != Value )
  {
    writeEEPROM( CV, Value ) ;

    if( notifyCVChange )
      notifyCVChange( CV, Value) ;
  }
  return readEEPROM( CV ) ;
}

uint16_t getMyAddr(void)
{
  uint16_t  Addr ;
  uint8_t   CV29Value ;

  CV29Value = readCV( CV_29_CONFIG ) ;

  if( CV29Value & CV29_ACCESSORY_DECODER )  // Accessory Decoder? 
    Addr = ( readCV( CV_ACCESSORY_DECODER_ADDRESS_MSB ) << 6 ) | readCV( CV_ACCESSORY_DECODER_ADDRESS_LSB ) ;

  else   // Multi-Function Decoder?
  {
    if( CV29Value & CV29_EXT_ADDRESSING )  // Two Byte Address?
      Addr = ( ( readCV( CV_MULTIFUNCTION_EXTENDED_ADDRESS_MSB ) - 192 ) << 8 ) | readCV( CV_MULTIFUNCTION_EXTENDED_ADDRESS_LSB ) ;

    else
      Addr = readCV( 1 ) ;
  }

  return Addr ;
}

void processDirectOpsOperation( uint8_t Cmd, uint16_t CVAddr, uint8_t Value )
{
  // is it a Byte Operation
  if( Cmd & 0x04 )
  {
    // Perform the Write Operation
    if( Cmd & 0x08 )
    {
      if( validCV( CVAddr, 1 ) )
      {
        if( writeCV( CVAddr, Value ) == Value )
          ackCV();
      }
    }

    else  // Perform the Verify Operation
    {  
      if( validCV( CVAddr, 0 ) )
      {
        if( readCV( CVAddr ) == Value )
          ackCV();
      }
    }
  }
  // Perform the Bit-Wise Operation
  else
  {
    uint8_t BitMask = (1 << (Value & 0x07) ) ;
    uint8_t BitValue = Value & 0x08 ;
    uint8_t BitWrite = Value & 0x10 ;

    uint8_t tempValue = readCV( CVAddr ) ;  // Read the Current CV Value

    // Perform the Bit Write Operation
    if( BitWrite )
    {
      if( validCV( CVAddr, 1 ) )
      {
        if( BitValue )
          tempValue |= BitMask ;     // Turn the Bit On

        else
          tempValue &= ~BitMask ;  // Turn the Bit Off

        if( writeCV( CVAddr, tempValue ) == tempValue )
          ackCV() ;
      }
    }

    // Perform the Bit Verify Operation
    else
    {
      if( validCV( CVAddr, 0 ) )
      {
        if( BitValue ) 
        {
          if( tempValue & BitMask )
            ackCV() ;
        }
        else
        {
          if( !( tempValue & BitMask)  )
            ackCV() ;
        }
      }
    }
  }
}

#ifdef NMRA_DCC_PROCESS_MULTIFUNCTION
void processMultiFunctionMessage( uint16_t Addr, DCC_ADDR_TYPE AddrType, uint8_t Cmd, uint8_t Data1, uint8_t Data2 )
{
  uint8_t  speed ;
  uint16_t CVAddr ;
  DCC_DIRECTION dir ;
  DCC_SPEED_STEPS speedSteps ;

  uint8_t  CmdMasked = Cmd & 0b11100000 ;

  // If we are an Accessory Decoder
  if( DccProcState.Flags & FLAGS_DCC_ACCESSORY_DECODER )
  {
    // and this isn't an Ops Mode Write or we are NOT faking the Multifunction Ops mode address in CV 33+34 or
    // it's not our fake address, then return
    if( ( CmdMasked != 0b11100000 ) || ( DccProcState.OpsModeAddressBaseCV == 0 ) )
      return ;

    uint16_t FakeOpsAddr = readCV( DccProcState.OpsModeAddressBaseCV ) | ( readCV( DccProcState.OpsModeAddressBaseCV + 1 ) << 8 ) ;
    uint16_t OpsAddr = Addr & 0x3FFF ;

    if( OpsAddr != FakeOpsAddr )
      return ;
  }

  // We are looking for FLAGS_MY_ADDRESS_ONLY but it does not match and it is not a Broadcast Address then return
  else if( ( DccProcState.Flags & FLAGS_MY_ADDRESS_ONLY ) && ( Addr != getMyAddr() ) && ( Addr != 0 ) ) 
    return ;

  switch( CmdMasked )
  {
  case 0b00000000:  // Decoder Control
    switch( Cmd & 0b00001110 )
    {
    case 0b00000000:  
      if( notifyDccReset && ( Cmd & 0b00000001 ) ) // Hard Reset
        if( notifyDccReset)
          notifyDccReset( 1 ) ;
      break ;

    case 0b00000010:  // Factory Test
      break ;

    case 0b00000110:  // Set Decoder Flags
      break ;

    case 0b00001010:  // Set Advanced Addressing
      break ;

    case 0b00001110:  // Decoder Acknowledgment
      break ;

    default:    // Reserved
      ;
    }
    break ;

  case 0b00100000:  // Advanced Operations
    switch( Cmd & 0b00011111 )
    {
    case 0b00011111:
      if( notifyDccSpeed )
      {
        switch( Data1 & 0b01111111 )
        {
        case 0b00000000:  // 0=STOP
          speed = 1 ;     // => 1
          break ;

        case 0b00000001:  // 1=EMERGENCY_STOP
          speed = 0 ;     // => 0
          break ;

        default:          // 2..127
          speed = (Data1 & 0b01111111) ;
        }
        dir = (DCC_DIRECTION) ((Data1 & 0b10000000) >> 7) ;
        notifyDccSpeed( Addr, AddrType, speed, dir, SPEED_STEP_128 ) ;
      }
    }
    break;

  case 0b01000000:
  case 0b01100000:
    //TODO should we cache this info in DCC_PROCESSOR_STATE.Flags ?
#ifdef NMRA_DCC_ENABLE_14_SPEED_STEP_MODE
    speedSteps = (readCV( CV_29_CONFIG ) & CV29_F0_LOCATION) ? SPEED_STEP_28 : SPEED_STEP_14 ;
#else
    speedSteps = SPEED_STEP_28 ;
#endif    
    if( notifyDccSpeed )
    {
      switch( Cmd & 0b00011111 )
      {
      case 0b00000000:    // 0 0000 = STOP   
      case 0b00010000:    // 1 0000 = STOP
        speed = 1 ;       // => 1
        break ;

      case 0b00000001:    // 0 0001 = EMERGENCY STOP
      case 0b00010001:    // 1 0001 = EMERGENCY STOP
        speed = 0 ;       // => 0
        break ;

      default:
#ifdef NMRA_DCC_ENABLE_14_SPEED_STEP_MODE
        if( speedSteps == SPEED_STEP_14 )
        {
          speed = (Cmd & 0b00001111) ; // => 2..15
        }
        else
        {
#endif
          speed = (((Cmd & 0b00001111) << 1 ) | ((Cmd & 0b00010000) >> 4)) - 2 ; // => 2..29
#ifdef NMRA_DCC_ENABLE_14_SPEED_STEP_MODE
        }    
#endif        
      }
      dir = (DCC_DIRECTION) ((Cmd & 0b00100000) >> 5) ;
      notifyDccSpeed( Addr, AddrType, speed, dir, speedSteps ) ;
    }
    if( notifyDccSpeedRaw )
    	notifyDccSpeedRaw(Addr, AddrType, Cmd );

#ifdef NMRA_DCC_ENABLE_14_SPEED_STEP_MODE
    if( notifyDccFunc && (speedSteps == SPEED_STEP_14) )
    {
      // function light is controlled by this package
      uint8_t fn0 = (Cmd & 0b00010000) ;
      notifyDccFunc( Addr, AddrType, FN_0, fn0 ) ;
    }
#endif
    break;

  case 0b10000000:  // Function Group 0..4
    if( notifyDccFunc )
    { 
        // function light is controlled by this package (28 or 128 speed steps)
        notifyDccFunc( Addr, AddrType, FN_0_4, Cmd & 0b00011111 ) ;
    }
    break;

  case 0b10100000:  // Function Group 5..8
    if( notifyDccFunc)
    {
      if (Cmd & 0b00010000 )
        notifyDccFunc( Addr, AddrType, FN_5_8,  Cmd & 0b00001111 ) ;
      else
        notifyDccFunc( Addr, AddrType, FN_9_12, Cmd & 0b00001111 ) ;
    }
    break;

  case 0b11000000:  // Feature Expansion Instruction
  	switch(Cmd & 0b00011111)
  	{
  	case 0B00011110:
  	  if( notifyDccFunc )
	    notifyDccFunc( Addr, AddrType, FN_13_20, Data1 ) ;
	  break;
	  
  	case 0B00011111:
  	  if( notifyDccFunc )
	    notifyDccFunc( Addr, AddrType, FN_21_28, Data1 ) ;
	  break;
  	}
    break;

  case 0b11100000:  // CV Access
    CVAddr = ( ( ( Cmd & 0x03 ) << 8 ) | Data1 ) + 1 ;

    processDirectOpsOperation( Cmd, CVAddr, Data2 ) ;
    break;
  }
}
#endif

#ifdef NMRA_DCC_PROCESS_SERVICEMODE
void processServiceModeOperation( DCC_MSG * pDccMsg )
{
  uint16_t CVAddr ;
  uint8_t Value ;

  if( pDccMsg->Size == 3) // 3 Byte Packets are for Address Only, Register and Paged Mode
  {
    uint8_t RegisterAddr ;

    RegisterAddr = pDccMsg->Data[0] & 0x07 ;
    Value = pDccMsg->Data[1] ;

    if( RegisterAddr == 5 )
    {
      DccProcState.PageRegister = Value ;
      ackCV();
    }

    else
    {
      if( RegisterAddr == 4 )
        CVAddr = CV_29_CONFIG ;

      else if( ( RegisterAddr <= 3 ) && ( DccProcState.PageRegister > 0 ) )
        CVAddr = ( ( DccProcState.PageRegister - 1 ) * 4 ) + RegisterAddr + 1 ;

      else
        CVAddr = RegisterAddr + 1 ;

      if( pDccMsg->Data[0] & 0x08 ) // Perform the Write Operation
      {
        if( validCV( CVAddr, 1 ) )
        {
          if( writeCV( CVAddr, Value ) == Value )
            ackCV();
        }
      }

      else  // Perform the Verify Operation
      {  
        if( validCV( CVAddr, 0 ) )
        {
          if( readCV( CVAddr ) == Value )
            ackCV();
        }
      }
    }
  }

  else if( pDccMsg->Size == 4) // 4 Byte Packets are for Direct Byte & Bit Mode
  {
    CVAddr = ( ( ( pDccMsg->Data[0] & 0x03 ) << 8 ) | pDccMsg->Data[1] ) + 1 ;
    Value = pDccMsg->Data[2] ;

    processDirectOpsOperation( pDccMsg->Data[0] & 0b00001100, CVAddr, Value ) ;
  }
}
#endif
void resetServiceModeTimer(uint8_t inServiceMode)
{
  // Set the Service Mode
  DccProcState.inServiceMode = inServiceMode ;
  
  DccProcState.LastServiceModeMillis = inServiceMode ? millis() : 0 ;
}

void clearDccProcState(uint8_t inServiceMode)
{
  resetServiceModeTimer( inServiceMode ) ;

  // Set the Page Register to it's default of 1 only on the first Reset
  DccProcState.PageRegister = 1 ;

  // Clear the LastMsg buffer and DuplicateCount in preparation for possible CV programming
  DccProcState.DuplicateCount = 0 ;
  memset( &DccProcState.LastMsg, 0, sizeof( DCC_MSG ) ) ;
}

void execDccProcessor( DCC_MSG * pDccMsg )
{
  if( ( pDccMsg->Data[0] == 0 ) && ( pDccMsg->Data[1] == 0 ) )
  {
    if( notifyDccReset )
      notifyDccReset( 0 ) ;

#ifdef NMRA_DCC_PROCESS_SERVICEMODE
    // If this is the first Reset then perform some one-shot actions as we maybe about to enter service mode
    if( DccProcState.inServiceMode )
      resetServiceModeTimer( 1 ) ;
    else
      clearDccProcState( 1 );
#endif
  }

  else
  {
#ifdef NMRA_DCC_PROCESS_SERVICEMODE
    if( DccProcState.inServiceMode && ( pDccMsg->Data[0] >= 112 ) && ( pDccMsg->Data[0] < 128 ) )
    {
      resetServiceModeTimer( 1 ) ;

      if( memcmp( pDccMsg, &DccProcState.LastMsg, sizeof( DCC_MSG ) ) )
      {
        DccProcState.DuplicateCount = 0 ;
        memcpy( &DccProcState.LastMsg, pDccMsg, sizeof( DCC_MSG ) ) ;
      }
      // Wait until you see 2 identicle packets before acting on a Service Mode Packet 
      else
      {
        DccProcState.DuplicateCount++ ;
        processServiceModeOperation( pDccMsg ) ;
      }
    }

    else
    {
      if( DccProcState.inServiceMode )
        clearDccProcState( 0 );	
#endif

      // Idle Packet
      if( ( pDccMsg->Data[0] == 0b11111111 ) && ( pDccMsg->Data[1] == 0 ) )
      {
        if( notifyDccIdle )
          notifyDccIdle() ;
      }

#ifdef NMRA_DCC_PROCESS_MULTIFUNCTION
      // Multi Function Decoders (7-bit address)
      else if( pDccMsg->Data[0] < 128 )
        processMultiFunctionMessage( pDccMsg->Data[0], DCC_ADDR_SHORT, pDccMsg->Data[1], pDccMsg->Data[2], pDccMsg->Data[3] ) ;  

      // Basic Accessory Decoders (9-bit) & Extended Accessory Decoders (11-bit)
      else if( pDccMsg->Data[0] < 192 )
#else
      else if( ( pDccMsg->Data[0] >= 128 ) && ( pDccMsg->Data[0] < 192 ) )
#endif
      {
        if( DccProcState.Flags & FLAGS_DCC_ACCESSORY_DECODER )
        {
          uint16_t BoardAddress ;
          uint8_t  OutputAddress ;
          uint8_t  OutputIndex ;
          uint16_t Address ;

          BoardAddress = ( ( (~pDccMsg->Data[1]) & 0b01110000 ) << 2 ) | ( pDccMsg->Data[0] & 0b00111111 ) ;

          // If we're filtering was it my board address Our or a broadcast address
          if( ( DccProcState.Flags & FLAGS_MY_ADDRESS_ONLY ) && ( BoardAddress != getMyAddr() ) && ( BoardAddress != 511 ) )
            return;

          OutputAddress = pDccMsg->Data[1] & 0b00000111 ;
          
          OutputIndex = OutputAddress >> 1;

          Address = ( ( ( BoardAddress - 1 ) << 2 ) | OutputIndex ) + 1 ;

          if(pDccMsg->Data[1] & 0b10000000)
          {
          	uint8_t direction = OutputAddress & 0x01;
          	uint8_t outputPower = (pDccMsg->Data[1] & 0b00001000) >> 3;
          	
            if( notifyDccAccState )
              notifyDccAccState( Address, BoardAddress, OutputAddress, pDccMsg->Data[1] & 0b00001000 ) ;
              
            if( notifyDccAccTurnoutBoard )
            	notifyDccAccTurnoutBoard( BoardAddress, OutputIndex, direction, outputPower );
            	
            if( notifyDccAccTurnoutOutput )
            	notifyDccAccTurnoutOutput( Address, direction, outputPower );
          }

          else
          {
            if( notifyDccSigState )
              notifyDccSigState( Address, OutputIndex, pDccMsg->Data[2] ) ;
          }
        }
      }

#ifdef NMRA_DCC_PROCESS_MULTIFUNCTION
      // Multi Function Decoders (14-bit address)
      else if( pDccMsg->Data[0] < 232 )
      {
        uint16_t Address ;
        Address = ( ( pDccMsg->Data[0] - 192 ) << 8 ) | pDccMsg->Data[1];
        //TODO should we convert Address to 1 .. 10239 ?
        processMultiFunctionMessage( Address, DCC_ADDR_LONG, pDccMsg->Data[2], pDccMsg->Data[3], pDccMsg->Data[4] ) ;  
      }
#endif
#ifdef NMRA_DCC_PROCESS_SERVICEMODE
    }
#endif
  }
}

NmraDcc::NmraDcc()
{
}

void NmraDcc::pin( uint8_t ExtIntNum, uint8_t ExtIntPinNum, uint8_t EnablePullup)
{
  DccProcState.ExtIntNum = ExtIntNum;
  DccProcState.ExtIntPinNum = ExtIntPinNum;
	
  pinMode( ExtIntPinNum, INPUT );
  if( EnablePullup )
    digitalWrite(ExtIntPinNum, HIGH);
}

void NmraDcc::initAccessoryDecoder( uint8_t ManufacturerId, uint8_t VersionId, uint8_t Flags, uint8_t OpsModeAddressBaseCV )
{
	init(ManufacturerId, VersionId, Flags | FLAGS_DCC_ACCESSORY_DECODER, OpsModeAddressBaseCV);
}

void NmraDcc::init( uint8_t ManufacturerId, uint8_t VersionId, uint8_t Flags, uint8_t OpsModeAddressBaseCV )
{
  #if defined(ESP8266)
    EEPROM.begin(MAXCV);
  #endif
  // Clear all the static member variables
  memset( &DccRx, 0, sizeof( DccRx) );

  MODE_TP1; // only for debugging and timing measurement
  MODE_TP2;
  MODE_TP3;
  MODE_TP4;
  ISREdge = RISING;
  bitMax = MAX_ONEBITFULL;
  bitMin = MIN_ONEBITFULL;
  attachInterrupt( DccProcState.ExtIntNum, ExternalInterruptHandler, RISING);

  DccProcState.Flags = Flags ;
  DccProcState.OpsModeAddressBaseCV = OpsModeAddressBaseCV ;

  // Set the Bits that control Multifunction or Accessory behaviour
  // and if the Accessory decoder optionally handles Output Addressing 
  uint8_t cv29Mask = Flags & (CV29_ACCESSORY_DECODER | CV29_OUTPUT_ADDRESS_MODE) ; // peal off the top two bits
  writeCV( CV_29_CONFIG, ( readCV( CV_29_CONFIG ) & ~cv29Mask ) | (Flags & ~FLAGS_MY_ADDRESS_ONLY) ) ; // KGW: Don't write bit 0 to CV.

  writeCV( 7, VersionId ) ;
  writeCV( 8, ManufacturerId ) ;

  clearDccProcState( 0 );
}

uint8_t NmraDcc::getCV( uint16_t CV )
{
  return readCV(CV);
}

uint8_t NmraDcc::setCV( uint16_t CV, uint8_t Value)
{
  return writeCV(CV,Value);
}

uint16_t NmraDcc::getAddr(void)
{
  return getMyAddr();
}

uint8_t NmraDcc::isSetCVReady(void)
{
  if(notifyIsSetCVReady)
	return notifyIsSetCVReady();
  return readyEEPROM();
}

#ifdef DCC_DEBUG
uint8_t NmraDcc::getIntCount(void)
{
  return DccProcState.IntCount;
}

uint8_t NmraDcc::getTickCount(void)
{
  return DccProcState.TickCount;
}

uint8_t NmraDcc::getNestedIrqCount(void)
{
  return DccProcState.NestedIrqCount;
}

uint8_t NmraDcc::getState(void)
{
  return DccRx.State;
}

uint8_t NmraDcc::getBitCount(void)
{
  return DccRx.BitCount;
}
#endif

uint8_t NmraDcc::process()
{
  if( DccProcState.inServiceMode )
  {
    if( (millis() - DccProcState.LastServiceModeMillis ) > 20L )
    {
      clearDccProcState( 0 ) ;
    }
  }

  if( DccRx.DataReady )
  {
    // We need to do this check with interrupts disabled
    //SET_TP4;
    cli();
    Msg = DccRx.PacketCopy ;
    DccRx.DataReady = 0 ;
    sei();
      #ifdef DCC_DBGVAR
      countOf.Tel++;
      #endif
    
    uint8_t xorValue = 0 ;
    
    for(uint8_t i = 0; i < DccRx.PacketCopy.Size; i++)
      xorValue ^= DccRx.PacketCopy.Data[i];
    //CLR_TP4;
    if(xorValue) {
      SET_TP4;
      #ifdef DCC_DBGVAR
      countOf.Err++;
      #endif
      CLR_TP4;
      return 0 ;
    } else {
        //SET_TP4;
		if( notifyDccMsg ) 	notifyDccMsg( &Msg );
		
        execDccProcessor( &Msg );
        //CLR_TP4;
    }
    return 1 ;
  }

  return 0 ;  
};