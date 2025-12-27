//-------------------------------------------------
//                  NAVA v1.x
//                 SEQ Parameter
//-------------------------------------------------

/////////////////////Function//////////////////////
void SeqParameter() {
  readButtonState = StepButtonGet(MOMENTARY);
  //can not access config when isRunning
  if (isRunning && seq.configMode) {
    seq.configMode = FALSE;
    needLcdUpdate = TRUE;
  }
  if (curSeqMode != MUTE) muteBtn.counter = 0;
  if (!seq.configMode) seq.configPage = 0;

  //-------------------Encoder button---------------------------
  if (encBtn.justPressed) {
    curIndex++;
    if (curIndex >= MAX_CUR_POS) curIndex = 0;
#if MIDI_HAS_SYSEX
    // Limit encoder positions for Sysex
    if (seq.configMode && seq.configPage == 3) {
      if (sysExDump < SYSEX_MAXPARAM && curIndex > 1) curIndex = 0;
      if (sysExDump >= SYSEX_MAXPARAM && curIndex > 0) curIndex = 0;
    }
#endif
    needLcdUpdate = TRUE;
  }
  //[oort] Start/Stop-Continue is not fully implemented, some experimental code here

  //-------------------start button---------------------------
  if ((startBtn.justPressed && !isRunning) || midiStart  || midiContinue ) {  //[oort] midiCOntinue belongs to stopBtn probably TO DO 
    if (curPattern < MAX_PTRN)                             
    {
      ppqn = 0;
      isRunning = TRUE;
      isStop = FALSE;
      sequencerJustStarted = TRUE;  //[oort] some increments need special attention after start
      stopBtn.counter = 0;
      changeDir = 1;     //restart Forward
      shufPolarity = 0;  //Init shuffle polarity
      noteIndexCpt = 0;  //init ext instrument note index counter [oort] comment: start offset problem, this index is handled strangely TO DO
      blinkTempo = 0;    // [zabox] looks more consistent

      //[oort] differences to keep in mind in previous firmwares
      //      stepCount = 0;  //[oort] in Neuro but not in Sandor (in stop instead)
      //      tapStepCount = 0;  //[oort] in Neuro but not in Sandor (in stop instead)

      if (seq.sync == MASTER) MIDI.sendRealTime(midi::MidiType::Start);  //;MidiSend(START_CMD);
      DIN_START_HIGH;           //[oort] comment: Why are there two different "din start"? 
      dinStartState = HIGH;     //
      //[oort] comment: According to Wikipedia DIN-socket Start pin should be HIGH 9ms before clock starts.
    }
  }

  //-------------------stop button------------------------------
  if ((stopBtn.justPressed && !instBtn) || midiStop || curPattern >= MAX_PTRN) {  //[oort] TO DO? this makes END_OF_TRACK reset which is not desired during track write
    //Init Midi note off
    InitMidiNoteOff();
    SendAllNoteOff();
    if (midiStop) stopBtn.counter = 0;
    else if (midiContinue) stopBtn.counter = 1;  //[oort] is this supposed to sync to bar? TO DO
    stopBtn.counter++;                           //[oort] only use continue in track play, consider more options but which?

    if (stopBtn.counter == 1) {
      isStop = TRUE;
      isRunning = FALSE;
      if (curSeqMode != TRACK_PLAY && curSeqMode != TRACK_WRITE) stopBtn.counter = 0;
      if (seq.sync == MASTER) MIDI.sendRealTime(midi::MidiType::Stop);  //;MidiSend(STOP_CMD);
      DIN_START_LOW;
      dinStartState = LOW;

      //[oort] skeleton in case we press continue next
      stepCountContinue = stepCount;
      tapStepCountContinue = tapStepCount;
      trackPosContinue = trk.pos;
      groupPosContinue = group.pos;

      stepCount = 0;
      tapStepCount = 0;
      group.pos = 0;  //[oort] maybe don't do this?
      trk.pos = 0;
      if (curSeqMode == TRACK_PLAY) nextPattern = track[trkBuffer].patternNbr[trk.pos]; //[oort] does Mute in Track Mode work here?
      else if (group.length) nextPattern = group.firstPattern;
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
    }
    //-----------second press on STOP/CONT
    //[oort] Temporarily disabled
    /*  if (stopBtn.counter == 2 && curSeqMode == TRACK_PLAY && curPattern < MAX_PTRN) {  //[oort] not sure what third term is for
   //[oort] TO DO: How to implement continue
      isStop = FALSE;                                                                
      isRunning = TRUE;
      stopBtn.counter = 0;
      ppqn = 0;
      //stepCount = stepCountContinue;
      //tapStepCount = tapStepCountContinue;
      trk.pos = trackPosContinue;
      group.pos = groupPosContinue;

      if (curSeqMode == TRACK_PLAY)  nextPattern = track[trkBuffer].patternNbr[trk.pos];
      else if (group.length) curPattern = group.pos;
      selectedPatternChanged = TRUE;  //[oort] better to load from RAM (copy/paste) TO DO
      sequencerJustStarted = TRUE;

      if (seq.sync == MASTER) MIDI.sendRealTime(midi::MidiType::Continue);  //MidiSend(CONTINU_CMD);
      DIN_START_HIGH;
      dinStartState = HIGH; 
    } */
  }

  //-------------------Start increments------------------------------

  if (sequencerJustStarted) {
    sequencerJustStarted = FALSE;
    if (curSeqMode == TRACK_PLAY || (prevSeqMode == TRACK_PLAY && curSeqMode == MUTE)) {
      displayTrkPlayPos = trk.pos;
      trk.pos++;
    }
    if (group.length) group.pos++;
    incrementRequired = TRUE;  //[oort] maybe not in Pattern modes?
    //[oort] Maybe we can handle 9 ms start delay here? TO DO
  }

  //-------------------Shift button pressed------------------------------
  if (shiftBtn) {
    if (trkBtn.justPressed) {
      needLcdUpdate = TRUE;
      curSeqMode = TRACK_WRITE;
      trk.pos = 0;
      keyboardMode = FALSE;
      seq.configMode = FALSE;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];  // Get the correct pattern
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      bankReloadNeededOnPattern = TRUE;  //[oort] We will have to reload current bank when we exit Pattern Modes
    }
    if (ptrnBtn.justPressed) {
      if (curPattern >= MAX_PTRN) nextPattern = 0;                   // If on an invalid pattern number, goto pattern 0
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;  //
      needLcdUpdate = TRUE;
      curSeqMode = PTRN_STEP;
      seq.configMode = FALSE;
      trackNeedSaved = FALSE;
    }
    if (tapBtn.justPressed) {
      if (curPattern >= MAX_PTRN) nextPattern = 0;                   // If on an invalid pattern number, goto pattern 0
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;  //
      curSeqMode = PTRN_TAP;
      needLcdUpdate = TRUE;
      keyboardMode = FALSE;
      seq.configMode = FALSE;
      trackNeedSaved = FALSE;
    }
    if (bankBtn.justPressed) {
      CopyPatternToBuffer(curPattern);
    }  // copy current pattern to the buffer
    if (muteBtn.justPressed) {
      PasteBufferToPattern(curPattern);
      patternWasEdited = TRUE;
    }  //paste copy buffered pattern to the current pattern number

    //sequencer configuration page
    if (tempoBtn.justPressed && !isRunning) {
      if (!seq.configMode) {
        // First press - enter config mode
        seq.configMode = TRUE;
        keyboardMode = FALSE;
        seq.configPage = 1; // Start at page 1
      } else {
        // Already in config mode, cycle to next page
#if MIDI_HAS_SYSEX
        if (seq.configPage == 1) {
          seq.configPage = 2; // Go to page 2
        } else if (seq.configPage == 2) {
          seq.configPage = 3; // Go to sysex page when MIDI_HAS_SYSEX is defined
        } else if (seq.configPage == 3) {
          seq.configPage = 4; // Go to bootloader page (page 4)
        } else if (seq.configPage == 4) {
          seq.configPage = 1; // Wrap back to page 1
        }
#else
        if (seq.configPage == 1) {
          seq.configPage = 2; // Go to page 2
        } else if (seq.configPage == 2) {
          seq.configPage = 3; // Go to bootloader page (page 3)
        } else if (seq.configPage == 3) {
          seq.configPage = 1; // Wrap back to page 1
        }
#endif
      }

      curIndex = 0;
#if MIDI_HAS_SYSEX
      if (seq.configPage == 3) seq.setupNeedSaved = FALSE;  //only if sysex
#endif

      // No debug display, just update the LCD immediately

      needLcdUpdate = TRUE;
    }
  }
  //-------------------Shift button released------------------------------
  else {
    if (trkBtn.justPressed) {
      curSeqMode = TRACK_PLAY;
      needLcdUpdate = TRUE;
      keyboardMode = FALSE;
      seq.configMode = FALSE;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      bankReloadNeededOnPattern = TRUE;  //[oort]
    }

    // if (backBtn.justPressed) ;//back  track postion
    //if (fwdBtn.justPressed) ;//foward track postion
    if (numBtn.pressed)
      ;  //select Track number, //[oort] implemented elsewhere, numBtn not used for this
    if (ptrnBtn.justPressed) {
      if (curPattern >= MAX_PTRN) nextPattern = 0;                   // If on an invalid pattern number, goto pattern 0
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;  //
      if (curSeqMode == PTRN_PLAY) curSeqMode = PTRN_STEP;
      else curSeqMode = PTRN_PLAY;
      needLcdUpdate = TRUE;
      keyboardMode = FALSE;
      seq.configMode = FALSE;
      trackNeedSaved = FALSE;
    }
    if (tapBtn.justPressed) ShiftLeftPattern();
    if (dirBtn.justPressed) ShiftRightPattern();
    if (guideBtn.justPressed) {
      guideBtn.counter++;
      switch (guideBtn.counter) {
        case 1:
          metronomeState = TRUE;  //[oort]
          break;
        case 2:
          metronomeState = FALSE;  //[oort]
          guideBtn.counter = 0;
          break;
      }
      Metronome(metronomeState);
    }

    if (muteBtn.justPressed && curSeqMode != TRACK_WRITE) {
      muteBtn.counter++;
      switch (muteBtn.counter) {
          //[oort] comment: the way mute mode is handled is not optimal,
          //it's actually not a separate sequencer mode but a special
          //mode in all the other modes,
          //muteMode = TRUE or something like that could be better
        case 1:
          prevSeqMode = curSeqMode;
          curSeqMode = MUTE;  //paste copy buffered pattern to the current pattern number
          break;
        case 2:
          curSeqMode = prevSeqMode;
          muteBtn.counter = 0;
          break;
      }
    }
    if (curSeqMode != MUTE) muteBtn.counter = 0;
    if (!seq.configMode) seq.configPage = 0;
    if (tempoBtn.justPressed && !isRunning) {
      seq.configMode = FALSE;
      seq.configPage = 0;
#if MIDI_HAS_SYSEX
      seq.SysExMode = false;
#endif
      SetSeqSync();
      needLcdUpdate = TRUE;
    }
    else if (tempoBtn.justRelease) needLcdUpdate = TRUE;
  }

  if (curSeqMode != TRACK_PLAY && curSeqMode != TRACK_WRITE && curSeqMode != MUTE && bankReloadNeededOnPattern) {
    //this might happen when leaving Track mode
    LoadPatternBank(curBank);
    bankReloadNeededOnPattern = FALSE;
  }

  //==============================================================================
  //////////////////////////MODE PATTERN EDIT/////////////////////////////////

  if (curSeqMode == PTRN_STEP || curSeqMode == PTRN_TAP) {  //this is a long if statement

    static boolean curInstChanged;  //flag that curInstchanged to not update LCD more than one time

    // Force instrument to EXT_INST if we're in EXT INST edit mode but somehow lost the selection
    if (extInstEditMode && curInst != EXT_INST) {
      curInst = EXT_INST;
      needLcdUpdate = TRUE;
    }

    //-------------------Select instrument------------------------------
    //Match with trig shift register out (cf schematic)
    if (readButtonState == 0) doublePush = 0;  //init double push if all step button  released

    if (instBtn && readButtonState && !extInstEditMode && !extInstButtonHandled) {  // [zabox] [1.027] added flam, [SIZZLE] Normal instrument selection (not for EXT INST edit mode)
      curInstChanged = TRUE;
      keyboardMode = FALSE;
      switch (FirstBitOn()) {
        case BD_BTN:
        case BD_LOW_BTN:
          curInst = BD;
          if (doublePush == 0) {
            curFlam = 0;
          }
          break;
        case SD_BTN:
        case SD_LOW_BTN:
          curInst = SD;
          if (doublePush == 0) {
            curFlam = 0;
          }
          break;
        case LT_BTN:
        case LT_LOW_BTN:
          curInst = LT;
          if (doublePush == 0) {
            curFlam = 0;
          }
          break;
        case MT_BTN:
        case MT_LOW_BTN:
          curInst = MT;
          if (doublePush == 0) {
            curFlam = 0;
          }
          break;
        case HT_BTN:
        case HT_LOW_BTN:
          curInst = HT;
          if (doublePush == 0) {
            curFlam = 0;
          }
          break;
        case RM_BTN:
          curInst = RM;
          curFlam = 0;
          break;
        case HC_BTN:
          curInst = HC;
          curFlam = 0;
          break;
        case CH_BTN:
        case CH_LOW_BTN:
          curFlam = 0;
          if (doublePush == 0) {
            curInst = CH;
          }
          break;
        case RIDE_BTN:
          curInst = RIDE;
          curFlam = 0;
          break;
        case CRASH_BTN:
          curInst = CRASH;
          curFlam = 0;
          break;
      }
      switch (readButtonState) {  //[oort] 1 instead of TRUE used below, why?
        case BD_F_BTN:
          curFlam = 1;
          doublePush = 1;
          break;
        case SD_F_BTN:
          curFlam = 1;
          doublePush = 1;
          break;
        case LT_F_BTN:
          curFlam = 1;
          doublePush = 1;
          break;
        case MT_F_BTN:
          curFlam = 1;
          doublePush = 1;
          break;
        case HT_F_BTN:
          curFlam = 1;
          doublePush = 1;
          break;
        case OH_BTN:
          curInst = OH;
          doublePush = 1;
          break;
      }
    }
    if (curInstChanged && stepsBtn.justRelease) {
      needLcdUpdate = TRUE;
      curInstChanged = FALSE;
    }
    if (instBtn && enterBtn.justPressed && !extInstEditMode) {
      curInst = TOTAL_ACC;
      curFlam = 0;
      needLcdUpdate = TRUE;
    }
    if (instBtn && stopBtn.justPressed && !extInstEditMode) {
      curInst = TRIG_OUT;
      curFlam = 0;
      needLcdUpdate = TRUE;
    }
    if (shiftBtn && guideBtn.justPressed && !extInstEditMode) {
      curInst = EXT_INST;
      curFlam = 0;
      needLcdUpdate = TRUE;
    }
    //---------still in: if (curSeqMode == PTRN_STEP || curSeqMode == PTRN_TAP) ------

    //-------------------Clear Button for STEP Mode------------------------------
    if (clearBtn.pressed && !keyboardMode && curSeqMode != PTRN_TAP && isRunning) {


      if (clearBtn.justPressed) prev_muteInst = muteInst;  // [zabox] save mute state
      muteInst |= (1 << curInst);                          // [zabox] mute current instrument while holding clear

      bitClear(pattern[ptrnBuffer].inst[curInst], curStep);
      pattern[ptrnBuffer].velocity[curInst][curStep] = instVelLow[curInst];
      if (curInst == CH) pattern[ptrnBuffer].velocity[CH][curStep] = instVelHigh[HH];  //update HH velocity that OH is trigged correctly
      patternWasEdited = TRUE;
    }

    if (clearBtn.justRelease) muteInst = prev_muteInst;  // [zabox] unmute


    if (shiftBtn && clearBtn.justPressed && !keyboardMode && !isRunning) {  //curSeqMode != PTRN_TAP &&
      //clear full pattern
      for (int a = 0; a < NBR_INST; a++) {
        pattern[ptrnBuffer].inst[a] = 0;
      }
      //init all intrument velocity
      for (int b = 0; b < NBR_STEP; b++) {
        pattern[ptrnBuffer].velocity[BD][b] = instVelLow[BD];
        pattern[ptrnBuffer].velocity[SD][b] = instVelLow[SD];
        pattern[ptrnBuffer].velocity[LT][b] = instVelLow[LT];
        pattern[ptrnBuffer].velocity[MT][b] = instVelLow[MT];
        pattern[ptrnBuffer].velocity[HT][b] = instVelLow[HT];
        pattern[ptrnBuffer].velocity[RM][b] = instVelLow[RM];
        pattern[ptrnBuffer].velocity[HC][b] = instVelLow[HC];
        pattern[ptrnBuffer].velocity[RIDE][b] = instVelLow[RIDE];
        pattern[ptrnBuffer].velocity[CRASH][b] = instVelLow[CRASH];
      }
      // [TR-909 STYLE] Clear all external tracks
      for (byte t = 0; t < 16; t++) {
        pattern[ptrnBuffer].extTrack[t] = 0;
      }
      pattern[ptrnBuffer].shuffle = DEFAULT_SHUF;
      pattern[ptrnBuffer].flam = DEFAULT_FLAM;  // [1.028] flam
      pattern[ptrnBuffer].length = NBR_STEP - 1;
      pattern[ptrnBuffer].scale = SCALE_16;
      keybOct = DEFAULT_OCT;
      patternWasEdited = TRUE;
      needLcdUpdate = TRUE;
    }


    //-------------------shuffle Button------------------------------                          [zabox] test
    if (shufBtn.justPressed || shufBtn.justRelease) needLcdUpdate = TRUE;

    if (shufBtn.pressed) {

      if (readButtonState) {
        if (FirstBitOn() < 7) {
          pattern[ptrnBuffer].shuffle = FirstBitOn() + 1;
          if (pattern[ptrnBuffer].shuffle != prevShuf) {

            lcd.setCursor(8 + prevShuf, 0);  // [zabox] [1.027] flam
            lcd.print((char)161);            //

            prevShuf = pattern[ptrnBuffer].shuffle;

            lcd.setCursor(8 + prevShuf, 0);  // [zabox] [1.027] flam
            lcd.print((char)219);            //

            patternWasEdited = TRUE;
          }
        } else if (FirstBitOn() > 7) {
          pattern[ptrnBuffer].flam = FirstBitOn() - 8;
          if (pattern[ptrnBuffer].flam != prevFlam) {

            lcd.setCursor(8 + prevFlam, 1);  // [zabox] [1.027] flam
            lcd.print((char)161);            //

            prevFlam = pattern[ptrnBuffer].flam;

            lcd.setCursor(8 + prevFlam, 1);  // [zabox] [1.027] flam
            lcd.print((char)219);            //

            patternWasEdited = TRUE;
          }
        }
      }
    }

    //-------------------scale button------------------------------
    if (scaleBtn.justPressed && !keyboardMode) {
      needLcdUpdate = TRUE;
      patternWasEdited = TRUE;
      scaleBtn.counter++;
      if (scaleBtn.counter == 4) scaleBtn.counter = 0;
      switch (scaleBtn.counter) {
        case 0:
          pattern[ptrnBuffer].scale = SCALE_16;  // 1/16
          break;
        case 1:
          pattern[ptrnBuffer].scale = SCALE_32;  // 1/32
          break;
        case 2:
          pattern[ptrnBuffer].scale = SCALE_8t;  // 1/8t
          break;
        case 3:
          pattern[ptrnBuffer].scale = SCALE_16t;  // 1/16t
          break;
      }
    }

    //-------------------last step button------------------------------
    if (lastStepBtn.pressed && readButtonState) {
      pattern[ptrnBuffer].length = FirstBitOn();
      needLcdUpdate = TRUE;
      patternWasEdited = TRUE;
    }
    //---------still in: if (curSeqMode == PTRN_STEP || curSeqMode == PTRN_TAP) ------

    //-------------------Steps buttons------------------------------
    /////////////////////////////STEP EDIT + PATTERN SELECTION FOR STEP & TAP ///////////////////////////////////////////////////////////////////

    if (stepsBtn.justRelease) doublePush = FALSE;
    if (!lastStepBtn.pressed && !instBtn && !keyboardMode && !shufBtn.pressed) {  // [zabox] test
      if (curSeqMode == PTRN_STEP && isRunning)                                   //step programming
      {
        pattern[ptrnBuffer].inst[curInst] = InstValueGet(pattern[ptrnBuffer].inst[curInst]);  //cf InstValueGet()
      } else if (!isRunning) {                                                                //Return pattern number
        if (stepsBtn.pressed) {
          if (bankBtn.pressed) {
            if (FirstBitOn() > MAX_BANK) curBank = MAX_BANK;
            else curBank = FirstBitOn();
            nextPattern = curBank * NBR_PATTERN + (curPattern % NBR_PATTERN);
            if (curPattern != nextPattern) {
              LoadPatternBank(curBank);  //[oort] measure time requirement
              selectedPatternChanged = TRUE;
            }
            group.length = 0;  //[oort] maybe keep groups on bank change? Not implemented yet.
            group.pos = 0;
          } else {  //pattern group edit------------------------------------------------------
            if (SecondBitOn()) {
              group.length = SecondBitOn() - FirstBitOn();
              nextPattern = group.firstPattern = FirstBitOn() + curBank * NBR_PATTERN;
              doublePush = TRUE;
              group.priority = TRUE;
              group.pos = 0;  //[oort] newly selected pattern group/chain plays from first pattern

              //Store groupe in eeprom  //[oort] no group storage
              /* if(enterBtn.justPressed){
                  group.priority = FALSE;
                  byte tempLength;
                  byte tempPos;
                  //Test if one the  selected pattern is already in a Group
                  for (int a = 0; a <= group.length; a++){
                    tempLength = LoadPatternGroup(group.firstPattern + a, LENGTH);
                    if (tempLength){
                      tempPos = LoadPatternGroup(group.firstPattern + a, POSITION);
                      ClearPatternGroup(group.firstPattern + a - tempPos, tempLength);
                    }
                  }
                  SavePatternGroup(group.firstPattern, group.length);
                } */

            } else if (!doublePush) {
              group.priority = FALSE;
              nextPattern = FirstBitOn() + curBank * NBR_PATTERN;

              /* [oort] no group storage
                if(enterBtn.justPressed){
                  ClearPatternGroup(nextPattern - pattern[ptrnBuffer].groupPos, pattern[ptrnBuffer].groupLength);
                  group.length = 0;
                } 
              //group.pos = pattern[ptrnBuffer].groupPos;
              */

              group.pos = 0;  //[oort]
            }
            needLcdUpdate = TRUE;
            if (curPattern != nextPattern || curPattern == group.firstPattern) selectedPatternChanged = TRUE;
          }
        }
      }
    }

    //////////////////////////////TAP EDIT////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (curSeqMode == PTRN_TAP) {
      //static byte lastHHtrigged;// remember last OH or CH was trigged to prevent OH noise when trigget other instruments

      if (clearBtn.pressed) {  // [zabox] new function in tap mode: hold clear + inst in tap mode

        if (clearBtn.justPressed) prev_muteInst = muteInst;  // [zabox] save mute state
        if (readButtonState) {

          byte clearInst;

          switch (FirstBitOn()) {
            case BD_BTN:
            case BD_LOW_BTN:
              clearInst = BD;
              break;
            case SD_BTN:
            case SD_LOW_BTN:
              clearInst = SD;
              break;
            case LT_BTN:
            case LT_LOW_BTN:
              clearInst = LT;
              break;
            case MT_BTN:
            case MT_LOW_BTN:
              clearInst = MT;
              break;
            case HT_BTN:
            case HT_LOW_BTN:
              clearInst = HT;
              break;
            case RM_BTN:
              clearInst = RM;
              break;
            case HC_BTN:
              clearInst = HC;
              break;
            case CH_BTN:
            case CH_LOW_BTN:
              if (doublePush == 0) {
                clearInst = CH;
              }
              break;
            case RIDE_BTN:
              clearInst = RIDE;
              break;
            case CRASH_BTN:
              clearInst = CRASH;
              break;
          }
          if (readButtonState == OH_BTN) {
            clearInst = OH;
            doublePush = 1;
          }


          muteInst = prev_muteInst | (1 << clearInst);  // [zabox] mute inst

          bitClear(pattern[ptrnBuffer].inst[clearInst], curStep);
          pattern[ptrnBuffer].velocity[clearInst][curStep] = instVelLow[clearInst];   // [zabox] was missing here
          if (clearInst == CH) pattern[ptrnBuffer].velocity[CH][curStep] = HIGH_VEL;  //update HH velocity that OH is trigged correctly
          patternWasEdited = TRUE;
        } else if (muteInst != prev_muteInst) muteInst = prev_muteInst;  // [zabox] unmute
      }

      if (clearBtn.justRelease) muteInst = prev_muteInst;  // [zabox] unmute

      if (!lastStepBtn.pressed && !instBtn && !clearBtn.pressed && !shufBtn.pressed)  // [zabox] test //[oort] unclear what's tested
      {
        static boolean doublePushOH;
        static byte tempVel[16];  //store temp instrument velocity
        static byte triggedInst;

        if (bitRead(readButtonState, 12) && bitRead(readButtonState, 13)) doublePushOH = 1;
        else doublePushOH = 0;

        for (byte a = 0; a < NBR_STEP_BTN; a++) { //[oort] long loop, see end mark
          stepBtn[a].curState = bitRead(readButtonState, a);

          if (stepBtn[a].curState != stepBtn[a].prevState) {
            if ((stepBtn[a].pressed == LOW) && (stepBtn[a].curState == HIGH)) {
              //the "a" button is pressed NOW!
              switch (a) {
                case BD_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[BD];
                  tempVel[BD] = instVelHigh[BD];
                  triggedInst = BD;
                  break;
                case BD_LOW_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelLow[BD];
                  tempVel[BD] = instVelLow[BD];
                  triggedInst = BD;
                  break;
                case SD_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[SD];
                  tempVel[SD] = instVelHigh[SD];
                  triggedInst = SD;
                  break;
                case SD_LOW_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelLow[SD];
                  tempVel[SD] = instVelLow[SD];
                  triggedInst = SD;
                  break;
                case LT_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[LT];
                  tempVel[LT] = instVelHigh[LT];
                  triggedInst = LT;
                  break;
                case LT_LOW_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelLow[LT];
                  tempVel[LT] = instVelLow[LT];
                  triggedInst = LT;
                  break;
                case MT_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[MT];
                  tempVel[MT] = instVelHigh[MT];
                  triggedInst = MT;
                  break;
                case MT_LOW_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelLow[MT];
                  tempVel[MT] = instVelLow[MT];
                  triggedInst = MT;
                  break;
                case HT_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[HT];
                  tempVel[HT] = instVelHigh[HT];
                  triggedInst = HT;
                  break;
                case HT_LOW_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelLow[HT];
                  tempVel[HT] = instVelLow[HT];
                  triggedInst = HT;
                  break;
                case CH_BTN:
                  if (!doublePushOH) {
                    if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[CH];
                    tempVel[CH] = instVelHigh[CH];
                    triggedInst = CH;
                  }
                  break;
                case CH_LOW_BTN:
                  if (!doublePushOH) {
                    if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelLow[CH];
                    tempVel[CH] = instVelLow[CH];
                    triggedInst = CH;
                  }
                  break;
                case RM_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[RM];
                  tempVel[RM] = instVelHigh[RM];
                  triggedInst = RM;
                  break;
                case HC_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[HC];
                  tempVel[HC] = instVelHigh[HC];
                  triggedInst = HC;
                  break;
                case CRASH_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[CRASH];
                  tempVel[CRASH] = instVelHigh[CRASH];
                  triggedInst = CRASH;
                  break;
                case RIDE_BTN:
                  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[RIDE];
                  tempVel[RIDE] = instVelHigh[RIDE];
                  triggedInst = RIDE;
                  break;
              }
              buttonTapped = TRUE;  //[oort] this is set every tap

              if (doublePushOH) {
                //  if (isRunning) pattern[ptrnBuffer].velocity[instOut[a]][tapStepCount] = instVelHigh[OH];
                if (isRunning) pattern[ptrnBuffer].velocity[OH][tapStepCount] = instVelHigh[OH];  // [zabox] fixes oh low velocity bug in tap mode (dim leds, no sound after mux fix)
                tempVel[OH] = instVelHigh[OH];
                triggedInst = OH;
              }

              //-----SET Velocity Values-----//
              SetMuxTrigMidi(triggedInst, tempVel[triggedInst]);

              if (a == CH_LOW_BTN || a == CH_BTN || readButtonState == OH_BTN) {
                //If OH is tapped
                if (doublePushOH) {
                  lastHHtrigged = 0;
                  if (isRunning) {
                    bitSet(tempInst[OH], tapStepCount);
                    bitClear(tempInst[CH], tapStepCount);
                  }
                }
                //If CH is tapped
                else {
                  lastHHtrigged = B10;
                  if (isRunning) {
                    bitSet(tempInst[CH], tapStepCount);
                    bitClear(tempInst[OH], tapStepCount);
                  }
                }
                while (TCCR2B) {};  // [zabox] [1.028] wait until the last trigger is low again (checks if timer2 is running)
                SetDoutTrig((1 << HH) | lastHHtrigged);
              } else {
                while (TCCR2B) {};  // [zabox] [1.028] wait until the last trigger is low again (checks if timer2 is running)
                //SetDoutTrig(1 << instOut[a] | lastHHtrigged);
                SetDoutTrig((1 << instOut[a]) | (lastDoutTrig & B11));  // [zabox] [1.027] fixes hh cuts when tapping other instruments
                if (isRunning) bitSet(tempInst[instOut[a]], tapStepCount);
              }
              delayMicroseconds(2000);
              //SetDoutTrig(lastHHtrigged);
              SetDoutTrig(lastDoutTrig & B11);  // [zabox] [1.027] fixes hh cuts when tapping other instruments
            }
          }
          stepBtn[a].prevState = stepBtn[a].curState;
        }  //END FOR LOOP
      }

      //[oort] This section is probably inefficient
      //[oort] align this to every 4th step reduce load? Or does that affect playback?
      if (buttonTapped) {  //Update pattern at the end of measure to not get a double trig
        for (int inst = 0; inst < NBR_INST; inst++) {
          if (tempInst[OH]) {
            unsigned int i = tempInst[OH] & pattern[ptrnBuffer].inst[CH];  //pattern[ptrnBuffer] or bufferedPattern
            pattern[ptrnBuffer].inst[CH] ^= i;
            pattern[ptrnBuffer].inst[OH] |= tempInst[OH];
            tempInst[OH] = 0;  // init tempInst
            patternWasEdited = TRUE;
          } else if (tempInst[CH]) {
            unsigned int i = tempInst[CH] & pattern[ptrnBuffer].inst[OH];
            pattern[ptrnBuffer].inst[OH] ^= i;
            pattern[ptrnBuffer].inst[CH] |= tempInst[CH];
            tempInst[CH] = 0;  // init tempInst
            patternWasEdited = TRUE;
          } else if (tempInst[inst])  //if instruments was edited
          {
            pattern[ptrnBuffer].inst[inst] |= tempInst[inst];
            tempInst[inst] = 0;       // init tempInst
            patternWasEdited = TRUE;
          }
        }
        SetHHPattern(&pattern[ptrnBuffer]);
        InstToStepWord(&pattern[ptrnBuffer]);
        memcpy(&bufferedPattern, &pattern[ptrnBuffer], sizeof(Pattern));  //[oort] a bit heavy but must do since it's cleared at "endMeasure"
      }
    }  //END IF PTRN_TAP MODE
  }    //ENDIF MODE STEP & TAP EDIT


  //===============================================================================================================================================
  //////////////////////////MODE PATTERN PLAY...///////////////////////////////////////////////////////////////////////////////////////////////////

  if (curSeqMode == PTRN_PLAY) {
    //-------------------------------select pattern-----------------------------------
    if (stepsBtn.justRelease) doublePush = FALSE;
    if (readButtonState) {
      if (bankBtn.pressed) {                               //[oort] make function instead
        if (FirstBitOn() > MAX_BANK) curBank = MAX_BANK;
        else curBank = FirstBitOn();
        nextPattern = curBank * NBR_PATTERN + (curPattern % NBR_PATTERN);
        if (curPattern != nextPattern) {
          LoadPatternBank(curBank);  //[oort]
          selectedPatternChanged = TRUE;
        }
        //[oort] reset groups
        group.priority = FALSE;
        group.length = 0; 
        needLcdUpdate = TRUE;

      } else {
        //Group selected
        if (SecondBitOn()) {
          group.length = SecondBitOn() - FirstBitOn();
          needLcdUpdate = TRUE;
          nextPattern = group.firstPattern = FirstBitOn() + curBank * NBR_PATTERN;
          //if (isRunning && (seq.ptrnChangeSync == SYNC)) {  // [zabox] fixes group bug while running //[oort] probably not                                                  // [zabox] fixes group bug while running
            //group.pos = group.length;
          //}
          doublePush = TRUE;
          group.priority = TRUE;
          group.pos = 0;  //[oort], reset to first bar on new group/chain selection
        }
        //Only one pattern selected
        else if (!doublePush) {
          group.priority = FALSE;
          group.pos = 0;
          nextPattern = FirstBitOn() + curBank * NBR_PATTERN;
        }

        if (curPattern != nextPattern || curPattern == group.firstPattern) {  // [zabox] [1.027] fixes pattern change bug in slave mode
          needLcdUpdate = TRUE;
          selectedPatternChanged = TRUE;
        }
      }
    }

    //--------------------------------sequencer run direction-----------------------
    if (shiftBtn && dirBtn.justPressed) {
      if (seq.dir++ >= MAX_SEQ_DIR) seq.dir = FORWARD;
    }
  }  //END IF MODE PLAY PATTERN

  //===================================================================================================================================

  //===================================================================================================================================

  //////////////////////////MODE TRACK WRITE//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //[oort] All unsaved patterns will be lost when entering TRACK WRITE or TRACK PLAY

  if (curSeqMode == TRACK_WRITE) {
    //-------------------------------select pattern-----------------------------------
    if (readButtonState) {

      if (bankBtn.pressed) {
        if (FirstBitOn() >= MAX_BANK) curBank = MAX_BANK;
        else curBank = FirstBitOn();
        nextPattern = curBank * NBR_PATTERN + (curPattern % NBR_PATTERN);
        if (curPattern != nextPattern) selectedPatternChanged = TRUE;  //[oort] basically, all these are redundant curPattern != nextPattern at the end all that's needed
      } else if (numBtn.pressed) {
        trk.next = FirstBitOn();  //[oort] we should try for pattern length <4 not allowed
        selectedTrackChanged = TRUE;
        needLcdUpdate = TRUE;
      } else {
        nextPattern = FirstBitOn() + curBank * NBR_PATTERN;
        if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      }
    }
    //decrease track position
    if (backBtn.justPressed) {
      trk.pos--;
      if (trk.pos < 0 || trk.pos > MAX_PTRN_TRACK) trk.pos = 0;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      needLcdUpdate = TRUE;
    }
    //increment track position
    if (fwdBtn.justPressed) {
      trk.pos++;
      if (trk.pos > MAX_PTRN_TRACK) trk.pos = MAX_PTRN_TRACK;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      needLcdUpdate = TRUE;
    }
    //go to first measure
    if (clearBtn.justPressed) {
      trk.pos = 0;
      displayTrkPlayPos = 0;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      needLcdUpdate = TRUE;
    }
    //end of track
    if (lastStepBtn.justPressed) {
      if (shiftBtn) {                               //delete END_OF_TRACK
        for (int i = 1; i < MAX_PTRN_TRACK; i++) {  //[oort] start at 0 makes no sense
          if (track[trkBuffer].patternNbr[i] == END_OF_TRACK)
            track[trkBuffer].patternNbr[i] = track[trkBuffer].patternNbr[i - 1];  //[oort] replace END_OF_TRACK with previous
          //track[trkBuffer].length = (track[trkBuffer].length) - 1; //[oort] not working for unknown reason
        }
      } else {  // Set end of track
        track[trkBuffer].patternNbr[trk.pos] = END_OF_TRACK;
        track[trkBuffer].length = trk.pos + 1;
        trackNeedSaved = TRUE;
        needLcdUpdate = TRUE;
        curPattern = END_OF_TRACK;
        nextPattern = END_OF_TRACK;
      }
    }
    if (shiftBtn) {
      //go to last measure
      if (numBtn.pressed) {
        trk.pos = track[trkBuffer].length;
        nextPattern = track[trkBuffer].patternNbr[trk.pos];
        if (curPattern != nextPattern) selectedPatternChanged = TRUE;
        needLcdUpdate = TRUE;
      }
      //delete current pattern in the current position
      if (backBtn.justPressed && track[trkBuffer].length) {  // [zabox] fixes crashs when deleting the last track pos
        if (trk.pos < (track[trkBuffer].length - 1)) {       // [zabox] delete only valid track pos
          for (int a = trk.pos + 1; a < track[trkBuffer].length; a++) {
            track[trkBuffer].patternNbr[a] = track[trkBuffer].patternNbr[a + 1];
          }
          track[trkBuffer].patternNbr[track[trkBuffer].length - 1] = 0;  // [Neuromancer] Delete pattern info at tail
          trk.pos += 1;                                                  //to stay in the same position
          track[trkBuffer].length = track[trkBuffer].length - 1;         //decremente length by 1 du to deleted pattern
          nextPattern = track[trkBuffer].patternNbr[trk.pos];
          if (curPattern != nextPattern) selectedPatternChanged = TRUE;
          trackNeedSaved = TRUE;
          needLcdUpdate = TRUE;
        } else {
          trk.pos += 1;  //to stay in the same position
        }
      }
      //insert a pattern
      if (fwdBtn.justPressed) {
        if (trk.pos < (track[trkBuffer].length + 1)) {  // [zabox] insert only inside track
          for (int a = track[trkBuffer].length + 1; a >= trk.pos; a--) {
            track[trkBuffer].patternNbr[a] = track[trkBuffer].patternNbr[a - 1];
          }
          trk.pos -= 1;  //to stay in the same position
          track[trkBuffer].patternNbr[trk.pos] = curPattern;
          track[trkBuffer].length = track[trkBuffer].length + 1;  //decremente length by 1 du to deleted pattern
          nextPattern = track[trkBuffer].patternNbr[trk.pos];
          if (curPattern != nextPattern) selectedPatternChanged = TRUE;
          trackNeedSaved = TRUE;
          needLcdUpdate = TRUE;
        } else {
          trk.pos -= 1;  //to stay in the same position
        }
      }
      if (clearBtn.justPressed) {
        trk.pos = 0;
        for (int a = 0; a < MAX_PTRN_TRACK; a++) {
          track[trkBuffer].patternNbr[a] = 0;
        }
        track[trkBuffer].length = 0;
        nextPattern = track[trkBuffer].patternNbr[trk.pos];
        if (curPattern != nextPattern) selectedPatternChanged = TRUE;
        trackNeedSaved = TRUE;
        needLcdUpdate = TRUE;
      }
    }  //end shift

    //write selected pattern in the current track position
    if (enterBtn.justRelease && !trackJustSaved) {
      track[trkBuffer].patternNbr[trk.pos] = curPattern;
      trk.pos++;
      if (trk.pos > MAX_PTRN_TRACK) trk.pos = MAX_PTRN_TRACK;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (nextPattern == 0 && curPattern != 0) {
        nextPattern = curPattern;
        selectedPatternChanged = TRUE;
      }
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      if (track[trkBuffer].length < trk.pos)  // Only change the length when larger
      {
        track[trkBuffer].length = trk.pos;
      }
      trackNeedSaved = TRUE;
      needLcdUpdate = TRUE;
    }
  }  //END IF MODE TRACK WRITE

  //////////////////////////MODE TRACK PLAY///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  if (curSeqMode == TRACK_PLAY || (curSeqMode == MUTE && prevSeqMode == TRACK_PLAY)) {  //[oort] also Mute in Track mode
  
    //go to first measure
    if (clearBtn.justPressed) {
      trk.pos = 0;
      displayTrkPlayPos = 0;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
      needLcdUpdate = TRUE;
    }
    if (readButtonState && curSeqMode != MUTE) {
      trk.next = FirstBitOn();
      selectedTrackChanged = TRUE;
      needLcdUpdate = TRUE;
    }
  }

  //========================================================================================================================================================
  //------------------------------------Update pattern track load/save-----------------------
  if (selectedTrackChanged) {
    selectedTrackChanged = FALSE;
    needLcdUpdate = TRUE;
    trackNeedSaved = FALSE;
    LoadTrack(trk.next);
    trk.current = trk.next;
    nextPattern = track[trkBuffer].patternNbr[trk.pos];
    if (curPattern != nextPattern) selectedPatternChanged = TRUE;
    //trkBuffer = !trkBuffer;
  }

  if (trackNeedSaved && enterBtn.hold) {
    trackNeedSaved = FALSE;
    SaveTrack(trk.current);
    LcdPrintSaved();
    trackJustSaved = TRUE;
    timeSinceSaved = millis();
  }
  //this function is to not increment trk.pos when released enterBtn after Saved track
  if (millis() - timeSinceSaved > HOLD_TIME) {
    trackJustSaved = FALSE;
  }

  //[oort] new version with patternBank in local RAM
  if (patternWasEdited) {  //update Pattern
    patternWasEdited = FALSE;

    patternBankNeedsSave = TRUE; //[oort] not yet TO DO
    if (curSeqMode == PTRN_TAP) {  //[oort] in tap mode we are one step behind, see: endMeasure
      editedPatterns[curPattern - curBank * NBR_PATTERN] = TRUE;  //[oort] pattern of this index needs to be saved to EEprom
      memcpy(&patternBank[curPattern - curBank * NBR_PATTERN], &bufferedPattern, sizeof(Pattern)); //[oort] edits saved in patternBank RAM
    } else {
      SetHHPattern(&pattern[ptrnBuffer]);
      InstToStepWord(&pattern[ptrnBuffer]);
      editedPatterns[curPattern - curBank * NBR_PATTERN] = TRUE; //[oort] pattern of this index needs to be saved to EEprom
      memcpy(&patternBank[curPattern - curBank * NBR_PATTERN], &pattern[ptrnBuffer], sizeof(Pattern)); //[oort] edits saved in patternBank RAM
    }
    needLcdUpdate = TRUE;
  }

  //[oort] no group storage
  /* 
  if ((groupNeedsSave || patternNeedsSave) && enterBtn.justPressed && !instBtn) {
    patternNeedsSave = FALSE;
    if (group.length) {
      for (int i = 0; i <= group.length; i++) {
        if (bitRead(groupPatternEdited, i)) {
          // Pattern needs saving
          //memcpy(&pattern[ptrnBuffer], &patternGroup[i], sizeof(Pattern)); //[oort] disabled patternGroup
          SavePattern(group.firstPattern + i);
        }
      }
      groupNeedsSave = FALSE;
    } else {
      SavePattern(curPattern);  //pattern saved //[oort] ordinary save
    }
    LcdPrintSaved();
  } */


  //[oort] save entire pattern bank from RAMbuffer to EEprom
  if (patternBankNeedsSave && enterBtn.justPressed && !instBtn) {

    for (int i = 0; i < NBR_PATTERN; i++) {
      if (editedPatterns[i]) {
        memcpy(&pattern[ptrnBuffer], &patternBank[i], sizeof(Pattern));
        SavePattern(curBank * NBR_PATTERN + i);    //save to EE-prom, only implemented for specific buffer - call using pointer to buffer instead?
        editedPatterns[i] = FALSE;
      }
    }
    patternBankNeedsSave = FALSE;  //[oort] not 100% certain on this placement
    LcdPrintSaved();  //[oort] not visible

    //[oort] restore buffer (room for improvements here)
    memcpy(&pattern[ptrnBuffer], &patternBank[curPattern - curBank * NBR_PATTERN], sizeof(Pattern));
  }

  if (selectedPatternChanged) {
    selectedPatternChanged = FALSE;
    needLcdUpdate = TRUE;  //selected pattern changed so we need to update display
    if (nextPattern != END_OF_TRACK)  //[oort] why this, track unique things should not be handled here
    {
      if (curSeqMode == TRACK_PLAY || curSeqMode == TRACK_WRITE || (prevSeqMode == TRACK_PLAY && curSeqMode == MUTE)) {
        LoadPattern(nextPattern);                                                 //[oort] in track modes we access EE-prom directly
      } else                                                                      //[oort] we should test for bank change/bank loading to be safe?
      {                                                                           //load directly from local RAM
        int nextIndex = nextPattern - curBank * NBR_PATTERN;
        if (nextIndex > 15) nextIndex = 15;                                       //[oort] rudimentary safety, this should never happen
        memcpy(&pattern[!ptrnBuffer], &patternBank[nextIndex], sizeof(Pattern));  //load into twin buffer
        if (curSeqMode == PTRN_TAP)
          memcpy(&bufferedPattern, &patternBank[nextIndex], sizeof(Pattern));     //tap needs extra buffer
      }
    }

    //prepare twin pattern buffer before swapping
    playingPattern = curPattern;  //used by LCD
    //curPattern = nextPattern; //[oort] moved this into clock.ino
    keybOct = DEFAULT_OCT;
    //noteIndex = 0;        //[oort] Should we not reset noteIndexCpt instead?
    noteIndexCpt = 1;       //[oort] possibility to fetch remembered position here? TO DO?
    InitMidiNoteOff();                   //[oort] can all these be moved to read from EE-prom? Probably not needed in RAM-buffers
    InitPattern(&pattern[!ptrnBuffer]);  //SHOULD BE REMOVED WHEN EEPROM WILL BE INITIALIZED ([oort] don't understand this old comment)
    SetHHPattern(&pattern[!ptrnBuffer]);

    for (byte stp = 0; stp < 16; stp = stp + 4) { //[oort] metronome solution 1 - put velocity values at 0, 4, 8, 12
      if (pattern[!ptrnBuffer].velocity[RM][stp] == 0) {
        pattern[!ptrnBuffer].velocity[RM][stp] = (stp == 0) ? instVelHigh[RM] : instVelLow[RM];
      }
    }
    InstToStepWord(&pattern[!ptrnBuffer]);

    nextPatternReady = TRUE;

    //[oort] Change on bar or directly
    if (isRunning && seq.ptrnChangeSync == SYNC) {
      // [oort] will be swapped at end of last measure in clock.ino
    } else  // [oort] not running or direct change
    {
      nextPatternReady = FALSE;
      ptrnBuffer = !ptrnBuffer;  //[oort] twin buffer swap
      curPattern = nextPattern;
      needLcdUpdate = TRUE;  //selected pattern changed so we need to update display
    }
  }

  if (enterBtn.justRelease) needLcdUpdate = TRUE;

  //////////////////////////MODE MUTE//////////////////////////////////////

  if (curSeqMode == MUTE) {
    MuteButtonGet();
    if (encBtn.pressed) { //[oort] reset mute
#if SHIFT_MUTE_ALL
      // Mute all instruments when shift is pressed
      if (shiftBtn) {
        // Avoid filling muteInst with all 1's
        for (byte a = 0; a < NBR_STEP_BTN; a++)
          muteInst |= (1 << muteOut[a]);
        muteLeds = ~0;
      } else {
        muteInst = 0;
        muteLeds = 0;  // [1.028] new MuteButtonGet function
        //InitMuteBtnCounter();                                                         //
      }
#else
      muteInst = 0;
      muteLeds = 0;  // [1.028] new MuteButtonGet function
                     //InitMuteBtnCounter();
#endif
    }
  }

  ////////////////////////// INCREMENT REACTIONS //////////////////////////////////////
  //depending on increments of group and track position in clock.ino last measure 
  //and by "sequencerJustStarted" on Start

  //Stay clear of bar change region (last step and first step excluded here) and execute once per bar:
  if (incrementRequired && stepCount > 1 && curSeqMode != TRACK_WRITE) {  // [oort] Maybe try > 0 instead?
    //group increment
    if (group.length) {  //[oort] for Track and Group positions //neuro comment: && stepCount > 0)
      if (group.pos > group.length) group.pos = 0;
      nextPattern = group.firstPattern + group.pos;
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
    }
    //track play increment
    if (curSeqMode == TRACK_PLAY || (prevSeqMode == TRACK_PLAY && curSeqMode == MUTE)) {
      if (trk.pos >= track[trkBuffer].length) trk.pos = 0;
      nextPattern = track[trkBuffer].patternNbr[trk.pos];
      if (curPattern != nextPattern) selectedPatternChanged = TRUE;
    }
    incrementRequired = FALSE;
    needLcdUpdate = TRUE;
  }
}
