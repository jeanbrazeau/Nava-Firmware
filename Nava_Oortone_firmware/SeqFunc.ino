//-------------------------------------------------
//                  NAVA v1.x
//                 SEQ Functions
//-------------------------------------------------

/////////////////////Function//////////////////////
//Initialise the sequencer before to run
void InitSeq() {
  LoadSeqSetup();
  ppqn = 0;
  stepCount = 0;
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
    bufferToSet->velocity[EXT_INST][stp] = HIGH_VEL;                                                   //EXT_INST [SIZZLE FW]

    // Initialize EXT_INST steps with ascending notes [SIZZLE FW]
    // This matches TR-909 behavior where each step plays a different note [SIZZLE FW]
    if (bufferToSet->extNote[stp] == 0) {
      bufferToSet->extNote[stp] = 36 + stp; // Start at C3 and ascend chromatically [SIZZLE FW]
    }
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

//copy pattern to buffer //[oort] note: patternNum is never used
void CopyPatternToBuffer(byte patternNum) {
  for (byte i = 0; i < NBR_INST; i++) {
    bufferedPattern.inst[i] = pattern[ptrnBuffer].inst[i];
  }
  bufferedPattern.length = pattern[ptrnBuffer].length;
  bufferedPattern.scale = pattern[ptrnBuffer].scale;
  bufferedPattern.shuffle = pattern[ptrnBuffer].shuffle;
  bufferedPattern.flam = pattern[ptrnBuffer].flam;
  bufferedPattern.extLength = pattern[ptrnBuffer].extLength;
  bufferedPattern.totalAcc = pattern[ptrnBuffer].totalAcc;

  for (byte j = 0; j < pattern[ptrnBuffer].extLength; j++) {
    bufferedPattern.extNote[j] = pattern[ptrnBuffer].extNote[j];
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
  pattern[ptrnBuffer].scale = bufferedPattern.scale;
  pattern[ptrnBuffer].shuffle = bufferedPattern.shuffle;
  pattern[ptrnBuffer].flam = bufferedPattern.flam;
  pattern[ptrnBuffer].extLength = bufferedPattern.extLength;
  pattern[ptrnBuffer].totalAcc = bufferedPattern.totalAcc;

  for (byte j = 0; j < bufferedPattern.extLength; j++) {
    pattern[ptrnBuffer].extNote[j] = bufferedPattern.extNote[j];
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