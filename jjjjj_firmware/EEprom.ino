//-------------------------------------------------
//                  NAVA v1.x
//                 EEprom 1024K
//
//Pattern size:
//  -16 instruments with 2 bytes each = 32 bytes
//  -16 steps of 16 instruments velocity (0 to 127 so 1 byte) = 256 bytes
//  -100 notes ext instrument  = 128 bytes// to do
//  -Setup pattern, each 1 byte: Scale, Shuffle, Flam, Length, ext length, group position, group length and 25 more parameters = 32 b
//  PATTERN_SIZE = 448 bytes x 128 = 57344 bytes(7 pages of 64 bytes)
//  OFFSET_PATTERN = 0
//
//Track size:
//  -each track is 1022 pattern = 1022 bytes  OFFICALY 1000 patterns by to got a multiple of 64 bytes page
//  -track lenght = 2 bytes   the last two byte in "track[trkBuffer].patternNbr[trk.pos]"
//  TRACK_SIZE = 1024 bytes x 16 = 16384 bytes
//  OFFSET_TRACK = MAX_PTRN * 448 = 57344 bytes
//
//Setup size:
//  -OFFSET_SETUP = TRACK_OFFSET + (TRACK_SIZE * MAX_TRACK) = 73728;
//
// Bootloader flag address - at the very end of EEPROM  
#define BOOTLOADER_FLAG_ADDR 0x3FF
#define BOOTLOADER_FLAG_VALUE 0xB0
//  -SETUP_SIZE = 64 bytes  only 4 bytes used NOW...
//
//
//Total 1M bits EEprom size: 131072 bytes
//-------------------------------------------------

#define PTRN_SIZE          (unsigned long)(448)
#define PTRN_OFFSET        (unsigned long)(0)
#define PTRN_TRIG_SIZE     (unsigned long)(32)
#define PTRN_TRIG_OFFSET   (PTRN_OFFSET + PTRN_TRIG_SIZE)
#define PTRN_SETUP_SIZE    (unsigned long)(32)
#define PTRN_SETUP_OFFSET  (PTRN_TRIG_OFFSET + PTRN_SETUP_SIZE)
#define PTRN_EXT_SIZE      (unsigned long)(128)
#define PTRN_EXT_OFFSET    (PTRN_SETUP_OFFSET + PTRN_EXT_SIZE)
#define PTRN_VEL_SIZE      (unsigned long)(256)
#define PTRN_VEL_OFFSET    (PTRN_EXT_OFFSET + PTRN_VEL_SIZE)
#define TRACK_SIZE         (unsigned long)(1024)
#define TRACK_OFFSET       (PTRN_SIZE * MAX_PTRN)
#define OFFSET_SETUP       (TRACK_OFFSET + (TRACK_SIZE * MAX_TRACK))
#define SETUP_SIZE         (unsigned long)(64)
// [TR-909 STYLE] Editable ext track notes live in the unused tail of the setup block.
// Placed at +32 rather than immediately after the 8 setup bytes so the setup record can
// still grow. 73728 is page aligned and MAX_PAGE_SIZE is 64, so 32..47 is one page write.
// A signature byte precedes the notes. Neither erased state can be assumed: an erased
// EEPROM reads 0xFF but InitEEprom() zeroes the device, and 0x00 is a legal MIDI note,
// so a range test alone would silently tune every track to note 0. The signature makes
// "never written" unambiguous and leaves the whole 0..127 range available to the user.
#define EXT_NOTES_OFFSET   (OFFSET_SETUP + 32)
#define EXT_NOTES_SIZE     (unsigned long)(16)
#define EXT_NOTES_SIG      (byte)(0x4E)
#define OFFSET_GROUP       (unsigned long)(37) //save only .groupPos and .groupLength
#define GROUP_SIZE         (unsigned long)(2)
#define MAX_PAGE_SIZE      (unsigned long)(64)
#define HRDW_ADDRESS       0x50
#define HRDW_ADDRESS_UP    (0x50 | B100)
#define DELAY_WR           5

////////////////////////Function//////////////////////

//save pattern
void SavePattern(byte patternNbr)
{
  unsigned long adress = (unsigned long)(PTRN_OFFSET + patternNbr * PTRN_SIZE);
  WireBeginTX(adress); 
  // Serial.println(adress);
  //TRIG-----------------------------------------------
  for (byte i = 0; i < NBR_INST; i++){ 
    byte lowbyte =  (pattern[ptrnBuffer].inst[i] & 0xFF);
    byte highbyte = (pattern[ptrnBuffer].inst[i] >> 8) & 0xFF;
    Wire.write((byte)(lowbyte)); 
    Wire.write((byte)(highbyte));
  }//32 bytes

  //SETUP-----------------------------------------------
  Wire.write((byte)(pattern[ptrnBuffer].length));
  Wire.write((byte)(pattern[ptrnBuffer].scale));
  Wire.write((byte)(pattern[ptrnBuffer].shuffle));
  Wire.write((byte)(pattern[ptrnBuffer].flam));
  // [TR-909] Reuses the slot the stored format reserved for extLength, biased by one so
  // that 0 still means "written before this existed". extLength 0 is itself a legal
  // setting (a one-step ext loop), so the bias is what keeps the full 0-15 range usable.
  Wire.write((byte)(pattern[ptrnBuffer].extLength + 1));
  Wire.write((byte)(pattern[ptrnBuffer].groupPos));
  Wire.write((byte)(pattern[ptrnBuffer].groupLength));
  Wire.write((byte)(pattern[ptrnBuffer].totalAcc));

  for(int a =0; a < 24; a++){
    Wire.write( (byte)(0));//unused parameters
  }//32 bytes

  Wire.endTransmission();//end page transmission
  delay(DELAY_WR);//delay between each write page

  //EXT TRACK----------------------------------------------- [TR-909 STYLE] 32 bytes + 32 padding
  adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + PTRN_SETUP_OFFSET);
  WireBeginTX(adress);
  for (byte i = 0; i < 16; i++) {
    byte lowbyte = (pattern[ptrnBuffer].extTrack[i] & 0xFF);
    byte highbyte = (pattern[ptrnBuffer].extTrack[i] >> 8) & 0xFF;
    Wire.write((byte)(lowbyte));
    Wire.write((byte)(highbyte));
  }
  // [TR-909 STYLE] Velocity levels go in the 32 bytes this page already reserved as
  // padding, so PTRN_SIZE is unchanged and no stored pattern needs migrating.
  //
  // Stored INVERTED. Every pattern written before this existed - and every slot of an
  // EEPROM that InitEEprom() zeroed - has zeros here, and the level those patterns
  // played at was the high one, the only level the ext lane had. Storing the accent mask
  // directly would decode all of them as unaccented and quietly halve the velocity of
  // every existing ext track; storing its complement decodes them as accented, which is
  // what they sounded like. No signature byte needed, and none would fit.
  for (byte i = 0; i < 16; i++) {
    unsigned int stored = ~pattern[ptrnBuffer].extAccent[i];
    Wire.write((byte)(stored & 0xFF));
    Wire.write((byte)((stored >> 8) & 0xFF));
  }
  Wire.endTransmission();
  delay(DELAY_WR);

  // Second 64-byte page (all zeros for compatibility)
  adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * 1) + PTRN_SETUP_OFFSET);
  WireBeginTX(adress);
  for (byte j = 0; j < MAX_PAGE_SIZE; j++) {
    Wire.write((byte)(0));
  }
  Wire.endTransmission();
  delay(DELAY_WR);

  //VELOCITY-----------------------------------------------
  for(int nbrPage = 0; nbrPage < 4; nbrPage++){
      MIDI.read();
    adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_EXT_OFFSET);
    //Serial.println(adress);
    WireBeginTX(adress);
    for (byte i = 0; i < 4; i++){//loop as many instrument for a page
      for (byte j = 0; j < NBR_STEP; j++){
        Wire.write((byte)(pattern[ptrnBuffer].velocity[i + 4*nbrPage][j] & 0xFF)); 
      }
    }
    Wire.endTransmission();//end of 64 bytes transfer
    delay(DELAY_WR);//delay between each write page
  }//4 * 64 bytes = 256 bytes
}

//Load Pattern
void LoadPattern(byte patternNbr)
{
  unsigned long adress = (unsigned long)(PTRN_OFFSET + patternNbr * PTRN_SIZE);
  WireBeginTX(adress); 
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request a 64 bytes page
  //TRIG-----------------------------------------------
  for(int i =0; i<NBR_INST;i++){
    pattern[!ptrnBuffer].inst[i] = (unsigned long)((Wire.read() & 0xFF) | (( Wire.read() << 8) & 0xFF00));
    // Serial.println(Wire.read());
  }
  //SETUP-----------------------------------------------
  pattern[!ptrnBuffer].length = Wire.read();
  pattern[!ptrnBuffer].scale = Wire.read();
  prevShuf = pattern[!ptrnBuffer].shuffle = Wire.read();                                                         // [zabox] [1.027] flam
  prevFlam = pattern[!ptrnBuffer].flam = Wire.read();                                                            // [zabox] [1.027] flam
  byte storedExtLength = Wire.read();  // [TR-909] biased by one; 0 = pattern predates ext length
  pattern[!ptrnBuffer].extLength = storedExtLength ? storedExtLength - 1 : pattern[!ptrnBuffer].length;
  pattern[!ptrnBuffer].groupPos = Wire.read();
  pattern[!ptrnBuffer].groupLength = Wire.read();
  pattern[!ptrnBuffer].totalAcc = Wire.read();
  for(int a = 0; a < 24; a++){
    Wire.read();
  }
  //EXT TRACK----------------------------------------------- [TR-909 STYLE]
  adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + PTRN_SETUP_OFFSET);
  WireBeginTX(adress);
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of 64 bytes

  for (byte i = 0; i < 16; i++) {
    pattern[!ptrnBuffer].extTrack[i] = (unsigned int)((Wire.read() & 0xFF) |
                                                       ((Wire.read() << 8) & 0xFF00));
  }
  // [TR-909 STYLE] Velocity levels, stored inverted in what used to be padding - see
  // SavePattern(): zeros mean a pattern written before this existed, which played at the
  // high level throughout.
  for (byte i = 0; i < 16; i++) {
    unsigned int stored = (unsigned int)((Wire.read() & 0xFF) |
                                          ((Wire.read() << 8) & 0xFF00));
    pattern[!ptrnBuffer].extAccent[i] = ~stored;
  }

  // Skip second 64-byte page (compatibility)
  adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * 1) + PTRN_SETUP_OFFSET);
  WireBeginTX(adress);
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE);
  for (byte j = 0; j < MAX_PAGE_SIZE; j++) {
    Wire.read();
  }
  //VELOCITY-----------------------------------------------
  for(int nbrPage = 0; nbrPage < 4; nbrPage++){
    MIDI.read();
    adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_EXT_OFFSET);
    WireBeginTX(adress);
    Wire.endTransmission();
    Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of  64 bytes
    for (byte i = 0; i < 4; i++){//loop as many instrument for a page
      for (byte j = 0; j < NBR_STEP; j++){
        pattern[!ptrnBuffer].velocity[i + 4*nbrPage][j] = (Wire.read() & 0xFF);
      }
    }
  }
}

//Load Temp Pattern
//[oort] This is a copy of LoadPattern but into another patternbuffer, needed when loading bank into RAM
// would be better to rewrite as one function with pointer adress to buffer as argument
void LoadTempPattern(byte patternNbr)
{
  unsigned long adress = (unsigned long)(PTRN_OFFSET + patternNbr * PTRN_SIZE);
  WireBeginTX(adress); 
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request a 64 bytes page
  //TRIG-----------------------------------------------
  for(int i =0; i<NBR_INST;i++){
    tempPattern.inst[i] = (unsigned long)((Wire.read() & 0xFF) | (( Wire.read() << 8) & 0xFF00));
    // Serial.println(Wire.read());
  }
  //SETUP-----------------------------------------------
  tempPattern.length = Wire.read();
  tempPattern.scale = Wire.read();
  prevShuf = tempPattern.shuffle = Wire.read();                                                         // [zabox] [1.027] flam
  prevFlam = tempPattern.flam = Wire.read();                                                            // [zabox] [1.027] flam
  byte storedTempExtLength = Wire.read();  // [TR-909] biased by one; 0 = pattern predates ext length
  tempPattern.extLength = storedTempExtLength ? storedTempExtLength - 1 : tempPattern.length;
  tempPattern.groupPos = Wire.read();
  tempPattern.groupLength = Wire.read();
  tempPattern.totalAcc = Wire.read();
  for(int a = 0; a < 24; a++){
    Wire.read();
  }
  //EXT TRACK----------------------------------------------- [TR-909 STYLE]
  adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + PTRN_SETUP_OFFSET);
  WireBeginTX(adress);
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of 64 bytes

  for (byte i = 0; i < 16; i++) {
    tempPattern.extTrack[i] = (unsigned int)((Wire.read() & 0xFF) |
                                              ((Wire.read() << 8) & 0xFF00));
  }
  // [TR-909 STYLE] Velocity levels, stored inverted - see SavePattern()
  for (byte i = 0; i < 16; i++) {
    unsigned int stored = (unsigned int)((Wire.read() & 0xFF) |
                                          ((Wire.read() << 8) & 0xFF00));
    tempPattern.extAccent[i] = ~stored;
  }

  // Skip second 64-byte page (compatibility)
  adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * 1) + PTRN_SETUP_OFFSET);
  WireBeginTX(adress);
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE);
  for (byte j = 0; j < MAX_PAGE_SIZE; j++) {
    Wire.read();
  }
  //VELOCITY-----------------------------------------------
  for(int nbrPage = 0; nbrPage < 4; nbrPage++){
    MIDI.read();
    adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_EXT_OFFSET);
    WireBeginTX(adress);
    Wire.endTransmission();
    Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of  64 bytes
    for (byte i = 0; i < 4; i++){//loop as many instrument for a page
      for (byte j = 0; j < NBR_STEP; j++){
        tempPattern.velocity[i + 4*nbrPage][j] = (Wire.read() & 0xFF);
      }
    }
  }
}

//Track save
void SaveTrack(byte trackNbr)
{
  byte lowbyte =  (byte)(track[trkBuffer].length & 0xFF);
  byte highbyte = (byte)((track[trkBuffer].length >> 8) & 0xFF);
  track[trkBuffer].patternNbr[1022] = lowbyte;
  track[trkBuffer].patternNbr[1023] = highbyte;

  unsigned long adress;
  for(unsigned int nbrPage = 0; nbrPage < TRACK_SIZE/MAX_PAGE_SIZE; nbrPage++){
    adress = (unsigned long)(PTRN_OFFSET + (trackNbr * TRACK_SIZE) + (MAX_PAGE_SIZE * nbrPage) + TRACK_OFFSET);
    WireBeginTX(adress);
    for (byte i = 0; i < MAX_PAGE_SIZE; i++){//loop as many instrument for a page
      Wire.write((byte)(track[trkBuffer].patternNbr[i + (MAX_PAGE_SIZE * nbrPage)] & 0xFF)); 
    }
    Wire.endTransmission();//end of 64 bytes transfer
    delay(DELAY_WR);//delay between each write page
  }
}

//Load track
void LoadTrack(byte trackNbr)
{
  unsigned long adress;
  for(unsigned int nbrPage = 0; nbrPage < TRACK_SIZE/MAX_PAGE_SIZE; nbrPage++){
    adress = (unsigned long)(PTRN_OFFSET + (trackNbr * TRACK_SIZE) + (MAX_PAGE_SIZE * nbrPage) + TRACK_OFFSET);
    WireBeginTX(adress);
    Wire.endTransmission();
    if (adress > 65535) Wire.requestFrom(HRDW_ADDRESS_UP,MAX_PAGE_SIZE); //request of  64 bytes
    else Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of  64 bytes
    for (byte i = 0; i < MAX_PAGE_SIZE; i++){//loop as many instrument for a page
      track[trkBuffer].patternNbr[i + (MAX_PAGE_SIZE * nbrPage)] = (Wire.read() & 0xFF); 
    }
  }
  byte lowbyte = (byte)track[trkBuffer].patternNbr[1022];
  byte highbyte = (byte)track[trkBuffer].patternNbr[1023];
  track[trkBuffer].length =  (unsigned long)(lowbyte | highbyte << 8);
}

//Save Setup
void SaveSeqSetup()
{
  unsigned long adress = (unsigned long)(OFFSET_SETUP);
  WireBeginTX(adress);
  Wire.write((byte)(seq.sync)); 
  Wire.write((byte)(seq.defaultBpm));
  Wire.write((byte)(seq.TXchannel));
  Wire.write((byte)(seq.RXchannel));
  Wire.write((byte)(seq.ptrnChangeSync));
  Wire.write((byte)(seq.muteModeHH));                                  // [zabox]
#if MIDI_EXT_CHANNEL
  Wire.write((byte)(seq.EXTchannel));  // [Neuromancer]
#endif
#if CONFIG_BOOTMODE
  Wire.write((byte)(seq.BootMode)); // [Neuromancer]
#endif
  // [TR-909 STYLE] Ext instrument velocity levels, appended to the setup record. The
  // block is 64 bytes with 8 used, so this needs no new allocation.
  Wire.write((byte)(seq.extVelLow));
  Wire.write((byte)(seq.extVelHigh));
  Wire.endTransmission();//end page transmission
  delay(DELAY_WR);//delay between each write page
}

//Save the editable external track notes [TR-909 STYLE]
void SaveExtTrackNotes()
{
  WireBeginTX(EXT_NOTES_OFFSET);
  Wire.write(EXT_NOTES_SIG);
  for (byte i = 0; i < EXT_NOTES_SIZE; i++) Wire.write((byte)(extTrackNote[i]));
  Wire.endTransmission();
  delay(DELAY_WR);
}

//Load the editable external track notes [TR-909 STYLE]
// Falls back to the default table whenever the signature is absent, so a machine that
// has never saved a note map plays the same pitches as the fixed-table firmware did.
void LoadExtTrackNotes()
{
  WireBeginTX(EXT_NOTES_OFFSET);
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS_UP, (int)(EXT_NOTES_SIZE + 1));

  boolean valid = Wire.available() && ((Wire.read() & 0xFF) == EXT_NOTES_SIG);
  for (byte i = 0; i < EXT_NOTES_SIZE; i++) {
    byte stored = Wire.available() ? (Wire.read() & 0xFF) : 0xFF;
    // A stored byte above 127 means the record is damaged, not merely unwritten
    extTrackNote[i] = (valid && stored <= 127) ? stored : pgm_read_byte(&EXT_TRACK_NOTES[i]);
  }
}

//Load Setup
void LoadSeqSetup()
{
  unsigned long adress = (unsigned long)(OFFSET_SETUP);
  WireBeginTX(adress); 
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS_UP,SETUP_SIZE); //
  seq.sync = (Wire.read() & 0xFF);
  seq.sync = constrain(seq.sync, 0, 2);                                     // [zabox] [1.028] + expander mode
  seq.defaultBpm = (Wire.read() & 0xFF);
  seq.defaultBpm = constrain(seq.defaultBpm, MIN_BPM, MAX_BPM);
  seq.TXchannel = (Wire.read() & 0xFF);
#if MIDI_DRUMNOTES_OUT
  seq.TXchannel = constrain(seq.TXchannel, 0 ,16);
#else  
  seq.TXchannel = constrain(seq.TXchannel, 1 ,16);
#endif
  seq.RXchannel = (Wire.read() & 0xFF);
  seq.RXchannel = constrain(seq.RXchannel, 1 ,16);
  seq.ptrnChangeSync = (Wire.read() & 0xFF);
  seq.ptrnChangeSync = constrain(seq.ptrnChangeSync, 0, 1);
  seq.muteModeHH = (Wire.read() & 0xFF);
  seq.muteModeHH = constrain(seq.muteModeHH, 0, 1);                       // [zabox]
#if MIDI_EXT_CHANNEL  
  seq.EXTchannel = (Wire.read() & 0xFF);
  seq.EXTchannel = constrain(seq.EXTchannel, 1 ,16);
#endif
#if CONFIG_BOOTMODE
  unsigned int bootmode = (Wire.read() & 0xFF);
  seq.BootMode = (SeqMode)constrain(bootmode, 0 ,4);
#endif
  // [TR-909 STYLE] Ext velocity levels. No signature needed to spot a setup record
  // written before they existed: those bytes are 0 (InitEEprom zeroes the device) or
  // 0xFF (never written), and neither is a legal level - a note-on with velocity 0 is a
  // note-off, and 0xFF is outside the 7-bit range. Either falls back to the pair the
  // firmware used before the levels were adjustable.
  byte storedExtVelLow = (Wire.read() & 0xFF);
  byte storedExtVelHigh = (Wire.read() & 0xFF);
  seq.extVelLow = (storedExtVelLow >= 1 && storedExtVelLow <= MAX_VEL)
                    ? storedExtVelLow : MIDI_LOW_VELOCITY;
  seq.extVelHigh = (storedExtVelHigh >= 1 && storedExtVelHigh <= MAX_VEL)
                     ? storedExtVelHigh : MIDI_HIGH_VELOCITY;
}


//[oort] These handles group info, nothing else, no pattern data
//saving of groups is not supported in this firmware so commented out
/*
void SavePatternGroup(byte firstPattern, byte length)
{
  for (int a = 0; a <= length ; a++){
    unsigned long adress = (unsigned long)(PTRN_OFFSET + ((firstPattern + a) * PTRN_SIZE) + OFFSET_GROUP);
    WireBeginTX(adress);
    * Serial.print("adresse=");  //[oort] nested block comments not allowed in Arduino, old block comment
     Serial.println(adress);
     Serial.print("groupPos=");
     Serial.println(a);
     Serial.print("groupLeght=");
     Serial.println(length);*   //[oort] end old comment block

    Wire.write((byte)(a)); //pattern[ptrnBuffer].groupPos
    Wire.write((byte)(length));//pattern[ptrnBuffer].groupLength
    Wire.endTransmission();//end page transmission
    delay(DELAY_WR);//delay between each write page
  }
}

//Clear pattern group
void ClearPatternGroup(byte firstPattern, byte length)
{
  for (int a = 0; a <= length ; a++){
    unsigned long adress = (unsigned long)(PTRN_OFFSET + ((firstPattern + a) * PTRN_SIZE) + OFFSET_GROUP);
    WireBeginTX(adress);
    Wire.write((byte)(0)); //pattern[ptrnBuffer].groupPos
    Wire.write((byte)(0));//pattern[ptrnBuffer].groupLength
    Wire.endTransmission();//end page transmission
    delay(DELAY_WR);//delay between each write page
  }
}

//Load pattern group type => POSITION = 0 or LENGTH = 1
byte LoadPatternGroup(byte patternNum, byte type)
{
  unsigned long adress = (unsigned long)(PTRN_OFFSET + (patternNum * PTRN_SIZE) + OFFSET_GROUP + type);
  WireBeginTX(adress);
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,(unsigned long)(1)); //
  byte data = (Wire.read() & 0xFF);
  return data;
}

//...mark (group functionality)
*/

//==================================================================================================

//==================================================================================================
//init pattern in the EEprom
void InitEEprom()
{
  unsigned long adress;
  
  //Pattern init
  for (byte nbrPattern = 0; nbrPattern < MAX_PTRN ; nbrPattern++)
  {
    adress = (unsigned long)(PTRN_OFFSET + nbrPattern * PTRN_SIZE);
    WireBeginTX(adress); 
    // Serial.println(adress);
    //TRIG-----------------------------------------------
    for (byte i = 0; i < NBR_INST; i++){ 
      Wire.write((byte)(0)); 
      Wire.write((byte)(0));
    }//32 bytes

    //SETUP-----------------------------------------------
    Wire.write((byte)(DEFAULT_LEN - 1));
    Wire.write((byte)(DEFAULT_SCALE));
    Wire.write((byte)(DEFAULT_SHUF));
    Wire.write((byte)(DEFAULT_FLAM));                                      // [1.028] flam
    Wire.write((byte)(0));
    Wire.write((byte)(0));
    Wire.write((byte)(0));
    Wire.write((byte)(0));
    for(int a = 0; a < 24; a++){
      //Wire.write( (byte)(0));//unused parameters
    }//32 bytes

    Wire.endTransmission();//end page transmission
    delay(DELAY_WR);//delay between each write page

    //EXT INST-----------------------------------------------
    for(int nbrPage = 0; nbrPage < 2; nbrPage++){
      adress = (unsigned long)(PTRN_OFFSET + (nbrPattern * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_SETUP_OFFSET);
      //Serial.println(adress);
      WireBeginTX(adress);
      for (byte j = 0; j < MAX_PAGE_SIZE; j++){
        Wire.write((byte)(0));
      }//64 bytes -> fisrt page
      Wire.endTransmission();//end page transmission
      delay(DELAY_WR);//delay between each write page
    }//2 * 64 bytes = 128 bytes

    //VELOCITY-----------------------------------------------
    for(int nbrPage = 0; nbrPage < 4; nbrPage++){
      adress = (unsigned long)(PTRN_OFFSET + (nbrPattern * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_EXT_OFFSET);
      //Serial.println(adress);
      WireBeginTX(adress);
      for (byte i = 0; i < 4; i++){//loop as many instrument for a page
        for (byte j = 0; j < NBR_STEP; j++){
          Wire.write((byte)(0)); 
        }
      }
      Wire.endTransmission();//end of 64 bytes transfer
      delay(DELAY_WR);//delay between each write page
    }//4 * 64 bytes = 256 bytes
    static unsigned int tempInitLeds;
    tempInitLeds |= bitSet(tempInitLeds, (nbrPattern / 8));
    SetDoutLed(tempInitLeds, 0, 0);
    //delay(10);
  }
 /* Serial.print("patrn offset add=");
  Serial.println(adress);*/
  
  //Track Init
  for (unsigned long trackNbr = 0; trackNbr < MAX_TRACK; trackNbr++){
    for(unsigned long nbrPage = 0; nbrPage < (TRACK_SIZE/MAX_PAGE_SIZE); nbrPage++){// to init 1024 byte track size we need 16 pages of 64 bytes
      adress = (unsigned long)(PTRN_OFFSET + (trackNbr * TRACK_SIZE) + (MAX_PAGE_SIZE * nbrPage) + TRACK_OFFSET);
      WireBeginTX(adress);
      for (byte i = 0; i < MAX_PAGE_SIZE; i++){//loop as many instrument for a page
          Wire.write((byte)(0));
      }
      Wire.endTransmission();//end of 64 bytes transfer
      delay(DELAY_WR);//delay between each write page
    }
    static unsigned int tempInitLeds;
    tempInitLeds |= bitSet(tempInitLeds, trackNbr);
    SetDoutLed(tempInitLeds, 0, 0);
   //delay(10);
  }
 /* Serial.print("track offset add=");
  Serial.println(TRACK_OFFSET);*/
  
  //Setup init
  adress = (unsigned long)(OFFSET_SETUP);
  WireBeginTX(adress);
  Wire.write((byte)(MASTER));//seq.sync)); 
  Wire.write((byte)(DEFAULT_BPM));//seq.defaultBpm));
  Wire.write((byte)(1));//seq.TXchannel));
  Wire.write((byte)(1));//seq.RXchannel));
  Wire.write((byte)(1));//seq.ptrnChangeSync));
  Wire.write((byte)(1));//seq.muteModeHH));  
#if MIDI_EXT_CHANNEL
  Wire.write((byte)(2));//seq.EXTchannel);
#endif
#if CONFIG_BOOTMODE
  Wire.write((byte)(SeqMode::PTRN_STEP)); // Bootmode
#endif   
  Wire.endTransmission();//end page transmission
  delay(DELAY_WR);//delay between each write page
  /*Serial.print("setup offset add=");
  Serial.println((unsigned long)(TRACK_OFFSET + (TRACK_SIZE * MAX_TRACK)));*/
}

//wire begin
void WireBeginTX(unsigned long address)
{
  byte hardwareAddress;
  if (address > 65535) hardwareAddress = HRDW_ADDRESS_UP;
  else hardwareAddress = HRDW_ADDRESS;
  
  Wire.beginTransmission(hardwareAddress);
  Wire.write((byte)(address >> 8));
  Wire.write((byte)(address & 0xFF));
}

// Helper function to write a single byte to EEPROM
void EEpromByte(unsigned long address, byte data) 
{
  WireBeginTX(address);
  Wire.write(data);
  Wire.endTransmission();
  delay(DELAY_WR);
}

// Helper function to read a single byte from EEPROM
byte EEpromByteRead(unsigned long address) 
{
  WireBeginTX(address);
  Wire.endTransmission();
  
  byte hardwareAddress;
  if (address > 65535) hardwareAddress = HRDW_ADDRESS_UP;
  else hardwareAddress = HRDW_ADDRESS;
  
  Wire.requestFrom(hardwareAddress, (unsigned long)(1));
  return Wire.read() & 0xFF;
}

// Set bootloader flag in EEPROM
void SetBootloaderFlag() 
{
  EEpromByte(BOOTLOADER_FLAG_ADDR, BOOTLOADER_FLAG_VALUE);
}

// Check bootloader flag in EEPROM
byte CheckBootloaderFlag() 
{
  byte flag = EEpromByteRead(BOOTLOADER_FLAG_ADDR);
  // Clear the flag after reading it
  if (flag == BOOTLOADER_FLAG_VALUE) {
    EEpromByte(BOOTLOADER_FLAG_ADDR, 0);
  }
  return flag == BOOTLOADER_FLAG_VALUE;
}
