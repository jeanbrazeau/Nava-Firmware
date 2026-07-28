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

  SetDoutTrig((stepValueFlam & (~muteInst)) | tempDoutTrig);  //Send TempDoutTrig too to prevet tick noise on HH circuit

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

    // Release the sounding ext notes a couple of ticks BEFORE the next step instead of
    // at it. Held here, every active track costs 4 bytes at the step boundary (its
    // note-off then its note-on) and MIDI is serial, so the last track of a 6-track step
    // landed 8ms behind the analog trigger it should have hit with. Releasing early
    // leaves the boundary carrying note-ons only - 2 bytes per track - and the releases
    // ride in dead time where nothing is time-critical.
    // Same boundary expression as the step test below, offset by the lead; shufPolarity
    // already holds the value the NEXT boundary will use, having been flipped at the last
    // step. Adding PPQN keeps the sum non-negative for small ppqn against a negative
    // shuffle offset, and cannot shift the result because every scale divides PPQN.
    // The two conditions can never coincide: they differ by EXT_RELEASE_LEAD, which is
    // smaller than the smallest scale.
    if (extReleaseArmed
        && ((ppqn + PPQN + shuffle[(pattern[ptrnBuffer].shuffle) - 1][shufPolarity] + EXT_RELEASE_LEAD)
            % pattern[ptrnBuffer].scale) == 0) {
      extReleaseArmed = FALSE;
      extPendingOff = TRUE;
      ServiceExtMidiNotesFromClock();
    }

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

        SetDoutTrig((stepValue & (~temp_muteInst)) | tempDoutTrig);  //Send TempDoutTrig too to prevet tick noise on HH circuit

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

      //Trig external instrument (TR-909 STYLE - polyphonic)---------
      // Only record what should sound; ServiceExtMidiNotes() transmits it from the
      // main loop. Sending here costs up to 16 note-offs plus 16 note-ons = 96 bytes
      // against Serial1's 64 byte TX buffer, and HardwareSerial busy-waits on UDRE
      // with interrupts disabled, so a dense step blocks this function for ~30ms
      // while a PPQN tick at 120 BPM is ~5.2ms - the MIDI clock and DIN sync
      // generated a few lines above would collapse.
      // A queued step that is overtaken before the loop drains it is coalesced away
      // (SLAVE runs CountPPQN four times in a row from HandleClock): the older step's
      // notes were about to be cut by this one anyway, and extPendingOff is sticky
      // so the sounding notes still get their note-off.
      // The ext layer reads its own position. extLength equals length unless LAST STEP
      // was used inside EXT INST edit mode, in which case the MIDI tracks loop shorter
      // than the kit and the two lanes phase against each other. Direction is
      // deliberately not applied here: BACKWARD/PING_PONG/RANDOM are defined against
      // pattern.length, and re-deriving them from a different length would make the ext
      // lane disagree with the drums even when the two lengths match.
      extCurStep = (pattern[ptrnBuffer].extLength == pattern[ptrnBuffer].length)
                     ? curStep
                     : extStepCount;

      byte extVelocity = HIGH_VEL;
      unsigned int extOnMask = 0;
      for (byte track = 0; track < 16; track++) {
        if (bitRead(pattern[ptrnBuffer].extTrack[track], extCurStep)) bitSet(extOnMask, track);
      }
#if MIDI_EXT_CHANNEL
      if (extOnMask) {
        // Velocity is shared across all tracks of the step
        // extCurStep: the EXT_INST velocity lane belongs to the ext layer and moves with
        // it. TOTAL_ACC below stays on curStep - it accents the whole machine at that
        // moment, so it has to line up with the kit rather than with the ext loop.
        if (pattern[ptrnBuffer].velocity[EXT_INST][extCurStep] > 0) {
          long scaled = map(pattern[ptrnBuffer].velocity[EXT_INST][extCurStep],
                            instVelLow[EXT_INST], instVelHigh[EXT_INST],
                            MIDI_LOW_VELOCITY, MIDI_HIGH_VELOCITY);
          // A stored velocity outside the instVel table range maps past 127 and would
          // wrap when the MIDI library masks to 7 bits, landing near the bottom instead
          extVelocity = constrain(scaled, 1L, 127L);
        }
        if (bitRead(pattern[ptrnBuffer].inst[TOTAL_ACC], curStep)) {
          // Accent sits on top of the nominal high velocity, as the drum path does
          extVelocity = MIDI_HIGH_VELOCITY + MIDI_ACCENT_VELOCITY;
        }
      }
#endif
      extPendingVel = extVelocity;
      extPendingOn = extOnMask;
      // Still set unconditionally: if the early release did not get out in time - a very
      // short shuffled gap, or a burst the UART could not absorb - this is the safety net
      // that keeps off-before-on ordering intact, degrading to the old behaviour for that
      // step rather than stranding a note.
      extPendingOff = TRUE;
      extReleaseArmed = (extOnMask != 0);
      // Send it here when the UART has room, so the note is not held back by whatever
      // the loop is doing - an end-of-measure LCD repaint blocks it for ~14ms. Declines
      // and leaves the step queued if transmitting could block; the loop still drains.
      ServiceExtMidiNotesFromClock();

      //TRIG_HIGH;
      //ResetDoutTrig();
      stepCount++;
      // Wraps on its own length, so a shorter ext loop phases against the kit rather
      // than truncating the pattern. Guarded against a stored length of 0 from a bank
      // that has not been through InitPattern() yet.
      extStepCount++;
      if (extStepCount > pattern[ptrnBuffer].extLength || extStepCount >= NBR_STEP) extStepCount = 0;

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
          // The incoming pattern brings its own extLength, so a phase carried over from
          // the outgoing one would start the new ext loop at an arbitrary offset.
          extStepCount = 0;
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
