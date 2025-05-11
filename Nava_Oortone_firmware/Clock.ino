//-------------------------------------------------
//                  NAVA v1.x
//                  BPM
//-------------------------------------------------

/////////////////////Function//////////////////////
//Timer interrupt
ISR(TIMER1_COMPA_vect) {
  CountPPQN();
}

ISR(TIMER2_COMPA_vect) {  // [zabox] [v1.028] 2ms trig off isr. improves led flickering a lot and improves external instrument midi out lag

  TRIG_TIMER_STOP;  // [zabox] one shot
  TRIG_TIMER_ZERO;  // [zabox] reset
  SetDoutTrig(tempDoutTrig);
#if MIDI_DRUMNOTES_OUT
  SendInstrumentMidiOff();  // [Neuromancer] MIDI note out
#endif
}



ISR(TIMER3_COMPA_vect) {  // [zabox] flam

  FLAM_TIMER_STOP;
  FLAM_TIMER_ZERO;

  SetMuxFlam();

  SetDoutTrig(stepValueFlam & (~muteInst) | tempDoutTrig);  //Send TempDoutTrig too to prevet tick noise on HH circuit

#if MIDI_DRUMNOTES_OUT
  SendInstrumentMidiOut(stepValueFlam & (~muteInst) | tempDoutTrig);  // [Neuromancer] MIDI Note out
#endif

  TRIG_TIMER_START;
  stepValueFlam = 0;
}


//Tick proceed each pulse
void CountPPQN() {
  blinkVeryFast = !blinkVeryFast;
  if (ppqn % (PPQN / 2) == 0) blinkTempo = !blinkTempo;
  if (ppqn % (pattern[ptrnBuffer].scale / 2) == 0) blinkFast = LOW;

  //[oort] comment: This section is different in the 1.028beta, this is from the 2021Neuro firmware
  if (seq.sync == MASTER) {  // [zabox] has to be 0/2 for the correct phase
    if (ppqn % 4 == 0) {
      DIN_CLK_HIGH;        //[oort] comment: Why are there two different DIN-socket clock settings in parallell?
      dinClkState = HIGH;  //
      while (!(UCSR1A & (1 << UDRE1))) {};  // [zabox] directly adressing the uart fixes the midi clock lag
      UDR1 = CLOCK_CMD;                     //Tick
    } else if (ppqn % 4 == 2) {
      DIN_CLK_LOW;
      dinClkState = LOW;
    }
  }


  if (isRunning) {
    if (ppqn % pattern[ptrnBuffer].scale == (pattern[ptrnBuffer].scale / 2)) tapStepCount++;

    if (tapStepCount > pattern[ptrnBuffer].length) tapStepCount = 0;

    // Initialize the step value for trigger and gate value for cv gate track
    stepValue = 0;

    if (ppqn % pattern[ptrnBuffer].scale == 0) stepChanged = TRUE;  //[oort]one: only used locally here, not needed, extend this if instead?

    if (((ppqn + shuffle[(pattern[ptrnBuffer].shuffle) - 1][shufPolarity]) % pattern[ptrnBuffer].scale == 0) && stepChanged) {  //Each Step
      stepChanged = FALSE;                                                                                                      //flag that we already trig this step
      shufPolarity = !shufPolarity;
      blinkFast = HIGH;

      //sequencer direction-----------
      switch (seq.dir) {
        case FORWARD:
          curStep = stepCount;
          break;
        case BACKWARD:
          curStep = pattern[ptrnBuffer].length - stepCount;
          break;
        case PING_PONG:
          if (curStep == pattern[ptrnBuffer].length && changeDir == 1) changeDir = 0;
          else if (curStep == 0 && changeDir == 0) changeDir = 1;
          if (changeDir) curStep = stepCount;
          else curStep = pattern[ptrnBuffer].length - stepCount;
          break;
        case RANDOM:
          curStep = random(0, 16);
          break;
      }

      //Set step value to be trigged
      for (byte z = 0; z < NBR_INST; z++) {
        stepValue |= (bitRead(pattern[ptrnBuffer].inst[z], curStep)) << z;
      }
      //Set stepvalue depending metronome
      stepValue |= (bitRead(metronome, curStep) << RM);

      setFlam();  // [zabox] [1.027] if changed, update flam interval

      if (stepValue) {
        SetMux();
        int temp_muteInst = muteInst;                                  // [zabox] OH/CH mute select
        if (bitRead(stepValue, CH) && bitRead(muteInst, CH)) {         //
          temp_muteInst |= (1 << HH);                                  //
        } else if (bitRead(stepValue, OH) && bitRead(muteInst, OH)) {  //
          temp_muteInst |= (1 << HH);                                  //
        }

        if (bitRead(pattern[ptrnBuffer].inst[CH], curStep) && !bitRead(muteInst, CH)) tempDoutTrig = B10;     //CH trig                        // [zabox] + check if OH/CH mute
        else if (bitRead(pattern[ptrnBuffer].inst[OH], curStep) && !bitRead(muteInst, OH)) tempDoutTrig = 0;  // OH trig                    // [zabox] + check if OH/CH mute

        SetDoutTrig((stepValue) & (~temp_muteInst) | (tempDoutTrig));  //Send TempDoutTrig too to prevet tick noise on HH circuit

#if MIDI_DRUMNOTES_OUT
        SendInstrumentMidiOut((stepValue) & (~temp_muteInst) | (tempDoutTrig));  // [Neuromancer] MIDI Note out
#endif

        TRIG_TIMER_START;  // [zabox] [1.028] start trigger off timer

        if (stepValueFlam) {
          FLAM_TIMER_START;
        }
      }
      if (bitRead(pattern[ptrnBuffer].inst[TRIG_OUT], curStep)) {
        TRIG_LOW;  //Trigout
        trigCounterStart = TRUE;
      }

      //Trig external instrument------------------------------------- [SIZZLE FW]
      if (bitRead(pattern[ptrnBuffer].inst[EXT_INST], curStep)) {
        InitMidiNoteOff();
#if MIDI_EXT_CHANNEL
        if (pattern[ptrnBuffer].velocity[EXT_INST][curStep] > 0) {
          unsigned int MIDIVelocity = pattern[ptrnBuffer].velocity[EXT_INST][curStep];
          MIDIVelocity = map(MIDIVelocity, instVelLow[EXT_INST], instVelHigh[EXT_INST], MIDI_LOW_VELOCITY, MIDI_HIGH_VELOCITY);
          if (bitRead(pattern[ptrnBuffer].inst[TOTAL_ACC], curStep)) MIDIVelocity = MIDI_ACCENT_VELOCITY;
          // Send the MIDI note that corresponds to this step [SIZZLE FW]
          MidiSendNoteOn(seq.EXTchannel, pattern[ptrnBuffer].extNote[curStep], MIDIVelocity);
          midiNoteOnActive = TRUE;
        }
#else
        // Send the MIDI note that corresponds to this step [SIZZLE FW]
        MidiSendNoteOn(seq.TXchannel, pattern[ptrnBuffer].extNote[curStep], HIGH_VEL);
        midiNoteOnActive = TRUE;
#endif
        // We no longer need to increment noteIndexCpt since each step has its own note [SIZZLE FW]
      }

      //TRIG_HIGH;
      //ResetDoutTrig();
      stepCount++;

      //endMeasure
      //[oort] comment: This is executed at the last step of every bar.
      if (stepCount > pattern[ptrnBuffer].length) {  // && (ppqn % 24 == pattern[ptrnBuffer].scale - 1))
        endMeasure = TRUE;
        incrementRequired = TRUE;
        stepCount = 0;
        displayTrkPlayPos = trk.pos;                                                                   //[oort] since trk.pos is incremented in advance
        if (curSeqMode == TRACK_PLAY || (prevSeqMode == TRACK_PLAY && curSeqMode == MUTE)) trk.pos++;  //[oort] mute in track mode
        group.pos++;
        needLcdUpdate = TRUE;  //[oort] maybe not needed

        //[oort] addition
        if (nextPatternReady) {  //&& curSeqMode == PTRN_PLAY
          nextPatternReady = FALSE;
          noteIndexCpt = 0; //[oort] also needed, the whole Externa Instr index count must be revised, it's a mess TO DO
          ptrnBuffer = !ptrnBuffer;  //[oort] comment: switch between twin buffers
          prevPattern = curPattern;  //[oort] needed in tap mode
          curPattern = nextPattern;
          needLcdUpdate = TRUE;
          //[oort] consider moving these before pattern buffer swap (in seq.ino), 2nd buffer is not really ready until these are executed, comment: DONE I BELIEVE?
        }
      } else {  //[oort] could we increment trackposition here and reduce code?
        endMeasure = FALSE;
      }
    }
  }  //end if (isRunning)

  ppqn++;  // [1.028] more consistent to run the counter from 0-95
  if (ppqn >= PPQN) ppqn = 0;
}

void Metronome(boolean state) {
  //  if (state){ //[oort] original, are these global?
  if (state)
    metronome = 0x1111;  //trig RM every beat
  else
    metronome = 0;
}
