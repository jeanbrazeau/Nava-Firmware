//-------------------------------------------------
//                  NAVA v1.x
//                 keyboard mode
//-------------------------------------------------

/////////////////////Function//////////////////////
void KeyboardUpdate()
{
  if (isRunning && keyboardMode){
    keyboardMode = 0;
    needLcdUpdate = TRUE;
  }

  if (numBtn.justPressed && curInst == EXT_INST && curSeqMode == PTRN_STEP){
    keyboardMode = !keyboardMode;
    //stop sequencer when keyboard mode ON [SIZZLE FW]
    if (keyboardMode){
      InitMidiNoteOff();
      isStop = TRUE;
      isRunning = FALSE;
   //   MIDI.sendRealTime(Stop);//;MidiSend(STOP_CMD); [SIZZLE FW]
      stopBtn.counter = 1;//like a push on stop button [SIZZLE FW]
    }
    needLcdUpdate = TRUE;
  }

  /////////////////////////////KeyboardMode////////////////////////////// [SIZZLE FW]
  if(keyboardMode)
  {
    if (scaleBtn.justPressed){
      keybOct++;
      if (keybOct >= MAX_OCT) keybOct = MAX_OCT - 1;
      needLcdUpdate = TRUE;
    }
    if (lastStepBtn.justPressed){
      keybOct--;
      if (keybOct < 0 || keybOct > MAX_OCT - 1) keybOct = 0;
      needLcdUpdate = TRUE;
    }

    // Navigate step positions using back/fwd buttons [SIZZLE FW]
    if (backBtn.justPressed){
      noteIndex--;
      if( noteIndex < 0 || noteIndex > NBR_STEP - 1) noteIndex = 0;
      MidiSendNoteOn(seq.TXchannel, pattern[ptrnBuffer].extNote[noteIndex], HIGH_VEL);
      needLcdUpdate = TRUE;
    }
    if (backBtn.justRelease)MidiSendNoteOff(seq.TXchannel, pattern[ptrnBuffer].extNote[noteIndex]);

    if (fwdBtn.justPressed){
      noteIndex++;
      if( noteIndex >= NBR_STEP ) noteIndex = NBR_STEP - 1;
      MidiSendNoteOn(seq.TXchannel, pattern[ptrnBuffer].extNote[noteIndex], HIGH_VEL);
      needLcdUpdate = TRUE;
    }
    if (fwdBtn.justRelease)MidiSendNoteOff(seq.TXchannel, pattern[ptrnBuffer].extNote[noteIndex]);

    if(clearBtn.justPressed){
      // Reset step counter if SHIFT is not pressed [SIZZLE FW]
      if (!shiftBtn) {
        noteIndex = 0;
      } else {
        // Toggle step on/off when SHIFT+CLEAR is pressed [SIZZLE FW]
        if (bitRead(pattern[ptrnBuffer].inst[EXT_INST], noteIndex)) {
          bitClear(pattern[ptrnBuffer].inst[EXT_INST], noteIndex);
        } else {
          bitSet(pattern[ptrnBuffer].inst[EXT_INST], noteIndex);
        }
        patternWasEdited = TRUE;
      }
      needLcdUpdate = TRUE;
    }

    if (isStop){
      // When holding SHIFT and pressing a sequencer step button, toggle that step on/off [SIZZLE FW]
      if (shiftBtn && readButtonState) {
        unsigned int stepPressed = FirstBitOn();
        if (stepPressed < NBR_STEP) {
          // Toggle the selected step on/off [SIZZLE FW]
          if (bitRead(pattern[ptrnBuffer].inst[EXT_INST], stepPressed)) {
            bitClear(pattern[ptrnBuffer].inst[EXT_INST], stepPressed);
          } else {
            bitSet(pattern[ptrnBuffer].inst[EXT_INST], stepPressed);
          }
          patternWasEdited = TRUE;
          needLcdUpdate = TRUE;
        }
      }
      // When not holding SHIFT, step buttons are used to set note values [SIZZLE FW]
      else {
        for (byte a = 0; a < NBR_STEP_BTN; a++){
          stepBtn[a].curState = bitRead(readButtonState,a);
          if (stepBtn[a].curState != stepBtn[a].prevState){
            if ((stepBtn[a].pressed == LOW) && (stepBtn[a].curState == HIGH)){
              // Set the note for the current step position being edited [SIZZLE FW]
              pattern[ptrnBuffer].extNote[noteIndex] = a + (12* keybOct);
  #if MIDI_EXT_CHANNEL
              MidiSendNoteOn(seq.EXTchannel, a + 12*keybOct, HIGH_VEL);
  #else
              MidiSendNoteOn(seq.TXchannel, a + 12*keybOct, HIGH_VEL);
  #endif
              patternWasEdited = TRUE;
              needLcdUpdate = TRUE;
            }
            if ((stepBtn[a].pressed == HIGH) && (stepBtn[a].curState == LOW)){
  #if MIDI_EXT_CHANNEL
              MidiSendNoteOff(seq.EXTchannel, a + 12*keybOct);
  #else
              MidiSendNoteOff(seq.TXchannel, a + 12*keybOct);
  #endif
            }
            stepBtn[a].pressed = stepBtn[a].curState;
          }
          stepBtn[a].prevState = stepBtn[a].curState;
        }
      }
    }
  }
}
