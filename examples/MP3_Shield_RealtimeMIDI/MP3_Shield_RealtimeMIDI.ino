#define USE_SPI_MIDI   1
#define USE_PATCH_INIT  !USE_GPIO_INIT
#define USE_SERIAL_MIDI !USE_SPI_INIT
#define USE_SPI         (USE_SPI_MIDI||USE_PATCH_INIT)
#include <SPI.h>
#define VS_XCS    6 // Control Chip Select Pin (for accessing SPI Control/Status registers)
#define VS_XDCS   7 // Data Chip Select / BSYNC Pin
#define VS_DREQ   2 // Data Request Pin: Player asks for more data
#define VS_RESET  8 //Reset is active low

//Write to VS10xx register
//SCI: Data transfers are always 16bit. When a new SCI operation comes in 
//DREQ goes low. We then have to wait for DREQ to go high again.
//XCS should be low for the full duration of operation.

void VSWriteRegister(unsigned char addressbyte, unsigned char highbyte, unsigned char lowbyte){
  while(!digitalRead(VS_DREQ)) ; //Wait for DREQ to go high indicating IC is available
  digitalWrite(VS_XCS, LOW); //Select control

  //SCI consists of instruction byte, address byte, and 16-bit data word.
  SPI.transfer(0x02); //Write instruction
  SPI.transfer(addressbyte);
  SPI.transfer(highbyte);
  SPI.transfer(lowbyte);
  while(!digitalRead(VS_DREQ)) ; //Wait for DREQ to go high indicating command is complete
  digitalWrite(VS_XCS, HIGH); //Deselect Control
}

// Plugin to put VS10XX into realtime MIDI mode
// Originally from http://www.vlsi.fi/fileadmin/software/VS10XX/vs1053b-rtmidistart.zip
// Permission to reproduce here granted by VLSI solution.
//
const unsigned short sVS1053b_Realtime_MIDI_Plugin[28] = { /* Compressed plugin */
  0x0007, 0x0001, 0x8050, 0x0006, 0x0014, 0x0030, 0x0715, 0xb080, /*    0 */
  0x3400, 0x0007, 0x9255, 0x3d00, 0x0024, 0x0030, 0x0295, 0x6890, /*    8 */
  0x3400, 0x0030, 0x0495, 0x3d00, 0x0024, 0x2908, 0x4d40, 0x0030, /*   10 */
  0x0200, 0x000a, 0x0001, 0x0050,
};

void VSLoadUserCode(void) {
  int i = 0;

  while (i<sizeof(sVS1053b_Realtime_MIDI_Plugin)/sizeof(sVS1053b_Realtime_MIDI_Plugin[0])) {
    unsigned short addr, n, val;
    addr = sVS1053b_Realtime_MIDI_Plugin[i++];
    n = sVS1053b_Realtime_MIDI_Plugin[i++];
    while (n--) {
      val = sVS1053b_Realtime_MIDI_Plugin[i++];
      VSWriteRegister(addr, val >> 8, val & 0xFF);
    }
  }
}



void setup() {
  pinMode(VS_DREQ, INPUT);
  pinMode(VS_XCS, OUTPUT);
  pinMode(VS_XDCS, OUTPUT);
  digitalWrite(VS_XCS, HIGH); //Deselect Control
  digitalWrite(VS_XDCS, HIGH); //Deselect Data
  pinMode(VS_RESET, OUTPUT);
  Serial.begin(57600); //Use serial for debugging 
  Serial.println("\n******\n");
  Serial.println("MP3 Shield Example");
  
  //Initialize VS1053 chip 
  digitalWrite(VS_RESET, LOW); //Put VS1053 into hardware reset


  pinMode(10, OUTPUT); //Pin 10 must be set as an output for the SPI communication to work
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);

  //From page 12 of datasheet, max SCI reads are CLKI/7. Input clock is 12.288MHz. 
  //Internal clock multiplier is 1.0x after power up. 
  //Therefore, max SPI speed is 1.75MHz. We will use 1MHz to be safe.
  SPI.setClockDivider(SPI_CLOCK_DIV16); //Set SPI bus speed to 1MHz (16MHz / 16 = 1MHz)
  SPI.transfer(0xFF); //Throw a dummy byte at the bus
  delayMicroseconds(1);
  digitalWrite(VS_RESET, HIGH); //Bring up VS1053
  
  VSLoadUserCode();

}

void sendMIDI(byte data)
{
  SPI.transfer(0);
  SPI.transfer(data);
}

//Plays a MIDI note. Doesn't check to see that cmd is greater than 127, or that data values are less than 127
void talkMIDI(byte cmd, byte data1, byte data2) {
#if USE_SPI_MIDI
  //
  // Wait for chip to be ready (Unlikely to be an issue with real time MIDI)
  //
  while (!digitalRead(VS_DREQ))
    ;
  digitalWrite(VS_XDCS, LOW);
#endif
  sendMIDI(cmd);
  //Some commands only have one data byte. All cmds less than 0xBn have 2 data bytes 
  //(sort of: http://253.ccarh.org/handout/midiprotocol/)
  if( (cmd & 0xF0) <= 0xB0 || (cmd & 0xF0) >= 0xE0) {
    sendMIDI(data1);
    sendMIDI(data2);
  } else {
    sendMIDI(data1);
  }

#if USE_SPI_MIDI
  digitalWrite(VS_XDCS, HIGH);
#endif
}

//Send a MIDI note-on message.  Like pressing a piano key
//channel ranges from 0-15
void noteOn(byte channel, byte note, byte attack_velocity) {
  talkMIDI( (0x90 | channel), note, attack_velocity);
}

//Send a MIDI note-off message.  Like releasing a piano key
void noteOff(byte channel, byte note, byte release_velocity) {
  talkMIDI( (0x80 | channel), note, release_velocity);
}

void loop() {
  delay(1000);
  
  talkMIDI(0xB0, 0x07, 120); //0xB0 is channel message, set channel volume to near max (127)

#if 1
  //Demo Basic MIDI instruments, GM1
  //=================================================================
  Serial.println("Basic Instruments");
  talkMIDI(0xB0, 0, 0x00); //Default bank GM1

  //Change to different instrument
  for(int instrument = 0 ; instrument < 127 ; instrument++) {

    Serial.print(" Instrument: ");
    Serial.println(instrument, DEC);

    talkMIDI(0xC0, instrument, 0); //Set instrument number. 0xC0 is a 1 data byte command

    //Play notes from F#-0 (30) to F#-5 (90):
    for (int note = 30 ; note < 50 ; note++) {
      Serial.print("N:");
      Serial.println(note, DEC);
      
      //Note on channel 1 (0x90), some note value (note), middle velocity (0x45):
      noteOn(0, note, 127);
      delay(200);

      //Turn off the note with a given off/release velocity
      noteOff(0, note, 127);
      delay(10);
    }

    delay(100); //Delay between instruments
  }
  //=================================================================
#endif

#if 1
  for(int instrument = 0 ; instrument < 1 ; instrument++) {
    for (int bank=0; bank<2; ++bank) {
      talkMIDI(0xB0, 0, bank ? 0x79 : 0);
      Serial.print("Bank: ");
      Serial.print(bank ? 0x79 : 0, DEC);
      Serial.print(" Instrument: ");
      Serial.println(instrument+1, DEC);
  
      talkMIDI(0xC0, instrument, 0);
      noteOn(0, 60, 127);
      noteOn(0, 63, 127);
      noteOn(0, 67, 127);
      delay(2000);
      
      //Turn off the note with a given off/release velocity
      noteOff(0, 60, 127);
      noteOff(0, 63, 127);
      noteOff(0, 67, 127);
      delay(100);
  }
  delay(5000);
}
#endif

#if 1
  //Demo GM2 / Fancy sounds
  //=================================================================
  Serial.println("Demo Fancy Sounds");
  talkMIDI(0xB0, 0, 0x78); //Bank select drums

  //For this bank 0x78, the instrument does not matter, only the note
  for(int instrument = 30 ; instrument < 31 ; instrument++) {

    Serial.print(" Instrument: ");
    Serial.println(instrument, DEC);

    talkMIDI(0xC0, instrument, 0); //Set instrument number. 0xC0 is a 1 data byte command

    //Play fancy sounds from 'High Q' to 'Open Surdo [EXC 6]'
    for (int note = 27 ; note < 87 ; note++) {
      Serial.print("N:");
      Serial.println(note, DEC);
      
      //Note on channel 1 (0x90), some note value (note), middle velocity (0x45):
      noteOn(0, note, 127);
      delay(50);

      //Turn off the note with a given off/release velocity
      noteOff(0, note, 127);
      delay(50);
    }

    delay(100); //Delay between instruments
  }
#endif

#if 0
  //Demo Melodic
  //=================================================================
  Serial.println("Demo Melodic? Sounds");
  talkMIDI(0xB0, 0, 0x79); //Bank select Melodic
  //These don't sound different from the main bank to me

  //Change to different instrument
  for(int instrument = 27 ; instrument < 87 ; instrument++) {

    Serial.print(" Instrument: ");
    Serial.println(instrument, DEC);

    talkMIDI(0xC0, instrument, 0); //Set instrument number. 0xC0 is a 1 data byte command

    //Play notes from F#-0 (30) to F#-5 (90):
    for (int note = 30 ; note < 40 ; note++) {
      Serial.print("N:");
      Serial.println(note, DEC);
      
      //Note on channel 1 (0x90), some note value (note), middle velocity (0x45):
      noteOn(0, note, 127);
      delay(500);

      //Turn off the note with a given off/release velocity
      noteOff(0, note, 127);
      delay(50);
    }

    delay(100); //Delay between instruments
  }
#endif
}
