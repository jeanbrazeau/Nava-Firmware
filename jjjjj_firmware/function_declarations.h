// Function declarations for all modules

// Timer functions
void initTrigTimer();
void initFlamTimer();
void setFlam();
void TimerSetFrequency();

// Sequencer functions
void InitSeq();
void LoadPatternBank(byte bankNmbr);
void InitPattern(Pattern* pattern);
void SetHHPattern(Pattern* pattern);
void InstToStepWord(Pattern* pattern);
void SetMuxTrigMidi(byte instrument, byte velocity);
void InitMidiRealTime();
void SeqConfiguration();
void SeqParameter();
void KeyboardUpdate();
void ExitExtInstEditMode();
void ExtPreviewOn(byte note, unsigned long holdMs);
void ExtPreviewOff();
void ExtPreviewCheck();
void SetMuxFlam();
void SetMux();
void InitMidiNoteOff();
void SendExtTrackNoteOff();
void ServiceExtMidiNotes();
void MidiSendNoteOn(byte channel, byte note, byte velocity);
void MidiSendNoteOff(byte channel, byte note);
void SendAllNoteOff();
void SetSeqSync();
void Metronome(boolean state);

// Digital I/O functions
void SetDoutTrig(unsigned int value);
void SetDoutLed(unsigned int stepLeds, unsigned int configLed, byte menuLed);