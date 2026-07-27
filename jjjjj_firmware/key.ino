//-------------------------------------------------
//                  NAVA v1.x
//          external instrument track editor
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
  // The preview and the sequencer have no shared ownership of a note: both send on
  // seq.EXTchannel from EXT_TRACK_NOTES, so if the running pattern uses this track
  // its per-step SendExtTrackNoteOff() would cut the preview and the preview's
  // note-off would truncate a sequenced note. Decline the preview instead - the
  // sequencer is already sounding the pitch the user wanted to hear. Declining still
  // has to close any preview already open, or the previous track's note would keep
  // sounding under the newly selected track's label.
  if (isRunning && pattern[ptrnBuffer].extTrack[currentExtTrack]) {
    ExtPreviewOff();
    return;
  }

  if (previewActive) ExtPreviewOff();
#if MIDI_EXT_CHANNEL
  MidiSendNoteOn(seq.EXTchannel, note, HIGH_VEL);
#else
  MidiSendNoteOn(seq.TXchannel, note, HIGH_VEL);
#endif
  previewNote = note;
  previewActive = TRUE;
  if (holdMs) {
    previewOffAt = millis() + holdMs;
    // 0 is the sustain sentinel, so a deadline landing exactly on the millis() wrap
    // would turn a timed audition into a note that never stops
    if (!previewOffAt) previewOffAt = 1;
  }
  else {
    previewOffAt = 0;  // sustain until the button is released
  }
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
  // Signed elapsed comparison so the deadline survives the millis() wrap
  if (previewActive && previewOffAt && (long)(millis() - previewOffAt) >= 0) ExtPreviewOff();
}

void ExtInstUpdate()
{
  // Edge source for ext step programming. stepBtn[].justPressed cannot serve here:
  // its only live setter is InstValueGet, which SeqParameter no longer calls while
  // this mode owns the step buttons, and ButtonGet clears the flags every scan.
  // Sampled on every pass rather than inside the edit block below, so a button held
  // across a mode change cannot present a stale edge on the way back in.
  static unsigned int prevExtStepState;

  unsigned int currentButtonState = StepButtonGet(MOMENTARY);

  // Unconditional: a timed preview started just before leaving edit mode still has
  // to be retired, and the edit block below no longer runs to do it.
  ExtPreviewCheck();

  // A sustained preview is released by the step-button release inside the edit block,
  // which stops running the moment the mode changes. MUTE assigns curSeqMode directly
  // rather than going through ExitExtInstEditMode(), so it would strand the note with
  // no way to clear it: InitMidiNoteOff() only walks extTrackNoteOn[] and
  // SendAllNoteOff() targets seq.TXchannel. Retire it when its context is gone.
  if (previewActive && !previewOffAt &&
      (!extInstEditMode || curSeqMode != PTRN_STEP || !stepsBtn.pressed)) ExtPreviewOff();

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
    // [TR-909 STYLE] INST + step button (1-16) = select track
    if (instBtn && currentButtonState) {
      extInstButtonHandled = TRUE;

      for (byte i = 0; i < NBR_STEP_BTN; i++) {
        if (bitRead(currentButtonState, i) && !bitRead(prevExtStepState, i)) {
          currentExtTrack = i;
          currentExtNote = pgm_read_byte(&EXT_TRACK_NOTES[i]);

          // Preview note, sustained until the step button is released
          ExtPreviewOn(currentExtNote, 0);
          needLcdUpdate = TRUE;
          stepBtn[i].justPressed = FALSE;
        }
        // Kept accurate for InstValueGet, which owns this array again once the mode
        // exits; the edge test above no longer reads it.
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
        if (bitRead(currentButtonState, step) && !bitRead(prevExtStepState, step)) {
          // Toggle step for current track
          if (bitRead(pattern[ptrnBuffer].extTrack[currentExtTrack], step)) {
            bitClear(pattern[ptrnBuffer].extTrack[currentExtTrack], step);
          } else {
            // Audition before the bitSet, not after: ExtPreviewOn() declines when the
            // running pattern already uses this track, and setting the bit first would
            // make that true of every step the user adds.
            ExtPreviewOn(currentExtNote, 50);
            bitSet(pattern[ptrnBuffer].extTrack[currentExtTrack], step);
          }
          patternWasEdited = TRUE;
          needLcdUpdate = TRUE;
        }
      }
    }
  }

  // Once per pass, after both branches: a press consumed by track select is already
  // recorded here, so releasing INST while still holding the step cannot re-fire it
  // as a programming edge.
  prevExtStepState = currentButtonState;
}
