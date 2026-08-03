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

// Move to a config page. Every path that changes the page goes through here so the
// per-page bookkeeping cannot drift between the TEMPO cycle and the step buttons.
void SetConfigPage(byte page)
{
  seq.configPage = page;
  // Pages do not all have the same number of fields, so a cursor left where the
  // previous page had one would sit on a column the encoder does not edit.
  curIndex = 0;
#if MIDI_HAS_SYSEX
  if (seq.configPage == CONF_PAGE_SYSEX) seq.setupNeedSaved = FALSE;  //only if sysex
#endif
  needLcdUpdate = TRUE;
}

// Step buttons 1..MAX_CONF_PAGE select a config page directly, alongside the TEMPO
// cycle. Those are exactly the step LEDs config mode already lights to advertise the
// available pages, so the panel was already showing the mapping - it just could not be
// pressed. Buttons past the last page are ignored rather than wrapped: they are dark,
// and wrapping would land on a page the user did not aim at.
//
// Edges come from a local mask rather than stepsBtn.justPressed, which is the OR of all
// sixteen buttons and reports no edge for a second step pressed while the first is held.
void ConfigPageButtons()
{
  static unsigned int prevPageBtns;
  unsigned int pageBtns = StepButtonGet(MOMENTARY);

  // A held qualifier means the step buttons belong to that combination (SHIFT+TEMPO
  // paging, INST select, BANK, LAST STEP, SHUFFLE), not to page selection. Tracking the
  // mask while suppressed keeps a release-into-press from registering afterwards.
  if (!seq.configMode || shiftBtn || instBtn || tempoBtn.pressed || bankBtn.pressed
      || numBtn.pressed || lastStepBtn.pressed || shufBtn.pressed) {
    prevPageBtns = pageBtns;
    return;
  }

  unsigned int justPressed = pageBtns & ~prevPageBtns;
  prevPageBtns = pageBtns;

  for (byte a = 0; a < MAX_CONF_PAGE; a++) {
    if (bitRead(justPressed, a)) {
      SetConfigPage(a + 1);
      break;
    }
  }
}

void SeqConfiguration()
{
  if(seq.syncChanged){
    SetSeqSync();
    seq.syncChanged = FALSE;
  }

  // ENTER on the SysEx page transmits a dump, so it must not be read as a save request
  // there. Every other page saves on ENTER - including the ext velocity page, which is
  // where the literal 3 this used to test now points.
#if MIDI_HAS_SYSEX
  if (seq.setupNeedSaved && enterBtn.justPressed && seq.configPage != CONF_PAGE_SYSEX ){
#else
  if (seq.setupNeedSaved && enterBtn.justPressed ){
#endif
    SaveSeqSetup();
    seq.setupNeedSaved = FALSE;
    LcdPrintSaved();
  }

#if MIDI_HAS_SYSEX
  // Transmit Midi System Exclusive
  if ( seq.configMode && seq.configPage == CONF_PAGE_SYSEX && enterBtn.justPressed )
  {
    MidiSendSysex(sysExDump, sysExParam);
  }
#endif

  // Bootloader mode activation - the last config page in either build
  if (seq.configMode && seq.configPage == CONF_PAGE_BOOT) {
    if (encBtn.justPressed) {
      EnterBootloaderMode();
    }
  }

  if (!seq.configMode) seq.setupNeedSaved = FALSE;

#if MIDI_HAS_SYSEX
  if ( seq.configPage == CONF_PAGE_SYSEX)
  {
    if ( seq.SysExMode == false )
    {
      EnableSysexMode();
    }
  } else {
    if ( seq.SysExMode == true )
    {
      DisableSysexMode();  // reloads the bank if a host wrote to EEPROM
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
