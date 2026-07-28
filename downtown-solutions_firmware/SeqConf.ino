//-------------------------------------------------
//                  NAVA v1.x
//                 SEQ configuration
//-------------------------------------------------

/////////////////////Function//////////////////////
// Function to enter bootloader mode for ATmega1284
// Different bootloader implementations may require different addresses or methods
void EnterBootloaderMode() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Entering        ");
  lcd.setCursor(0,1);
  lcd.print("Bootloader Mode ");

  // Wait a moment to display the message
  delay(1000);
  
  // Finish all pending operations
  SetDoutTrig(0);
  SetDoutLed(0, 0, 0);
  
  // Ensure any communication is complete
  Wire.endTransmission(true);
  MIDI.turnThruOff();
  
  /* IMPORTANT: For ATmega1284p, bootloader sections can be at different addresses
   * based on BOOTSZ fuse bits:
   * BOOTSZ1=1, BOOTSZ0=1: 512 words (1024 bytes),  Start address: 0x1FE00
   * BOOTSZ1=1, BOOTSZ0=0: 1024 words (2048 bytes), Start address: 0x1FC00
   * BOOTSZ1=0, BOOTSZ0=1: 2048 words (4096 bytes), Start address: 0x1F800
   * BOOTSZ1=0, BOOTSZ0=0: 4096 words (8192 bytes), Start address: 0x1F000
   */
  
  // Disable interrupts to prevent any interference
  cli();
  
  // We will try several approaches, in order of most direct to least:
  
  // Method 1: Directly set a special signature and jump to the bootloader address
  // This is the method used by many bootloaders like Optiboot
  
  // Define jump function signature
  // void (*bootloader)(void) = (void (*)(void))0x1F000; // 4K bootloader
  
  // Try multiple possible bootloader addresses:
  // Starting with the 4K bootloader (0x1F000)
  asm volatile (
    "jmp 0x1F000\n"
  );
  
  // If we're still here, try 2K bootloader (0x1F800)
  asm volatile (
    "jmp 0x1F800\n"
  );
  
  // If we're still here, try 1K bootloader (0x1FC00)
  asm volatile (
    "jmp 0x1FC00\n"
  );
  
  // If we're still here, try 512 byte bootloader (0x1FE00)
  asm volatile (
    "jmp 0x1FE00\n"
  );
  
  // Method 2: Set specific registers and the EEPROM flag
  // that the bootloader might check after reset
  SetBootloaderFlag();
  
  // Method 3: Hardware reset via watchdog
  // Most bootloaders check certain conditions after reset
  MCUSR = 0; // Clear all reset flags
  WDTCSR = (1<<WDCE) | (1<<WDE); // Enable watchdog change
  WDTCSR = (1<<WDE); // Set shortest timeout (16ms)
  
  // Wait for watchdog reset
  while(1) {
    // asm volatile("nop"); // Do nothing
  }
}

void SeqConfiguration()
{
  if(seq.syncChanged){
    SetSeqSync();
    seq.syncChanged = FALSE;
  }

  if (seq.setupNeedSaved && enterBtn.justPressed && seq.configPage != 3 ){
    SaveSeqSetup();
    seq.setupNeedSaved = FALSE;
    LcdPrintSaved();
  }

#if MIDI_HAS_SYSEX
  // Transmit Midi System Exclusive
  if ( seq.configMode && seq.configPage == 3 && enterBtn.justPressed )
  {
    MidiSendSysex(sysExDump, sysExParam);
  }
#endif

#if MIDI_HAS_SYSEX
  // Bootloader mode activation - only on page 4 when MIDI_HAS_SYSEX is defined
  if (seq.configMode && seq.configPage == 4) {
    // This is the bootloader config page
    if (encBtn.justPressed) {
      EnterBootloaderMode();
    }
  }
#else
  // Bootloader mode activation - on page 3 when MIDI_HAS_SYSEX is not defined
  if (seq.configMode && seq.configPage == 3) {
    // This is the bootloader config page
    if (encBtn.justPressed) {
      EnterBootloaderMode();
    }
  }
#endif

  if (!seq.configMode) seq.setupNeedSaved = FALSE;

#if MIDI_HAS_SYSEX
  if ( seq.configPage == 3)
  {
    if ( seq.SysExMode == false )
    {
      EnableSysexMode();
    }
  } else {
    if ( seq.SysExMode == true )
    {
      seq.SysExMode = false;
      SetSeqSync();
    }
  }
#endif
}

void SetSeqSync() 
{
  //Sync configuration
  switch (seq.sync){                             // [zabox] [1.028] added expander mode
  case MASTER: 
    initTrigTimer();                          
    DisconnectMidiHandleRealTime();
    DisconnectMidiHandleNote();
#if MIDI_HAS_SYSEX    
    DisconnectMidiSysex();
#endif    
    TimerStart();//cf timer
    break;
  case SLAVE:
    TimerStop();
    initTrigTimer();
#if MIDI_BANK_PATTERN_CHANGE    
    ConnectMidiHandleNote(); // Connects Notes but ignores drum notes.                       
#else    
    DisconnectMidiHandleNote();
#endif    
    ConnectMidiHandleRealTime();
#if MIDI_HAS_SYSEX    
    DisconnectMidiSysex();
#endif    
    break;
  case EXPANDER:
    TimerStop();
    initExpTimer();                 
    DisconnectMidiHandleRealTime();
    ConnectMidiHandleNote();
#if MIDI_HAS_SYSEX    
    DisconnectMidiSysex();
#endif    
    stepLeds = 0;
    configLed = 0;
    menuLed = 0;
    break;
  }
}
