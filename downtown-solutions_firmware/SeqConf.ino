//-------------------------------------------------
//                  NAVA v1.x
//                 SEQ configuration
//-------------------------------------------------

/////////////////////Function//////////////////////
// EnterBootloaderMode() and the BOOTLOADER config page it was reached from are removed.
// See the config-page block in define.h for why, and README.md for what to do instead.

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
