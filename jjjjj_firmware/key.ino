//-------------------------------------------------
//                  NAVA v1.x
//                 keyboard mode
//-------------------------------------------------

/////////////////////Function//////////////////////
// [SIZZLE] currentExtNote is defined in define.h now

void KeyboardUpdate()
{
  // [SIZZLE] Exit EXT INST edit mode only when INSTRUMENT SELECT + GUIDE is pressed again
  if (extInstEditMode && instBtn && guideBtn.justPressed) {
    extInstEditMode = FALSE;
    needLcdUpdate = TRUE;

    // When exiting EXT INST mode, make sure changes are marked as edited
    patternWasEdited = TRUE;
  }

  // While in EXT INST edit mode, ensure we stay on EXT_INST
  if (extInstEditMode && curInst != EXT_INST) {
    curInst = EXT_INST;
  }

  /////////////////////////////EXT INST Edit Mode (TR-909 STYLE)//////////////////////////////
  if (extInstEditMode && curSeqMode == PTRN_STEP)
  {
    // Get the current button state
    unsigned int currentButtonState = StepButtonGet(MOMENTARY);

    // [TR-909 STYLE] INST + step button (1-16) = select track
    if (instBtn && currentButtonState) {
      extInstButtonHandled = TRUE;

      for (byte i = 0; i < NBR_STEP_BTN; i++) {
        if (bitRead(currentButtonState, i) && !stepBtn[i].prevState) {
          currentExtTrack = i;
          currentExtNote = pgm_read_byte(&EXT_TRACK_NOTES[i]);

          // Preview note
#if MIDI_EXT_CHANNEL
          MidiSendNoteOn(seq.EXTchannel, currentExtNote, HIGH_VEL);
#else
          MidiSendNoteOn(seq.TXchannel, currentExtNote, HIGH_VEL);
#endif
          needLcdUpdate = TRUE;
          stepBtn[i].justPressed = FALSE;
        }
        stepBtn[i].prevState = bitRead(currentButtonState, i);
      }
    }

    // Release preview note when instrument button is released
    if (!instBtn && stepsBtn.justRelease) {
#if MIDI_EXT_CHANNEL
      MidiSendNoteOff(seq.EXTchannel, currentExtNote);
#else
      MidiSendNoteOff(seq.TXchannel, currentExtNote);
#endif
    }

    if (!instBtn) extInstButtonHandled = FALSE;

    // [TR-909 STYLE] Program steps when INST NOT held
    if (!instBtn && !extInstButtonHandled && currentButtonState) {
      for (byte step = 0; step < NBR_STEP; step++) {
        if (stepBtn[step].justPressed) {
          // Toggle step for current track
          if (bitRead(pattern[ptrnBuffer].extTrack[currentExtTrack], step)) {
            bitClear(pattern[ptrnBuffer].extTrack[currentExtTrack], step);
          } else {
            bitSet(pattern[ptrnBuffer].extTrack[currentExtTrack], step);

            // Preview note
#if MIDI_EXT_CHANNEL
            MidiSendNoteOn(seq.EXTchannel, currentExtNote, HIGH_VEL);
            delay(50);
            MidiSendNoteOff(seq.EXTchannel, currentExtNote);
#else
            MidiSendNoteOn(seq.TXchannel, currentExtNote, HIGH_VEL);
            delay(50);
            MidiSendNoteOff(seq.TXchannel, currentExtNote);
#endif
          }
          patternWasEdited = TRUE;
          needLcdUpdate = TRUE;
        }
      }
    }
  }
}
