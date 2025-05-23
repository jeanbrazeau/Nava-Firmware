//-------------------------------------------------
//                  NAVA v1.x
//              Sequencer Led
//-------------------------------------------------

////////////////////////Function//////////////////////

void SetLeds()
{
  //------------------------------Menu LEDS-------------------------------
  shiftLed = shiftBtn;
  instLed = instBtn;
  clearLed = clearBtn.pressed;
  shufLed = shufBtn.pressed;
  scaleLed = scaleBtn.pressed;
  lastStepLed = lastStepBtn.pressed;
  if (isRunning){
    menuLed = LED_PLAY & ~LED_STOP | shiftLed<<1 | instLed<<2 | clearLed<<3 | shufLed<<4 | lastStepLed<<5 | scaleLed<<6 ;
    //stepLeds = pattern[ptrnBuffer].inst[curInst] ^ (blinkFast << curStep-1);
  }
  else{
    menuLed = (LED_PLAY*blinkTempo) | LED_STOP | shiftLed<<1 | instLed<<2 | clearLed<<3 | shufLed<<4 | lastStepLed<<5 | scaleLed<<6;
    //stepLeds = pattern[ptrnBuffer].inst[curInst] ^ (blinkTempo << curStep);
  }

  //------------------------------Config LEDS-------------------------------
  trackLed =((curSeqMode == TRACK_WRITE)? blinkTempo:((curSeqMode == TRACK_PLAY)? 1:0));
  ptrnLed =((curSeqMode == PTRN_STEP)? blinkTempo:((curSeqMode == PTRN_PLAY)? 1:0));
  tapLed =((curSeqMode == PTRN_TAP)? blinkTempo:((tapBtn.pressed) ? 1:0));
  if (keyboardMode){
    backLed = HIGH;
    fwdLed = HIGH;
    numLed = HIGH; //[oort]
  }
  else{
    backLed = backBtn.pressed;
    fwdLed = fwdBtn.pressed;
    numLed = LOW; //[oort]
  }

  dirLed = dirBtn.pressed;
  scaleLeds = 1 << scaleBtn.counter;
  guideLed =  guideBtn.counter;
  if (curSeqMode == PTRN_STEP && curInst == EXT_INST) guideLed = blinkTempo;
  bankLed = bankBtn.pressed;

  if (instBtn && curInst == TOTAL_ACC) enterLed = HIGH;
//  else if (groupNeedsSave || patternNeedsSave || trackNeedSaved || seq.setupNeedSaved) enterLed = blinkTempo;
  else if (patternBankNeedsSave || trackNeedSaved || seq.setupNeedSaved) enterLed = blinkTempo;
  else enterLed = LOW;

  if (curSeqMode == MUTE) muteLed = HIGH;
  else {
    if (muteInst) {             // Show there are muted instruments when not in MUTE mode
      muteLed = LOW;
      if(flagMuteIntensity >= 8){
        muteLed = HIGH;
        flagMuteIntensity = 0;
      } else {
        flagMuteIntensity++;
      }
    } else { 
      muteLed = LOW;
    }
  }
  
  if (seq.configMode) tempoLed = blinkTempo;
  else  tempoLed = tempoBtn.pressed;

  configLed = numLed | scaleLeds<<1 | trackLed<<5 | backLed<<6 | fwdLed<<7 | enterLed<<8 | ptrnLed<<9 | tapLed<<10 | dirLed<<11 |guideLed<<12 | bankLed<<13 | muteLed<<14 | tempoLed<<15;

  //------------------------------Step LEDS-------------------------------
  
  //Update inst selected Leds
  switch (curInst){
  case BD:
    instSlctLed = 0x03;
    break;
  case SD:
    instSlctLed = 0x0C;
    break;
  case LT:
    instSlctLed = 0x30;
    break;
  case MT:
    instSlctLed = 0xC0;
    break;
  case HT:
    instSlctLed = 0x300;
    break;
  case RM:
    instSlctLed = 0x400;
    break;
  case HC:
    instSlctLed = 0x800;
    break;
  case CH:
    instSlctLed = 0x3000;
    break;
  case RIDE:
    instSlctLed = 0x8000;
    break;
  case CRASH:
    instSlctLed = 0x4000;
    break;
  case OH:
    instSlctLed = 0x3000;
    break;
  case TRIG_OUT:
  case EXT_INST:
  case TOTAL_ACC:
    instSlctLed = 0;//Clear step leds when TOTAL_ACC... selected
    break;
  }

  //Update step Leds depends sequencer Mode
  switch (curSeqMode){
    //------------------------------
    unsigned int temp;// tmep data of stepLeds;
  case TRACK_PLAY:
    if (isRunning) stepLeds = (1 << trk.current) ^ (blinkFast << curStep); 
    else stepLeds = blinkTempo << trk.current;
    break;
  case TRACK_WRITE:
    if (bankBtn.pressed){
      stepLeds = 1 << curBank;
    }
    else if (numBtn.pressed){
      stepLeds = 1 << trk.current;
    }
    else{
      if (isRunning) stepLeds = (1 <<(curPattern % NBR_PATTERN)) ^ (blinkFast<< curStep); 
      else stepLeds = blinkTempo << (curPattern % NBR_PATTERN); 
    }
    break;
    //------------------------------
  case PTRN_PLAY:
    if (bankBtn.pressed){
      stepLeds = 1 << curBank;
    }
    else{
      if (isRunning){
        if (group.length){
          temp = 0;
          for (byte a = 0; a <= group.length; a++){
            bitSet(temp,(group.firstPattern % NBR_PATTERN) + a);
          }
          stepLeds = temp & ~(!blinkTempo << (curPattern % NBR_PATTERN)) ^ (blinkFast<< curStep); //[oort] led show current pattern in group
        }
        else {
          stepLeds = (blinkTempo <<(nextPattern % NBR_PATTERN)) ^ (blinkFast<< curStep); //[oort] nextPattern becomes curPattern when there's only one seleted
        }
      } 
      else {
        // Not Running
        temp = 0;

        //[oort] section taken from 1.028[oort], groups disregarded in Neuro
        if (group.length) {
            temp = 0;
            for (byte a = 0; a <= group.length; a++) {  //[oort] byte instead of int
              bitSet(temp, (group.firstPattern % NBR_PATTERN) + a);
            }
            stepLeds = temp & ~(!blinkTempo << (nextPattern % NBR_PATTERN));
          } else {
            stepLeds = blinkTempo << (curPattern % NBR_PATTERN);  //[oort] curPattern in Neuro, next in 1.028[oort]
          }
      }
      
      /* ioriginal Neuro section
      else {
        // Not Running
        temp = 0;
        for (byte a = 0; a <= group.length; a++){
          bitSet(temp,(group.firstPattern % NBR_PATTERN) + a);
        }
        stepLeds = temp & ~(!blinkTempo << (curPattern % NBR_PATTERN));
      } */
    }
    break;
    //------------------------------
  case PTRN_STEP:
  case PTRN_TAP:
    //display selected inst on steps Leds
    if (instBtn){
      if(flagLedIntensity >= 8){
        stepLeds = instSlctLed ;
        flagLedIntensity = 0;
      }
      else{
        if (curInst == OH) stepLeds = instSlctLed & LED_MASK_OH;
        else if (curFlam) stepLeds = instSlctLed;                                                                  // [zabox] [1.027] flam
        else stepLeds = instSlctLed & LED_MASK;
        flagLedIntensity++;
      }
    }
    else if (shufBtn.pressed) {                                                                                    // [zabox] [1.028] flam
      if (flagLedIntensity >= 8) {
        stepLeds = ~(1 << 7);
        flagLedIntensity = 0;
      }
      else {
        stepLeds = (1 << (pattern[ptrnBuffer].shuffle - 1)) | (1 << (pattern[ptrnBuffer].flam + 8));
        flagLedIntensity++;
      }
      
    }
    else if (isRunning && !instBtn){ 
      stepLedsHigh = stepLedsLow = 0;//initialize step Leds variable 
//      for (int stp = 0; stp < NBR_STEP; stp++){
      for (int stp = 0; stp < (pattern[ptrnBuffer].length + 1); stp++){
        if (curFlam) {                                                                       // [zabox] [1.027] flam
          if (pattern[ptrnBuffer].velocity[curInst][stp] & 128) {
            if (((pattern[ptrnBuffer].velocity[curInst][stp]) & 127) > instVelLow[curInst] && bitRead(pattern[ptrnBuffer].inst[curInst],stp)) bitSet(stepLedsHigh, stp);
            else if (((pattern[ptrnBuffer].velocity[curInst][stp]) & 127) <= instVelLow[curInst] && bitRead(pattern[ptrnBuffer].inst[curInst],stp)) bitSet(stepLedsLow,stp);
            }
          else { 
            bitClear(stepLedsHigh, stp);   
            bitClear(stepLedsLow, stp); 
          }
        }
        else {
          if (!(pattern[ptrnBuffer].velocity[curInst][stp] & 128)) {
            if (pattern[ptrnBuffer].velocity[curInst][stp] > instVelLow[curInst] && bitRead(pattern[ptrnBuffer].inst[curInst],stp)) bitSet(stepLedsHigh, stp);
            else if (pattern[ptrnBuffer].velocity[curInst][stp] <= instVelLow[curInst] && bitRead(pattern[ptrnBuffer].inst[curInst],stp)) bitSet(stepLedsLow,stp);
          }
          else { 
            bitClear(stepLedsHigh, stp);   
            bitClear(stepLedsLow, stp); 
          }
        }
      }
      //this function is to fade low velocity leds
      if(flagLedIntensity >= 3){    //[oort] Sandor uses 8 instead of 3
        stepLeds = stepLedsHigh | stepLedsLow ^ blinkFast << curStep;//B1111111111111111;
        flagLedIntensity = 0;
      }
      else{
        stepLeds = stepLedsHigh ^ blinkFast << curStep;//B1010101010101010;
        flagLedIntensity++;
      }
    }
    else if (!isRunning && !instBtn){
      //Display Bank number
      if (bankBtn.pressed){
        stepLeds = 1 << curBank;
      }
      //display selected pattern
      else{

        //[oort] section taken from 1.028[oort], groups disregarded in Neuro
        if (group.length) {
            temp = 0;
            for (int a = 0; a <= group.length; a++) {
              bitSet(temp, (group.firstPattern % NBR_PATTERN) + a);
            }
            stepLeds = temp & ~(!blinkTempo << (curPattern % NBR_PATTERN));
          } else {
            stepLeds = blinkTempo << (curPattern % NBR_PATTERN);
          }
       
      /* [oort] original Neuro 
        temp = 0;
        for (byte a = 0; a <= group.length; a++){
          bitSet(temp,(group.firstPattern % NBR_PATTERN) + a);
        }
        stepLeds = temp & ~(!blinkTempo << (curPattern % NBR_PATTERN));
      */
      
      }
    }
    break;
    //------------------------------
  case MUTE:
    stepLeds =  muteLeds;
    if (encBtn.pressed) muteLeds = 0;
    break;
  }

  if ( seq.configMode )
  {
    if (flagLedIntensity >= 8) {
      stepLeds = ~(1 << MAX_CONF_PAGE) & 0xF;
      flagLedIntensity = 0;
    }
    else {
      stepLeds = (1 << (seq.configPage -1 ));
      flagLedIntensity++;
    }
  }
  
  //Send OUTPUTS now !
  SetDoutLed(stepLeds, configLed , menuLed);
}
