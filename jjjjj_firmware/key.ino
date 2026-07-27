//-------------------------------------------------
//                  NAVA v1.x
//                 keyboard mode
//-------------------------------------------------

/////////////////////Function//////////////////////
// [SIZZLE] currentExtNote is defined in define.h now

// Leaving edit mode has to put curInst back on a real voice: EXT_INST triggers no
// drum circuit, so a stranded selection looks like dead step buttons.
void ExitExtInstEditMode()
{
  if (!extInstEditMode) return;
  extInstEditMode = FALSE;
  extInstButtonHandled = FALSE;
  ExtPreviewOff();
  curInst = BD;
  needLcdUpdate = TRUE;
}

// Preview notes are started from the main loop, so they can never be closed with a
// delay(): ExtPreviewCheck() retires them instead. Only one preview may sound at a
// time, otherwise a second track select strands the first note on the synth.
void ExtPreviewOn(byte note, unsigned long holdMs)
{
  if (previewActive) ExtPreviewOff();
#if MIDI_EXT_CHANNEL
  MidiSendNoteOn(seq.EXTchannel, note, HIGH_VEL);
#else
  MidiSendNoteOn(seq.TXchannel, note, HIGH_VEL);
#endif
  previewNote = note;
  previewActive = TRUE;
  previewOffAt = holdMs ? (millis() + holdMs) : 0;  // 0 = sustain until the button is released
}

void ExtPreviewOff()
{
  if (!previewActive) return;
#if MIDI_EXT_CHANNEL
  MidiSendNoteOff(seq.EXTchannel, previewNote);
#else
  MidiSendNoteOff(seq.TXchannel, previewNote);
#endif
  previewActive = FALSE;
  previewOffAt = 0;
}

void ExtPreviewCheck()
{
  if (previewActive && previewOffAt && millis() >= previewOffAt) ExtPreviewOff();
}

void KeyboardUpdate()
{
  // Unconditional: a timed preview started just before leaving edit mode still has
  // to be retired, and the edit block below no longer runs to do it.
  ExtPreviewCheck();

  // [SIZZLE] Exit EXT INST edit mode only when INSTRUMENT SELECT + GUIDE is pressed again
  if (extInstEditMode && instBtn && guideBtn.justPressed) {
    ExitExtInstEditMode();

    // When exiting EXT INST mode, make sure changes are marked as edited
    patternWasEdited = TRUE;
  }

  // While in EXT INST edit mode, ensure we stay on EXT_INST
  if (extInstEditMode && curSeqMode == PTRN_STEP && curInst != EXT_INST) {
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

          // Preview note, sustained until the step button is released
          ExtPreviewOn(currentExtNote, 0);
          needLcdUpdate = TRUE;
          stepBtn[i].justPressed = FALSE;
        }
        stepBtn[i].prevState = bitRead(currentButtonState, i);
      }
    }

    // Release the preview on step release whatever INST does: releasing the step
    // button first used to leave the note hanging on the external synth.
    if (stepsBtn.justRelease) ExtPreviewOff();

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

            // Short audition, retired by ExtPreviewCheck() rather than a delay()
            ExtPreviewOn(currentExtNote, 50);
          }
          patternWasEdited = TRUE;
          needLcdUpdate = TRUE;
        }
      }
    }
  }
}
