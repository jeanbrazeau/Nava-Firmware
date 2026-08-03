//-------------------------------------------------
//                  NAVA v1.x
//                 SEQ configuration
//-------------------------------------------------

/////////////////////Function//////////////////////
// Hand the machine to the resident bootloader.
//
// This is a one-way door. The jump does not return and nothing after it can execute, so
// this function is solely responsible for leaving the unit in a state a power cycle can
// recover from. It works by:
// 1. Stopping the transport and silencing anything sounding, external notes included
// 2. Committing every unsaved edit - pattern bank, track, setup, ext note map
// 3. Stopping the timers, then quiescing the trigger and LED shift registers
// 4. Disabling interrupts and jumping
//
// ON THE JUMP ADDRESS, which is deliberately left as it was: avr-as halves the operand,
// so `jmp 0x1F000` lands on word 0xF800. Boot sections on this part start at byte 0x1FC00
// (512 words), 0x1F800 (1024), 0x1F000 (2048) and 0x1E000 (4096), so this address is
// correct for BOOTSZ=01 and wrong for the other three. Which one applies depends on the
// BOOTSZ fuses of the individual unit, and nothing in this repository records them - the
// released Nava0tone_0.90b.syx carries its own loader at 0x1FE00, which is a fourth answer
// again. Reading hfuse off a unit (avrdude -c usbasp -p m1284p -U hfuse:r:-:h) settles
// both this and whether BOOTRST makes a power cycle the real entry. Changing the address
// on any weaker evidence would only be a differently-sourced guess.
//
// noinline: this is called from exactly one place, on a page the sequencer cannot be
// running on, and it never returns - but GCC inlines it into SeqConfiguration(), which
// runs every pass of loop(). Inlining a function this size into the hot path shifted the
// sequencer's measured bar period by ~370 cycles (sim/tests/test_timing.c bounds it at
// 256), for code that executes at most once in the life of a power-on.
__attribute__((noinline)) void EnterBootloaderMode() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Saving...       ");
  lcd.setCursor(0,1);
  lcd.print("                ");

  // Stop before saving, not after: the EEPROM writes below block for milliseconds per
  // page, and a running sequencer would keep triggering voices and emitting MIDI for the
  // whole of it, with no note-off ever following.
  if (isRunning) {
    isRunning = FALSE;
    isStop = TRUE;
    if (seq.sync == MASTER) MIDI.sendRealTime(midi::MidiType::Stop);
    DIN_START_LOW;                 // was left asserted HIGH across the jump
    dinStartState = LOW;
  }
  // Not gated on isRunning: an editor preview or an audition can be sounding while the
  // transport is stopped, and it has no note-off after this point either.
  InitMidiNoteOff();
  SendAllNoteOff();

  // patternBank[] is authoritative over EEPROM until ENTER or a bank change, so without
  // this the press discarded up to a full bank of edits. The PlatformIO build happened to
  // survive because the page walk transits the SysEx page, where EnableSysexMode() flushes;
  // the Arduino IDE build has no SysEx page and lost the lot. Track, setup and the ext note
  // map have the same exposure and nothing else on this path commits them.
  if (patternBankNeedsSave) FlushPatternBank();
  if (trackNeedSaved) {
    SaveTrack(trk.current);
    trackNeedSaved = FALSE;
  }
  if (seq.setupNeedSaved) {
    SaveSeqSetup();
    seq.setupNeedSaved = FALSE;
  }
  if (extNotesNeedSaved) {
    SaveExtTrackNotes();
    extNotesNeedSaved = FALSE;
  }

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Entering        ");
  lcd.setCursor(0,1);
  lcd.print("Bootloader Mode ");
  delay(1000);

  MIDI.turnThruOff();

  // Timers must stop BEFORE the trigger word is cleared. Timer2 (trig off) and Timer3
  // (flam) both re-write the trigger shift register from their ISRs, so a step that fired
  // within the last 2ms re-asserts it in the gap and cli() then freezes a trigger line
  // high - the open-trigger state setup() records as making the BD oscillate.
  TimerStop();
  TIMSK2 = 0;
  TIMSK3 = 0;
  SetDoutTrig(0);
  SetDoutLed(0, 0, 0);

  cli();

  // One jump, and nothing after it. The ladder this replaces tried four addresses in
  // sequence, commented "if we're still here, try...", which an AVR jmp cannot do - it is
  // unconditional and does not return, so the three further jumps, the EEPROM flag write
  // and the watchdog reset were all unreachable. Deleting them changes no behaviour; it
  // removes ~70 bytes that documented three safety nets none of which existed at runtime.
  // A fallback, if one is ever wanted, has to be a watchdog reset armed BEFORE the jump.
  asm volatile ("jmp 0x1F000\n");
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

  // Bootloader mode activation - the last config page in either build.
  //
  // SHIFT qualifies the press. A bare encoder press is what advances the cursor between
  // fields on every other config page, including CONF_PAGE_EXT_VEL immediately before this
  // one, so the same finger motion that means "next field" one page back meant "leave the
  // firmware" here - and with ConfigPageButtons() a single step button now lands on this
  // page directly. SHIFT+ENC is used nowhere else, so it cannot be reached by that habit,
  // and it matches the qualified-gesture idiom the panel already uses for destructive
  // operations (SHIFT+CLEAR to clear, PLAY+STOP held at boot to init EEPROM). The user is
  // already holding SHIFT to page here, so it costs nothing to perform.
  //
  // !isRunning is belt and braces. SeqParameter() closes config mode when the transport
  // starts, but it runs AFTER this function in loop(), leaving a one-pass window in which
  // a running sequencer could still be sitting on an armed page.
  if (seq.configMode && seq.configPage == CONF_PAGE_BOOT && !isRunning) {
    if (shiftBtn && encBtn.justPressed) {
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
