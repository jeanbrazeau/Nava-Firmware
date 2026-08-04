//-------------------------------------------------
//                  NAVA v1.x
//                 SEQ Functions
//-------------------------------------------------

/////////////////////Function//////////////////////
//Initialise the sequencer before to run
void InitSeq() {
  LoadSeqSetup();
  LoadExtTrackNotes();  // [TR-909 STYLE] must precede any ext preview or playback
  ppqn = 0;
  stepCount = 0;
  extStepCount = 0;
  seq.configPage = 0;
  seq.configMode = FALSE;
  randomSeed(analogRead(0));
  seq.dir = FORWARD;
  curSeqMode = seq.BootMode;  //[oort] part of feature CONFIG_BOOTMODE, needs ifdef
  seq.bpm = seq.defaultBpm;
  SetSeqSync();  // [zabox] [1.028] moved
  seq.syncChanged = FALSE;
}

//Combine OH and CH pattern to trig HH and set total accent for ride and crash
/*
void SetHHPattern() {
  pattern[ptrnBuffer].inst[HH] = pattern[ptrnBuffer].inst[CH] | pattern[ptrnBuffer].inst[OH];
  if (curSeqMode != PTRN_TAP) {
    for (int a = 0; a < NBR_STEP; a++) {
      if (bitRead(pattern[ptrnBuffer].inst[CH], a) && curInst == CH) bitClear(pattern[ptrnBuffer].inst[OH], a);
      if (bitRead(pattern[ptrnBuffer].inst[OH], a) && curInst == OH) {
        bitClear(pattern[ptrnBuffer].inst[CH], a);
        pattern[ptrnBuffer].velocity[CH][a] = instVelHigh[HH];
      }
    }
  }
}*/

//Combine OH and CH pattern to trig HH and set total accent for ride and crash
//[oort] try use pointers
void SetHHPattern(Pattern* bufferToSet) {
  bufferToSet->inst[HH] = bufferToSet->inst[CH] | bufferToSet->inst[OH];
  if (curSeqMode != PTRN_TAP) {
    for (int a = 0; a < NBR_STEP; a++) {
      if (bitRead(bufferToSet->inst[CH], a) && curInst == CH) bitClear(bufferToSet->inst[OH], a);
      if (bitRead(bufferToSet->inst[OH], a) && curInst == OH) {
        bitClear(bufferToSet->inst[CH], a);
        bufferToSet->velocity[CH][a] = instVelHigh[HH];
      }
    }
  }
}

//init pattern
/*
void InitPattern() {
  if (!group.priority && !group.isLoaded) {
    group.length = pattern[ptrnBuffer].groupLength;
    group.firstPattern = curPattern - pattern[ptrnBuffer].groupPos;
  }
  // Backstop for any path that reaches a live buffer without going through the EEPROM
  // read, which is where the stored bias is decoded. An out-of-range ext length would
  // index extTrack[] past its 16 bits.
  if (pattern[ptrnBuffer].extLength >= NBR_STEP) pattern[ptrnBuffer].extLength = pattern[ptrnBuffer].length;
  //Init Ride, Crash velocity to HIGH_VEL
  for (int stp = 0; stp < NBR_STEP; stp++) {
    if (pattern[ptrnBuffer].velocity[CH][stp] == 0) pattern[ptrnBuffer].velocity[CH][stp] = instVelHigh[HH];  //HH
    pattern[ptrnBuffer].velocity[CRASH][stp] = instVelHigh[CRASH];                                            //CRASH
    pattern[ptrnBuffer].velocity[RIDE][stp] = instVelHigh[RIDE];                                              //RIDE
    pattern[ptrnBuffer].velocity[TOTAL_ACC][stp] = HIGH_VEL;                                                  //TOTAL_ACC
    pattern[ptrnBuffer].velocity[TRIG_OUT][stp] = HIGH_VEL;                                                   //TRIG_OUT
                                                                                                              //    pattern[ptrnBuffer].velocity[EXT_INST][stp] = HIGH_VEL;//EXT_INST
  }
  if (group.length) {
    prevShuf = pattern[ptrnBuffer].shuffle;
    prevFlam = pattern[ptrnBuffer].flam;
  }
  switch (pattern[ptrnBuffer].scale) {
    case SCALE_16:
      scaleBtn.counter = 0;
      break;
    case SCALE_32:
      scaleBtn.counter = 1;
      break;
    case SCALE_8t:
      scaleBtn.counter = 2;
      break;
    case SCALE_16t:
      scaleBtn.counter = 3;
      break;
  }
}*/

void InitPattern(Pattern* bufferToSet) {
  // Backstop for any path that reaches a live buffer without going through the EEPROM
  // read. The scale switch below is silent on a value it does not recognise, so an
  // unchecked record would leave scaleBtn.counter pointing at some other pattern's
  // setting while the sequencer divided by a scale it cannot use.
  SanitizePattern(bufferToSet);
  if (!group.priority && !group.isLoaded) {
    group.length = bufferToSet->groupLength;
    group.firstPattern = curPattern - bufferToSet->groupPos;
  }
  //Init Ride, Crash velocity to HIGH_VEL
  for (int stp = 0; stp < NBR_STEP; stp++) {
    if (bufferToSet->velocity[CH][stp] == 0) bufferToSet->velocity[CH][stp] = instVelHigh[HH];  //HH
    bufferToSet->velocity[CRASH][stp] = instVelHigh[CRASH];                                            //CRASH
    bufferToSet->velocity[RIDE][stp] = instVelHigh[RIDE];                                              //RIDE
    bufferToSet->velocity[TOTAL_ACC][stp] = HIGH_VEL;                                                  //TOTAL_ACC
    bufferToSet->velocity[TRIG_OUT][stp] = HIGH_VEL;                                                   //TRIG_OUT
    // EXT_INST: no longer read by playback - the ext lane takes its two levels from
    // extAccent[] per track. Still written at the high value so that a build predating
    // that change, reading a pattern this one saved, plays the ext tracks as before.
    bufferToSet->velocity[EXT_INST][stp] = instVelHigh[EXT_INST];
  }
  if (group.length) {
    prevShuf = bufferToSet->shuffle;
    prevFlam = bufferToSet->flam;
  }
  switch (bufferToSet->scale) {
    case SCALE_16:
      scaleBtn.counter = 0;
      break;
    case SCALE_32:
      scaleBtn.counter = 1;
      break;
    case SCALE_8t:
      scaleBtn.counter = 2;
      break;
    case SCALE_16t:
      scaleBtn.counter = 3;
      break;
  }
}

//Convert Instrument Word to Step Word
/*void InstToStepWord() {
  for (int a = 0; a < NBR_STEP; a++) {
    pattern[ptrnBuffer].step[a] = 0;
    for (int b = 0; b < NBR_INST; b++) {
      if (bitRead(pattern[ptrnBuffer].inst[b], a)) bitSet(pattern[ptrnBuffer].step[a], b);
    }
  }
}*/

//Convert Instrument Word to Step Word
void InstToStepWord(Pattern* bufferToSet) {
  for (int a = 0; a < NBR_STEP; a++) {
    bufferToSet->step[a] = 0;
    for (int b = 0; b < NBR_INST; b++) {
      if (bitRead(bufferToSet->inst[b], a)) bitSet(bufferToSet->step[a], b);
    }
  }
}

/////////////////////Pattern Initialisations in second buffer (!ptrnBuffer) //////////////////////
//[oort] these is unneccesary if we add argument ptrnBuffer to these functions TO DO

//init pattern  //[oort] comment: this is executed when a pattern has been loaded from EEprom, maybe I can do without this when using fast local RAM?
/*
void InitPattern_2ndBuffer() {
  if (!group.priority) {  //[oort] comment: only place where this boolean is used, Neuro also different here, can probably be removed

    //[oort] bug search, disabling saved groups
    group.length = 0;
    group.firstPattern = 0;

    //[oort] original code
    //group.length = pattern[ptrnBuffer].groupLength;
    //group.firstPattern = curPattern - pattern[ptrnBuffer].groupPos;
  }
  //Init Ride, Crash velocity to HIGH_VEL .    //[oort] comment: why is this performed every time a new pattern is loaded? These values should be written instead?
  for (int stp = 0; stp < NBR_STEP; stp++) {
    if (pattern[!ptrnBuffer].velocity[CH][stp] == 0) pattern[!ptrnBuffer].velocity[CH][stp] = instVelHigh[HH];  //HH
    pattern[!ptrnBuffer].velocity[CRASH][stp] = instVelHigh[CRASH];                                             //CRASH
    pattern[!ptrnBuffer].velocity[RIDE][stp] = instVelHigh[RIDE];                                               //RIDE
    pattern[!ptrnBuffer].velocity[TOTAL_ACC][stp] = HIGH_VEL;                                                   //TOTAL_ACC
    pattern[!ptrnBuffer].velocity[TRIG_OUT][stp] = HIGH_VEL;                                                    //TRIG_OUT
    pattern[!ptrnBuffer].velocity[EXT_INST][stp] = HIGH_VEL;                                                    //EXT_INST
  }                                                                                                             //[oort] some group stuff here in Neuro
  switch (pattern[!ptrnBuffer].scale) {
    case SCALE_16:
      scaleBtn.counter = 0;
      break;
    case SCALE_32:
      scaleBtn.counter = 1;
      break;
    case SCALE_8t:
      scaleBtn.counter = 2;
      break;
    case SCALE_16t:
      scaleBtn.counter = 3;
      break;
  }
}

//Combine OH and CH pattern to trig HH and set total accent for ride and crash
void SetHHPattern_2ndBuffer() {
  pattern[!ptrnBuffer].inst[HH] = pattern[!ptrnBuffer].inst[CH] | pattern[!ptrnBuffer].inst[OH];
  if (curSeqMode != PTRN_TAP) {
    for (int a = 0; a < NBR_STEP; a++) {
      if (bitRead(pattern[!ptrnBuffer].inst[CH], a) && curInst == CH) bitClear(pattern[!ptrnBuffer].inst[OH], a);
      if (bitRead(pattern[!ptrnBuffer].inst[OH], a) && curInst == OH) {
        bitClear(pattern[!ptrnBuffer].inst[CH], a);
        pattern[!ptrnBuffer].velocity[CH][a] = instVelHigh[HH];
      }
    }
  }
}

//Convert Instrument Word to Step Word
void InstToStepWord_2ndBuffer() {
  for (int a = 0; a < NBR_STEP; a++) {
    pattern[!ptrnBuffer].step[a] = 0;
    for (int b = 0; b < NBR_INST; b++) {
      if (bitRead(pattern[!ptrnBuffer].inst[b], a)) bitSet(pattern[!ptrnBuffer].step[a], b);
    }
  }
}*/

/////////////////////Other pattern operations //////////////////////

// Force a pattern's four timing fields into the range the sequencer can actually run.
//
// These are not cosmetic ranges. scale is the divisor of `ppqn % scale` in CountPPQN()
// and shuffle is used as `shuffle[pattern.shuffle - 1]`, so a stored 0 in either byte
// does not play the pattern wrongly - it stops the sequencer dead while leaving
// isRunning TRUE. The machine latches PLAY, sounds nothing, moves no step LED, and still
// accepts step programming (which is gated on isRunning alone), so it reads as a
// firmware fault with nothing on the panel pointing at the pattern. length and flam are
// index sources too: length past NBR_STEP-1 walks curStep past the end of step[] and
// past the 16 bits of a step LED word, and flam past MAX_FLAM_TYPE-1 indexes flam[].
//
// Called wherever a record reaches a live buffer, because more than one route can carry
// a bad one: a paste from the copy buffer before anything was copied into it
// (bufferedPattern is BSS, and a bare MUTE press in TRACK_WRITE pastes), a SysEx restore
// of a record made elsewhere, or a part that was never fully written - a blank EEPROM
// reads 0xFF, which is out of range on all four.
void SanitizePattern(Pattern* p) {
  if (p->length >= NBR_STEP) p->length = DEFAULT_LEN - 1;
  // extLength is bounded by its own stored range, not by length: the ext lane is allowed
  // to loop shorter than the kit.
  if (p->extLength >= NBR_STEP) p->extLength = p->length;
  switch (p->scale) {
    case SCALE_16:
    case SCALE_32:
    case SCALE_8t:
    case SCALE_16t:
      break;
    default:
      p->scale = DEFAULT_SCALE;
      break;
  }
  if (p->shuffle < 1 || p->shuffle > MAX_SHUF_TYPE) p->shuffle = DEFAULT_SHUF;
  if (p->flam >= MAX_FLAM_TYPE) p->flam = DEFAULT_FLAM;
}

//copy pattern to buffer //[oort] note: patternNum is never used
void CopyPatternToBuffer(byte patternNum) {
  for (byte i = 0; i < NBR_INST; i++) {
    bufferedPattern.inst[i] = pattern[ptrnBuffer].inst[i];
  }
  bufferedPattern.length = pattern[ptrnBuffer].length;
  bufferedPattern.extLength = pattern[ptrnBuffer].extLength;  // copied with the ext tracks it bounds
  bufferedPattern.scale = pattern[ptrnBuffer].scale;
  bufferedPattern.shuffle = pattern[ptrnBuffer].shuffle;
  bufferedPattern.flam = pattern[ptrnBuffer].flam;
  bufferedPattern.totalAcc = pattern[ptrnBuffer].totalAcc;

  // [TR-909 STYLE] Copy external tracks with their velocity levels
  for (byte i = 0; i < 16; i++) {
    bufferedPattern.extTrack[i] = pattern[ptrnBuffer].extTrack[i];
    bufferedPattern.extAccent[i] = pattern[ptrnBuffer].extAccent[i];
  }

  for (byte i = 0; i < NBR_INST; i++) {  //loop as many instrument for a page
    for (byte j = 0; j < NBR_STEP; j++) {
      bufferedPattern.velocity[i][j] = pattern[ptrnBuffer].velocity[i][j];
    }
  }
}

//paste buffer to current pattern //[oort] note: patternNum is never used
void PasteBufferToPattern(byte patternNum) {
  for (byte i = 0; i < NBR_INST; i++) {
    pattern[ptrnBuffer].inst[i] = bufferedPattern.inst[i];
  }
  pattern[ptrnBuffer].length = bufferedPattern.length;
  // Guarded rather than copied blind: bufferedPattern is BSS until something is copied
  // into it, so a paste before any copy would otherwise install a garbage ext length.
  pattern[ptrnBuffer].extLength = (bufferedPattern.extLength < NBR_STEP)
                                    ? bufferedPattern.extLength
                                    : bufferedPattern.length;
  pattern[ptrnBuffer].scale = bufferedPattern.scale;
  pattern[ptrnBuffer].shuffle = bufferedPattern.shuffle;
  pattern[ptrnBuffer].flam = bufferedPattern.flam;
  pattern[ptrnBuffer].totalAcc = bufferedPattern.totalAcc;
  // An untouched bufferedPattern is BSS - all zeros - and a bare MUTE press in
  // TRACK_WRITE pastes it. Without this the paste installs scale 0 and shuffle 0, which
  // stops the sequencer, and patternWasEdited then commits that to the bank and to
  // EEPROM, where it survives a reflash.
  SanitizePattern(&pattern[ptrnBuffer]);

  // [TR-909 STYLE] Paste external tracks with their velocity levels. Unlike extLength
  // no guard is needed: every bit pattern is a legal accent mask, and its bits are read
  // only where the extTrack bit pasted alongside it is set.
  for (byte i = 0; i < 16; i++) {
    pattern[ptrnBuffer].extTrack[i] = bufferedPattern.extTrack[i];
    pattern[ptrnBuffer].extAccent[i] = bufferedPattern.extAccent[i];
  }

  for (byte i = 0; i < NBR_INST; i++) {  //loop as many instrument for a page
    for (byte j = 0; j < NBR_STEP; j++) {
      pattern[ptrnBuffer].velocity[i][j] = bufferedPattern.velocity[i][j];
    }
  }
}

//shift left pattern
void ShiftLeftPattern() {
  patternWasEdited = TRUE;
  if (instBtn) {  //only shift selected instruments
    if (bitRead(pattern[ptrnBuffer].inst[curInst], 0)) {
      pattern[ptrnBuffer].inst[curInst] = pattern[ptrnBuffer].inst[curInst] >> 1 | (1 << 15);
      if (pattern[ptrnBuffer].velocity[curInst][0] > instVelLow[curInst]) pattern[ptrnBuffer].velocity[curInst][15] = instVelHigh[curInst];
      else pattern[ptrnBuffer].velocity[curInst][15] = instVelLow[curInst];
    } else pattern[ptrnBuffer].inst[curInst] = pattern[ptrnBuffer].inst[curInst] >> 1;
    //update instrument velocity
    for (int stp = 0; stp < NBR_STEP - 1; stp++) {
      pattern[ptrnBuffer].velocity[curInst][stp] = pattern[ptrnBuffer].velocity[curInst][stp + 1];
    }
  } else {  // shift entire pattern
    for (int nbrInst = 0; nbrInst < NBR_INST; nbrInst++) {
      if (bitRead(pattern[ptrnBuffer].inst[nbrInst], 0)) {
        pattern[ptrnBuffer].inst[nbrInst] = pattern[ptrnBuffer].inst[nbrInst] >> 1 | (1 << 15);
        if (pattern[ptrnBuffer].velocity[nbrInst][0] > instVelLow[nbrInst]) pattern[ptrnBuffer].velocity[nbrInst][15] = instVelHigh[nbrInst];
        else pattern[ptrnBuffer].velocity[nbrInst][15] = instVelLow[nbrInst];
      } else pattern[ptrnBuffer].inst[nbrInst] = pattern[ptrnBuffer].inst[nbrInst] >> 1;
      //update instrument velocity
      for (int stp = 0; stp < NBR_STEP - 1; stp++) {
        pattern[ptrnBuffer].velocity[nbrInst][stp] = pattern[ptrnBuffer].velocity[nbrInst][stp + 1];
      }
    }
  }
}

//Shift Right pattern
void ShiftRightPattern() {
  patternWasEdited = TRUE;
  if (instBtn) {  //shift only selected instrument
    if (bitRead(pattern[ptrnBuffer].inst[curInst], 15)) {
      pattern[ptrnBuffer].inst[curInst] = pattern[ptrnBuffer].inst[curInst] << 1 | 1;
      if (pattern[ptrnBuffer].velocity[curInst][15] > instVelLow[curInst]) pattern[ptrnBuffer].velocity[curInst][0] = instVelHigh[curInst];
      else pattern[ptrnBuffer].velocity[curInst][0] = instVelLow[curInst];
    } else pattern[ptrnBuffer].inst[curInst] = pattern[ptrnBuffer].inst[curInst] << 1;
    //update instrument velocity
    for (int stp = NBR_STEP - 1; stp > 0; stp--) {
      pattern[ptrnBuffer].velocity[curInst][stp] = pattern[ptrnBuffer].velocity[curInst][stp - 1];
    }
  } else {  //shift entire pattern
    for (int nbrInst = 0; nbrInst < NBR_INST; nbrInst++) {
      if (bitRead(pattern[ptrnBuffer].inst[nbrInst], 15)) {
        pattern[ptrnBuffer].inst[nbrInst] = pattern[ptrnBuffer].inst[nbrInst] << 1 | 1;
        if (pattern[ptrnBuffer].velocity[nbrInst][15] > instVelLow[nbrInst]) pattern[ptrnBuffer].velocity[nbrInst][0] = instVelHigh[nbrInst];
        else pattern[ptrnBuffer].velocity[nbrInst][0] = instVelLow[nbrInst];
      } else pattern[ptrnBuffer].inst[nbrInst] = pattern[ptrnBuffer].inst[nbrInst] << 1;
      //update instrument velocity
      for (int stp = NBR_STEP - 1; stp > 0; stp--) {
        pattern[ptrnBuffer].velocity[nbrInst][stp] = pattern[ptrnBuffer].velocity[nbrInst][stp - 1];
      }
    }
  }
}

//[oort] write every edited pattern of the current bank back to EEprom
//
// Split out of SeqParameter()'s ENTER handler so the SysEx path can reach it:
// a host is about to read or write EEPROM directly, and patternBank[] is the
// authority until this runs.
void FlushPatternBank() {
  for (byte i = 0; i < NBR_PATTERN; i++) {
    if (editedPatterns[i]) {
      memcpy(&pattern[ptrnBuffer], &patternBank[i], sizeof(Pattern));
      SavePattern(curBank * NBR_PATTERN + i);  //save to EE-prom, only implemented for specific buffer
      editedPatterns[i] = FALSE;
    }
  }
  patternBankNeedsSave = FALSE;  //[oort] not 100% certain on this placement

  //[oort] restore buffer (room for improvements here)
  memcpy(&pattern[ptrnBuffer], &patternBank[curPattern - curBank * NBR_PATTERN], sizeof(Pattern));
}

//[oort] the patterns in this bank are loaded into local RAM using tempPattern
void LoadPatternBank(byte bankNmbr) {
  if (bankNmbr > MAX_BANK) bankNmbr = MAX_BANK; //[oort] rudimentary check
  byte firstPtrnInBank = bankNmbr * NBR_PATTERN;

  for (byte i = 0; i < NBR_PATTERN; i++) {
    LoadTempPattern(firstPtrnInBank + i);                    //loaded into tempPattern
    memcpy(&patternBank[i], &tempPattern, sizeof(Pattern));  //[oort] idea from 2021 Neuromancer firmware
    editedPatterns[i] = FALSE;
  }
  patternBankNeedsSave = FALSE; //[oort] previous unsaved patterns in previous bank will be lost
}