//-------------------------------------------------
//                  NAVA v1.x
//              Midi Function
//-------------------------------------------------

/////////////////////Function//////////////////////

// Set while an external-instrument transmission is in progress. CountPPQN() can reach
// the transmitter at interrupt priority and land in the middle of one; the MIDI
// library's running-status state, HardwareSerial's TX ring and extTrackNoteOn[] are
// all non-reentrant. The clock-side path stands down when this is set and leaves the
// step queued for the loop, which is what the loop-only design did anyway.
static volatile boolean extMidiBusy = FALSE;

//initialize midi real time variable
void InitMidiRealTime() {
  midiStart = LOW;
  midiStop = LOW;
  midiContinue = LOW;
}

// External track notes are transmitted verbatim [TR-909 STYLE]
// MidiSendNoteOn/Off add 12 for the drum path. The ext path must not, now that the
// user sets the note directly: what the encoder shows, the LCD prints and the wire
// carries have to be the same number. The defaults in EXT_TRACK_NOTES absorb the
// offset so the transmitted pitches are unchanged from the fixed-table firmware.
void MidiSendExtNoteOn(byte note, byte velocity) {
#if MIDI_EXT_CHANNEL
  MIDI.sendNoteOn(note, velocity, seq.EXTchannel);
#else
  MIDI.sendNoteOn(note, velocity, seq.TXchannel);
#endif
}

// Sent as note-on with velocity 0, not as an 0x8n note-off. The two are equivalent
// on every receiver (the running-status convention is built on this, and the
// firmware's own MySettings enables HandleNullVelocityNoteOnAsNoteOff on input),
// but an 0x8n message forces a status-byte change between the note-offs and the
// note-ons of a step. Keeping one 0x9n status for the whole step lets running
// status carry it, which removes a byte per message from the critical path
// between the analog trigger and the first ext note reaching the wire.
void MidiSendExtNoteOff(byte note) {
#if MIDI_EXT_CHANNEL
  MIDI.sendNoteOn(note, 0, seq.EXTchannel);
#else
  MIDI.sendNoteOn(note, 0, seq.TXchannel);
#endif
}

//send note off for every external track currently sounding [TR-909 STYLE]
void SendExtTrackNoteOff() {
  if (midiNoteOnActive) {
    // Turn off all active external track notes
    for (byte track = 0; track < 16; track++) {
      if (extTrackNoteOn[track]) {
        // The recorded note, not the current map entry: retuning a sounding track
        // would otherwise note-off a pitch that was never started.
        MidiSendExtNoteOff(extSoundingNote[track]);
        extTrackNoteOn[track] = FALSE;
      }
    }
    midiNoteOnActive = FALSE;
  }
}

//intialize note off when stop or patternChanged [TR-909 STYLE]
void InitMidiNoteOff() {
  // Stopping or changing pattern also cancels a step CountPPQN queued but the loop
  // has not transmitted yet, otherwise those notes would sound after the stop
  // Claiming the transmitter with the same flag the clock path respects keeps a step
  // that arrives mid-silencing from interleaving its note-ons into these note-offs and
  // leaving the external synth holding a note the stop was supposed to end.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    extPendingOn = 0;
    extPendingOff = FALSE;
    // Nothing is left sounding after this, so a pending early release would only
    // re-latch the transmitter for an empty pass.
    extReleaseArmed = FALSE;
    extMidiBusy = TRUE;
  }
  SendExtTrackNoteOff();
  extMidiBusy = FALSE;
}

//drain the external instrument notes queued by CountPPQN [TR-909 STYLE]
// The fallback path. ServiceExtMidiNotesFromClock() transmits most steps the moment the
// clock produces them; this catches the ones it declined - a step too dense for the UART
// TX ring, or one that arrived while the transmitter was claimed - where the cost of
// blocking on the wire lands in the loop and can be absorbed. The queue is latched and
// cleared under one short critical section; transmission happens outside it. A step
// queued between the latch and the sends simply waits for the next drain, which is why
// the note-off pass uses the latched flag instead of InitMidiNoteOff() - that would
// discard the newer step.
void ServiceExtMidiNotes() {
  unsigned int noteOnMask;
  unsigned int accentMask;
  byte velocity;
  byte accentVelocity;
  boolean noteOffDue;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    noteOnMask = extPendingOn;
    accentMask = extPendingAcc;
    velocity = extPendingVel;
    accentVelocity = extPendingVelAcc;
    noteOffDue = extPendingOff;
    extPendingOn = 0;
    extPendingOff = FALSE;
    // Claimed inside the same critical section as the latch, not after it: a step the
    // clock queues in between would otherwise find the transmitter free, send itself
    // immediately, and land ahead of the older step latched here.
    // Claimed only when there is something to send, so an empty poll - the common case
    // once the clock path is doing the work - never blocks the clock out.
    if (noteOnMask || noteOffDue) extMidiBusy = TRUE;
  }
  if (!noteOnMask && !noteOffDue) return;

  ExtTransmitStep(noteOnMask, accentMask, velocity, accentVelocity, noteOffDue);
  extMidiBusy = FALSE;
}

// Transmit the step CountPPQN() has just queued, from the clock itself, when doing so
// cannot block.
//
// Waiting for the loop does not cost a steady offset - it costs jitter. A loop pass
// that repaints the LCD blocks for ~14ms while a pass that does not costs ~0.2ms, and
// the repaints are triggered at end of measure, so the worst delay lands on the bar's
// downbeat: the most audible step in the pattern.
//
// The reason the original design deferred to the loop still holds and is respected
// here rather than discarded: HardwareSerial::write() busy-waits on UDRE with
// interrupts disabled once its 64 byte TX ring fills, and a maximally dense step is
// 32 messages. So the step is sent here only when the ring is measured to have room
// for the whole worst-case burst; anything larger stays queued and the loop drains it,
// where the cost is absorbable. Nothing on this path can ever wait on the wire.
void ServiceExtMidiNotesFromClock() {
  unsigned int noteOnMask = 0;
  unsigned int accentMask = 0;
  byte velocity = 0;
  byte accentVelocity = 0;
  boolean noteOffDue = FALSE;

  // Test, cost and claim in one critical section. CountPPQN() reaches here from the
  // Timer1 ISR when clocking internally - already uninterruptible - but from loop()
  // via HandleClock() when slaved to incoming MIDI clock, where it is not.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!extMidiBusy && (extPendingOn || extPendingOff)) {
      byte messages = 0;
      for (byte track = 0; track < 16; track++) {
        if (bitRead(extPendingOn, track)) messages++;
        if (extPendingOff && extTrackNoteOn[track]) messages++;
      }
      // Two bytes per message plus at most one status byte. Every ext message is a
      // note-on on one channel - releases are note-on velocity 0 - and MySettings
      // forces UseRunningStatus for every build, so the status byte is emitted once
      // and then never again until a SysEx transfer resets it.
      // Budgeting 3 bytes each was pessimistic in the wrong direction: it declined
      // steps that would have fit, handing them to the loop where an end-of-measure
      // LCD repaint blocks ~14ms - 10x the entire wire time of the step it was
      // protecting. The cliff moves from 10 tracks to 15.
      // Two velocity levels do not change the budget: under running status a note-on is
      // two bytes whatever velocity it carries.
      if ((int)messages * 2 + 1 <= Serial1.availableForWrite()) {
        noteOnMask = extPendingOn;
        accentMask = extPendingAcc;
        velocity = extPendingVel;
        accentVelocity = extPendingVelAcc;
        noteOffDue = extPendingOff;
        extPendingOn = 0;
        extPendingOff = FALSE;
        extMidiBusy = TRUE;
      }
    }
  }
  if (!noteOnMask && !noteOffDue) return;

  ExtTransmitStep(noteOnMask, accentMask, velocity, accentVelocity, noteOffDue);
  extMidiBusy = FALSE;
}

// Emit one step's worth of external MIDI, ordered so the first new note reaches the
// wire as early as possible.
//
// The obvious order - every sounding note off, then every new note on - makes the
// delay between the analog trigger and the first ext note scale with how many tracks
// the PREVIOUS step left sounding: with N tracks held over, track 0's note-on is the
// (2N+3)rd byte, and at 320us per byte on a 31250 baud wire that is milliseconds of
// drift against the drum voices, growing with pattern density.
//
// Instead each track's note-off is interleaved with its own note-on, and tracks that
// are only being released (sounding, not in the new mask) are deferred to the end:
//
//   1. per track, in order: its note-off if it is sounding and retriggering,
//      immediately followed by its note-on
//   2. after all note-ons: note-offs for tracks that stop on this step
//
// Track k's note-on then lands at a fixed offset regardless of density, and the
// releases - which are not time-critical, the notes are already sounding - absorb the
// remaining wire time. A retriggering track still gets off-before-on, so no receiver
// sees a doubled note-on or a note-off that arrives after its replacement.
//
// GUIDE is the master enable for sequenced note-ons. Note-offs are unconditional:
// dropping them would strand notes on the external synth.
// accentMask names the tracks of noteOnMask that take accentVelocity instead of
// velocity - the step's second level. Passed as a mask rather than 16 per-track values
// because a step only ever carries the two levels the editor can program.
void ExtTransmitStep(unsigned int noteOnMask, unsigned int accentMask, byte velocity,
                     byte accentVelocity, boolean noteOffDue) {
  if (!guideBtn.counter) noteOnMask = 0;

  for (byte track = 0; track < 16; track++) {
    boolean starting = bitRead(noteOnMask, track);
    if (!starting) continue;

    if (noteOffDue && extTrackNoteOn[track]) {
      // The recorded note, not the current map entry: retuning a sounding track
      // would otherwise note-off a pitch that was never started.
      MidiSendExtNoteOff(extSoundingNote[track]);
      extTrackNoteOn[track] = FALSE;
    }
    byte noteToSend = extTrackNote[track];
    MidiSendExtNoteOn(noteToSend, bitRead(accentMask, track) ? accentVelocity : velocity);
    extSoundingNote[track] = noteToSend;  // Note-off must reuse this exact note
    extTrackNoteOn[track] = TRUE;
    midiNoteOnActive = TRUE;
  }

  if (!noteOffDue) return;

  boolean stillSounding = FALSE;
  for (byte track = 0; track < 16; track++) {
    if (!extTrackNoteOn[track]) continue;
    if (bitRead(noteOnMask, track)) {
      stillSounding = TRUE;  // just restarted above
      continue;
    }
    MidiSendExtNoteOff(extSoundingNote[track]);
    extTrackNoteOn[track] = FALSE;
  }
  midiNoteOnActive = stillSounding;
}

//Send note OFF
void MidiSendNoteOff(byte channel, byte value) {
  MIDI.sendNoteOff(value + 12, 0, channel);
  /* MidiSend(channel + 0x80);
   MidiSend(value);
   MidiSend(0x00);*/
}
//Send note ON
void MidiSendNoteOn(byte channel, byte value, byte velocity) {
  MIDI.sendNoteOn(value + 12, velocity, channel);
  /* MidiSend(channel + 0x90);
   MidiSend(value);
   MidiSend(velocity);*/
}

//Handle clock
void HandleClock() {
  DIN_CLK_HIGH;
  CountPPQN();  //execute 4x because internal sequencer run as 96 ppqn. [oort] comment: These functions will run all four ppqn-counts in a row compared to Master clocking with steady pace.
  CountPPQN();
  //delayMicroseconds(2000);                 // [zabox] [1.028]
  CountPPQN();
  CountPPQN();
  DIN_CLK_LOW;
}


//handle start
void HandleStart() {
  midiStart = HIGH;
}

//handle stop
void HandleStop() {
  midiStop = HIGH;
}

//handle Continue
void HandleContinue() {
  midiContinue = HIGH;
}


//Connect midi real time callback                         // [zabox] [1.028]
void ConnectMidiHandleRealTime() {
  MIDI.setHandleClock(HandleClock);
  MIDI.setHandleStart(HandleStart);
  MIDI.setHandleStop(HandleStop);
  MIDI.setHandleContinue(HandleContinue);

  //[oort] Unimplemented skeleton:
  //MIDI.setHandleSystemReset(HandleReset);  //[oort] prototype idea, button clear is used as reset, we need a midi equivalent
  
  /* [oort] SystemReset is probably not really correct to use maybe use song position pointer == 0 as Clear?  setHandleSongPosition
    We can however not easily use SPP in general since bears can have arbitrary length and hence the notion of "beat" used
    by SPP is invalid.
  */
}

//Disconnect midi real time callback
void DisconnectMidiHandleRealTime() {
  MIDI.disconnectCallbackFromType(midi::MidiType::Clock);
  MIDI.disconnectCallbackFromType(midi::MidiType::Start);
  MIDI.disconnectCallbackFromType(midi::MidiType::Stop);
  MIDI.disconnectCallbackFromType(midi::MidiType::Continue);
  //MIDI.disconnectCallbackFromType(midi::MidiType::SystemReset); //[oort] prototype idea, see above
}

#if MIDI_HAS_SYSEX
//-------------------------------------------------
// [SYSEX] Pattern, track and setup transfer.
//
// Records are the EEPROM images from EEprom.ino sent verbatim, 7-in-8 packed
// (Sysex.h). Both directions stream: a dump is read out of EEPROM 56 bytes at a
// time and pushed straight to the UART, and an incoming record is unpacked into
// one page at a time. Nothing here allocates a record-sized buffer, because a
// track record is 1KB against about 2.5KB of free RAM.
//
// The host counterpart is tools/nava (`nava backup` / `nava restore`), and
// tools/tests/fakenava.py models this side of the exchange.
//-------------------------------------------------

// 8 packed groups. A multiple of 7 so a chunk boundary is never mid-group, and
// well inside WireN's BUFFER_LENGTH of 66.
#define SYSEX_CHUNK 56

boolean sysexWroteEEprom = FALSE;  // a record was stored; the bank cache is stale

// The MIDI library remembers the running status it last transmitted. Writing to
// the UART directly bypasses that bookkeeping, while the F0..F7 just sent has
// cleared the *receiver's* - so the two must be resynchronised or the next
// note-on would go out without its status byte and be read as the wrong message.
// sendSysEx() with a zero-length body and "boundaries already included" emits no
// bytes and invalidates the stored status, which is exactly the reset needed.
void SysexResetRunningStatus() {
  MIDI.sendSysEx(0, NULL, true);
}

unsigned int SysexRecordSize(byte cmd) {
  if (cmd == NAVA_PTRN_DMP || cmd == NAVA_PTRN_REQ) return SYSEX_PTRN_BYTES;
  if (cmd == NAVA_TRACK_DMP || cmd == NAVA_TRACK_REQ) return SYSEX_TRACK_BYTES;
  return SYSEX_CONFIG_BYTES;
}

unsigned long SysexRecordAddress(byte cmd, byte param) {
  if (cmd == NAVA_PTRN_DMP || cmd == NAVA_PTRN_REQ)
    return (unsigned long)(PTRN_OFFSET + (unsigned long)param * PTRN_SIZE);
  if (cmd == NAVA_TRACK_DMP || cmd == NAVA_TRACK_REQ)
    return (unsigned long)(TRACK_OFFSET + (unsigned long)param * TRACK_SIZE);
  return (unsigned long)(OFFSET_SETUP);
}

boolean SysexParamValid(byte cmd, byte param) {
  switch (cmd) {
    case NAVA_PTRN_DMP:
    case NAVA_PTRN_REQ:   return param < MAX_PTRN;
    case NAVA_TRACK_DMP:
    case NAVA_TRACK_REQ:  return param < MAX_TRACK;
    case NAVA_BANK_REQ:   return param <= MAX_BANK;
    default:              return param == 0;
  }
}

// One packed group: a byte of the high bits, then the 7 stripped bytes.
void SysexTxGroup(const byte* group, byte count) {
  Serial1.write(SysexPackMsbs(group, count));
  for (byte i = 0; i < count; i++) Serial1.write((byte)(group[i] & 0x7F));
}

// Stream one EEPROM record out as a complete F0..F7 dump.
void SysexSendRecord(byte cmd, byte param) {
  unsigned long address = SysexRecordAddress(cmd, param);
  unsigned int count = SysexRecordSize(cmd);
  byte buf[SYSEX_CHUNK];
  byte group[7];
  byte inGroup = 0;
  byte sum = 0;

  Serial1.write((byte)START_OF_SYSEX);
  Serial1.write((byte)SYSEX_MANUFACTURER);
  Serial1.write((byte)SYSEX_DEVID_1);
  Serial1.write((byte)SYSEX_DEVID_2);
  Serial1.write(cmd);
  Serial1.write(param);

  unsigned int done = 0;
  while (done < count) {
    byte chunk = (count - done > SYSEX_CHUNK) ? SYSEX_CHUNK : (byte)(count - done);
    EEpromReadBlock(address + done, buf, chunk);
    for (byte i = 0; i < chunk; i++) {
      sum += buf[i];
      group[inGroup++] = buf[i];
      if (inGroup == 7) {
        SysexTxGroup(group, 7);
        inGroup = 0;
      }
    }
    done += chunk;
  }
  // A record whose length is not a multiple of 7 leaves a short final group -
  // the track (1024) and setup (64) records both do.
  if (inGroup) SysexTxGroup(group, inGroup);

  Serial1.write((byte)(sum & 0x7F));
  Serial1.write((byte)END_OF_SYSEX);
  SysexResetRunningStatus();
}

void SysexSendAck(byte status) {
  Serial1.write((byte)START_OF_SYSEX);
  Serial1.write((byte)SYSEX_MANUFACTURER);
  Serial1.write((byte)SYSEX_DEVID_1);
  Serial1.write((byte)SYSEX_DEVID_2);
  Serial1.write((byte)NAVA_ACK);
  Serial1.write(status);
  Serial1.write((byte)0);  // checksum of an empty payload
  Serial1.write((byte)END_OF_SYSEX);
  SysexResetRunningStatus();
}

// Verify an incoming record and commit it to EEPROM, then acknowledge.
void SysexStoreRecord(byte cmd, byte param, const byte* packed,
                      unsigned int packedLen, byte wantSum) {
  unsigned int count = SysexRecordSize(cmd);
  if (SysexUnpackedLen(packedLen) != count) {
    SysexSendAck(NAVA_ACK_BAD_LENGTH);
    return;
  }

  // Checksummed before anything is written. A record that fails midway would
  // otherwise leave half of the old pattern and half of the new one in EEPROM,
  // which is worse than the write not happening and nothing says it occurred.
  byte sum = 0;
  for (unsigned int i = 0; i < count; i++) sum += SysexUnpackByteAt(packed, i);
  if ((byte)(sum & 0x7F) != wantSum) {
    SysexSendAck(NAVA_ACK_BAD_CHECKSUM);
    return;
  }

  unsigned long address = SysexRecordAddress(cmd, param);
  byte page[MAX_PAGE_SIZE];
  unsigned int done = 0;
  while (done < count) {
    byte chunk = (count - done > MAX_PAGE_SIZE) ? (byte)MAX_PAGE_SIZE : (byte)(count - done);
    for (byte i = 0; i < chunk; i++) page[i] = SysexUnpackByteAt(packed, done + i);
    EEpromWriteBlock(address + done, page, chunk);
    done += chunk;
  }

  sysexWroteEEprom = TRUE;
  SysexSendAck(NAVA_ACK_OK);
}

// [SYSEX] Handle incoming system exclusive messages
void HandleSystemExclusive(byte* array, unsigned size) {
  // A message too long for the library's buffer arrives as fragments, the first
  // ending in F0 and the rest starting with F7. Demanding both boundaries drops
  // those instead of decoding a fragment as if it were a record.
  if (size < HEADERSIZE + 2) return;
  if (array[0] != (byte)START_OF_SYSEX || array[size - 1] != END_OF_SYSEX) return;
  if (array[1] != SYSEX_MANUFACTURER) return;
  if (array[2] != SYSEX_DEVID_1 || array[3] != SYSEX_DEVID_2) return;

  byte cmd = array[4];
  byte param = array[5];

  // Never answer our own reply. On a rig with MIDI thru or a loopback the ACK we
  // just sent comes back at us, and answering it - with a bad-parameter ACK, which
  // would come back too - is an exchange that never ends and drowns the port.
  if (cmd == NAVA_ACK) return;

  // EEPROM reads and writes block for milliseconds at a time and the sequencer
  // is driven from an ISR that reads the same pages, so nothing here runs while
  // it is playing. Reaching this state needs the config page, which cannot be
  // opened while running, but a host that got a request in first is answered
  // rather than left waiting for a reply that never comes.
  if (isRunning) {
    SysexSendAck(NAVA_ACK_BUSY);
    return;
  }

  if (!SysexParamValid(cmd, param)) {
    SysexSendAck(NAVA_ACK_BAD_PARAM);
    return;
  }

  switch (cmd) {
    case NAVA_PTRN_REQ:
      SysexSendRecord(NAVA_PTRN_DMP, param);
      break;

    case NAVA_TRACK_REQ:
      SysexSendRecord(NAVA_TRACK_DMP, param);
      break;

    case NAVA_CONFIG_REQ:
      SysexSendRecord(NAVA_CONFIG_DMP, 0);
      break;

    case NAVA_BANK_REQ:
      for (byte i = 0; i < NBR_PATTERN; i++) {
        SysexSendRecord(NAVA_PTRN_DMP, (byte)(param * NBR_PATTERN + i));
      }
      break;

    case NAVA_FULL_REQ:
      for (byte i = 0; i < MAX_PTRN; i++) SysexSendRecord(NAVA_PTRN_DMP, i);
      for (byte i = 0; i < MAX_TRACK; i++) SysexSendRecord(NAVA_TRACK_DMP, i);
      SysexSendRecord(NAVA_CONFIG_DMP, 0);
      break;

    case NAVA_PTRN_DMP:
    case NAVA_TRACK_DMP:
    case NAVA_CONFIG_DMP:
      // size - HEADERSIZE - 2 drops the checksum byte and the F7.
      SysexStoreRecord(cmd, param, array + HEADERSIZE,
                       (unsigned int)(size - HEADERSIZE - 2), array[size - 2]);
      break;

    default:
      SysexSendAck(NAVA_ACK_BAD_PARAM);
      break;
  }
}

// [SYSEX] Send system exclusive message - the config page's ENTER action.
// dumpType follows nameSysex[] in nava_strings.h: 0 bank, 1 pattern, 2 track,
// 3 config.
void MidiSendSysex(byte dumpType, byte param) {
  switch (dumpType) {
    case 0:
      for (byte i = 0; i < NBR_PATTERN; i++) {
        SysexSendRecord(NAVA_PTRN_DMP, (byte)(param * NBR_PATTERN + i));
      }
      break;
    case 1:
      if (param < MAX_PTRN) SysexSendRecord(NAVA_PTRN_DMP, param);
      break;
    case 2:
      if (param < MAX_TRACK) SysexSendRecord(NAVA_TRACK_DMP, param);
      break;
    default:
      SysexSendRecord(NAVA_CONFIG_DMP, 0);
      break;
  }
}

// [SYSEX] Enable sysex mode
void EnableSysexMode() {
  // Pending edits live in patternBank[] and are only written on ENTER or a bank
  // change. Flushing them here makes EEPROM authoritative before a host reads or
  // writes it: otherwise a dump would report stale patterns, and a restore would
  // be silently overwritten the next time the cache was saved.
  FlushPatternBank();
  sysexWroteEEprom = FALSE;
  seq.SysExMode = TRUE;
  ConnectMidiSysex();
}

// [SYSEX] Leave sysex mode, making anything a host wrote visible.
void DisableSysexMode() {
  seq.SysExMode = FALSE;
  DisconnectMidiSysex();
  // Reload so restored patterns take effect without a power cycle; the cache
  // still holds what EEPROM had on the way in.
  if (sysexWroteEEprom) {
    sysexWroteEEprom = FALSE;
    LoadPatternBank(curBank);
    memcpy(&pattern[ptrnBuffer], &patternBank[curPattern - curBank * NBR_PATTERN],
           sizeof(Pattern));
    needLcdUpdate = TRUE;
  }
}

void ConnectMidiSysex() {
  MIDI.setHandleSystemExclusive(HandleSystemExclusive);
}

void DisconnectMidiSysex() {
  MIDI.disconnectCallbackFromType(midi::MidiType::SystemExclusive);
}
#endif

void ConnectMidiHandleNote() {
  MIDI.setHandleNoteOn(HandleNoteOn);
  MIDI.setHandleNoteOff(HandleNoteOff);
}

//Disconnect midi real time callback                      // [zabox] [1.028]
void DisconnectMidiHandleNote() {
  MIDI.disconnectCallbackFromType(midi::MidiType::NoteOn);
  MIDI.disconnectCallbackFromType(midi::MidiType::NoteOff);
}

//Handle noteON
void HandleNoteOn(byte channel, byte pitch, byte velocity) {
  //Midi note On with 0 velocity as Midi note Off
  if (velocity != 0) {
    if (channel == seq.RXchannel) {
      switch (pitch) {
        case 36:
          MidiTrigOn(BD, velocity);
          break;
        case 40:  //GM Electric Snare
          MidiTrigOn(SD, velocity);
          break;
        case 45:
          MidiTrigOn(LT, velocity);
          break;
        case 47:
          MidiTrigOn(MT, velocity);
          break;
        case 48:
          MidiTrigOn(HT, velocity);
          break;
        case 37:
          MidiTrigOn(RM, velocity);
          break;
        case 39:
          MidiTrigOn(HC, velocity);
          break;
        case 42:
          MidiTrigOn(CH, velocity);
          break;
        case 46:
          MidiTrigOn(OH, velocity);
          break;
        case 49:
          MidiTrigOn(CRASH, velocity);
          break;
        case 51:
          MidiTrigOn(RIDE, velocity);
          break;
        case 60:
          TRIG_HIGH;
          break;
#if MIDI_BANK_PATTERN_CHANGE
        case 61:
        case 62:
        case 63:
        case 64:
        case 65:
        case 66:
        case 67:
        case 68:
          // Bank Select
          if (curSeqMode == PTRN_PLAY) {
            curBank = pitch - 61;
            nextPattern = curBank * NBR_PATTERN + (curPattern % NBR_PATTERN);
            if (curPattern != nextPattern) {
              LoadPatternBank(curBank);
              selectedPatternChanged = TRUE;
              needLcdUpdate = TRUE;
              group.length = 0;  //[oort] Pattern groups/chains are suspended when changing bank
              //              group.pos = 0; //[oort] not needed in this implementation
            }
          }
          break;
        case 72:
        case 73:
        case 74:
        case 75:
        case 76:
        case 77:
        case 78:
        case 79:
        case 80:
        case 81:
        case 82:
        case 83:
        case 84:
        case 85:
        case 86:
        case 87:
          // Pattern Select
          if (curSeqMode == PTRN_PLAY) {
            nextPattern = (pitch - 72) + curBank * NBR_PATTERN;
            //group.pos = pattern[ptrnBuffer].groupPos;
            if (curPattern != nextPattern) {
              curPattern = nextPattern;
              selectedPatternChanged = TRUE;
              needLcdUpdate = TRUE;
              group.length = 0;
              group.priority = FALSE;
            }
          }
          break;
#endif  // MIDI_BANK_PATTERN_CHANGE
      }
    }
  } else {
    HandleNoteOff(channel, pitch, velocity);
  }
}

//Handle noteOFF
void HandleNoteOff(byte channel, byte pitch, byte velocity) {
  if (channel == seq.RXchannel) {
    switch (pitch) {
      case 36:
        MidiTrigOff(BD);
        break;
      case 40:
        MidiTrigOff(SD);
        break;
      case 45:
        MidiTrigOff(LT);
        break;
      case 47:
        MidiTrigOff(MT);
        break;
      case 48:
        MidiTrigOff(HT);
        break;
      case 37:
        MidiTrigOff(RM);
        break;
      case 39:
        MidiTrigOff(HC);
        break;
      case 42:
        MidiTrigOff(CH);
        break;
      case 46:
        MidiTrigOff(OH);
        break;
      case 49:
        MidiTrigOff(CRASH);
        break;
      case 51:
        MidiTrigOff(RIDE);
        break;
      case 60:
        if (gateInst & 1) TRIG_HIGH;  //[oort] not tested    // [zabox] [1.028] gate
        break;

        /* previous Neuro version
    switch (pitch){
    case 35:
    case 36:
      MidiTrigOff(BD);
      break;
    case 38:
    case 40:
      MidiTrigOff(SD);
      break;
    case 41:
      MidiTrigOff(LT);
      break;
    case 45:
    case 47:
    case 48:
      MidiTrigOff(MT);
      break;
    case 50:
      MidiTrigOff(HT);
      break;
    case 34:
    case 37:
      MidiTrigOff(RM);
      break;
    case 39:
      MidiTrigOff(HC);
      break;
    case 42:
      MidiTrigOff(CH);
      break;
    case 46:
      MidiTrigOff(OH);
      break;
    case 51:
      MidiTrigOff(RIDE);
      break;
    case 49:
      MidiTrigOff(CRASH);
      break;
    case 60:
      if (gateInst & 1) TRIG_HIGH;                             // [zabox] [1.028] gate
      break; */
    }
  }
}


//MidiTrigOn instrument
void MidiTrigOn(byte inst, byte velocity) {
  if (seq.sync != EXPANDER) return;
  if (instWasMidiTrigged[inst] == FALSE && (~(muteInst >> inst) & 1)) {  // [zabox] [1.028] expander

    SetMuxTrigMidi(inst, velocity);

    if (inst == OH) {
      SetDoutTrig((1 << HH) | (lastDoutTrig & (~(1 << HH_SLCT))));
      triggerTime[HH] = TCNT2;
    } else if (inst == CH) {
      SetDoutTrig((1 << HH) | (lastDoutTrig | (1 << HH_SLCT)));
      triggerTime[HH] = TCNT2;
    } else {
      SetDoutTrig((1 << inst) | (lastDoutTrig));
      triggerTime[inst] = TCNT2;
    }

    if (showTrigLeds) {
      stepLeds = ((1 << ledMap[inst]) | lastStepLeds);
    }
    instWasMidiTrigged[inst] = TRUE;
  }
}

//bool isMidiTrigged()
//{
//  for(int i = 0 ; i < NBR_INST; i++)
//  {
//    if (instWasMidiTrigged[i] == TRUE) return TRUE;
//  }
//  return FALSE;
//}

//MidiTrigOff instrument
void MidiTrigOff(byte inst) {
  instWasMidiTrigged[inst] = FALSE;

  if ((gateInst >> inst) & 1U) SetMuxTrigMidi(inst, 0);  // [1.028]
}

//Midi send all note off
void SendAllNoteOff() {
  MIDI.sendControlChange(ALL_NOTE_OFF, 0, seq.TXchannel);
}

#if MIDI_DRUMNOTES_OUT
void SendInstrumentMidiOut(unsigned int value) {
  unsigned int MIDIVelocity;
  if (seq.TXchannel == 0) return;
  // Send MIDI notes for the playing instruments
  for (int inst = 0; inst < NBR_INST; inst++) {
    if (bitRead(value, inst)) {
      if (inst >= 14 && bitRead(muteInst, 5)) continue;
      if (instMidiNote[inst] != 0 && pattern[ptrnBuffer].velocity[inst][curStep] > 0) {
        MIDIVelocity = InstrumentMidiOutVelocity[inst];
        if (inst >= 14) {
          MIDIVelocity = InstrumentMidiOutVelocity[CH];
        }
        byte Accent;
        Accent = ((bitRead(pattern[ptrnBuffer].inst[TOTAL_ACC], curStep)) ? (pattern[ptrnBuffer].totalAcc * 4) : 0);

#if REALTIME_ACCENT
        if (analogRead(TRIG2_PIN) > 200) {
          Accent = (bitRead(pattern[ptrnBuffer].inst[TOTAL_ACC], curStep)) * ((analogRead(TRIG2_PIN) - 290) / 14);
        }
#endif
        MIDIVelocity = map(MIDIVelocity, instVelLow[inst], instVelHigh[inst] + Accent, MIDI_LOW_VELOCITY, MIDI_HIGH_VELOCITY);

        if (bitRead(pattern[ptrnBuffer].inst[TOTAL_ACC], curStep)) {
          byte midi_accent = 0;
          midi_accent = map(Accent, 0, 52, 0, 16);
          MIDIVelocity = MIDIVelocity + midi_accent;
        }
        MidiSendNoteOn(seq.TXchannel, instMidiNote[inst] - 12, MIDIVelocity);
      }
    }
    lastInstrumentMidiOut = value;
  }
}

void SendInstrumentMidiOff() {
  if (seq.TXchannel == 0) return;

  for (int inst = 0; inst < NBR_INST; inst++) {
    if (bitRead(lastInstrumentMidiOut, inst)) {
      if (inst >= 14 && bitRead(muteInst, 5)) continue;
      if (instMidiNote[inst] != 0 && pattern[ptrnBuffer].velocity[inst][curStep] > 0) {
        MidiSendNoteOff(seq.TXchannel, instMidiNote[inst] - 12);
      }
    }
  }
}
#endif  // MIDI_DRUMNOTES_OUT
