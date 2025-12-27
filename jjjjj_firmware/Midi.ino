//-------------------------------------------------
//                  NAVA v1.x
//              Midi Function
//-------------------------------------------------

/////////////////////Function//////////////////////

//initialize midi real time variable
void InitMidiRealTime() {
  midiStart = LOW;
  midiStop = LOW;
  midiContinue = LOW;
}

//intialize note off when stop or patternChanged [TR-909 STYLE]
void InitMidiNoteOff() {
  if (midiNoteOnActive) {
    // Turn off all active external track notes
    for (byte track = 0; track < 16; track++) {
      if (extTrackNoteOn[track]) {
        byte noteToSend = pgm_read_byte(&EXT_TRACK_NOTES[track]);
#if MIDI_EXT_CHANNEL
        MidiSendNoteOff(seq.EXTchannel, noteToSend);
#else
        MidiSendNoteOff(seq.TXchannel, noteToSend);
#endif
        extTrackNoteOn[track] = FALSE;
      }
    }
    midiNoteOnActive = FALSE;
  }
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
