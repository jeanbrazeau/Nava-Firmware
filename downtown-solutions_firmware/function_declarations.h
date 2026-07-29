// Function declarations for all modules

// Timer functions
void initTrigTimer();
void initFlamTimer();
void setFlam();
void TimerSetFrequency();

// Sequencer functions
void InitSeq();
void LoadPatternBank(byte bankNmbr);
void FlushPatternBank();
void InitPattern(Pattern* pattern);
void SetHHPattern(Pattern* pattern);
void InstToStepWord(Pattern* pattern);
void SetMuxTrigMidi(byte instrument, byte velocity);
void InitMidiRealTime();
void SeqConfiguration();
void SeqParameter();
void ExtInstUpdate();
void ExitExtInstEditMode();
void ExtPreviewOn(byte note, unsigned long holdMs);
void ExtPreviewOff();
void ExtPreviewCheck();
void ExtSetTrackNote(byte note);
byte LcdPrintNoteName(byte note);
void SaveExtTrackNotes();
void LoadExtTrackNotes();
void MidiSendExtNoteOn(byte note, byte velocity);
void MidiSendExtNoteOff(byte note);
void SetMuxFlam();
void SetMux();
void InitMidiNoteOff();
void SendExtTrackNoteOff();
void ServiceExtMidiNotes();
void ServiceExtMidiNotesFromClock();
void ExtTransmitStep(unsigned int noteOnMask, byte velocity, boolean noteOffDue);
void MidiSendNoteOn(byte channel, byte note, byte velocity);
void MidiSendNoteOff(byte channel, byte note);
void SendAllNoteOff();
void SetSeqSync();
void Metronome(boolean state);

// SysEx pattern/track transfer
#if MIDI_HAS_SYSEX
void HandleSystemExclusive(byte* array, unsigned size);
void MidiSendSysex(byte dumpType, byte param);
void EnableSysexMode();
void DisableSysexMode();
void ConnectMidiSysex();
void DisconnectMidiSysex();
void SysexSendRecord(byte cmd, byte param);
void SysexSendAck(byte status);
void SysexResetRunningStatus();
#endif

// EEPROM block access
void EEpromReadBlock(unsigned long address, byte* buf, byte count);
void EEpromWriteBlock(unsigned long address, const byte* buf, byte count);

// Digital I/O functions
void SetDoutTrig(unsigned int value);
void SetDoutLed(unsigned int stepLeds, unsigned int configLed, byte menuLed);