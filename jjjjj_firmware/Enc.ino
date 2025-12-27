//-------------------------------------------------
//                  NAVA v1.x
//                 Encoder function
//-------------------------------------------------

////////////////////////Function//////////////////////

void EncGet() {
  //////////////////////////////////TOTAL ACCENT////////////////////////////////////
  if (curInst == TOTAL_ACC && curSeqMode == PTRN_STEP) {
    pattern[ptrnBuffer].totalAcc = EncGet(pattern[ptrnBuffer].totalAcc, 1);
    pattern[ptrnBuffer].totalAcc = constrain(pattern[ptrnBuffer].totalAcc, 0, 13);
    static byte prevTotalAcc;
    if (pattern[ptrnBuffer].totalAcc != prevTotalAcc) {
      prevTotalAcc = pattern[ptrnBuffer].totalAcc;
      patternWasEdited = TRUE;
      needLcdUpdate = TRUE;
    }
  }
  ///////////////////////////////////TRACK WRITE////////////////////////////////////
  else if (curSeqMode == TRACK_WRITE && !tempoBtn.pressed && !seq.configMode) {
    switch (curIndex) {
        //track position
      case 0:  //track position
        if (instBtn) {
          trk.pos = EncGet(trk.pos, 10);
        } else {
          trk.pos = EncGet(trk.pos, 1);
        }
        trk.pos = constrain(trk.pos, 0, 999);
        trk.pos = constrain(trk.pos, 0, track[trkBuffer].length);
        static unsigned int prevTrkPos;
        if (trk.pos != prevTrkPos) {
          prevTrkPos = trk.pos;
          nextPattern = track[trkBuffer].patternNbr[trk.pos];

          if (trk.pos > track[trkBuffer].length - 1 && nextPattern < MAX_PTRN) nextPattern = curPattern;  // Neuromancer: keep same pattern if writing at the end of the track.
          if (curPattern != nextPattern) selectedPatternChanged = TRUE;
          needLcdUpdate = TRUE;
          break;
          case 1:  // track pattern
            if (instBtn) {
              nextPattern = EncGet(nextPattern, 16);
            } else {
              nextPattern = EncGet(nextPattern, 1);
            }
            nextPattern = constrain(nextPattern, 0, MAX_PTRN - 1);
            static unsigned int prevNextPattern;
            if (nextPattern != prevNextPattern) {
              prevNextPattern = nextPattern;
              if (curPattern != nextPattern) selectedPatternChanged = TRUE;
              needLcdUpdate = TRUE;
            }
            break;
          case 2:  //track length
            if (instBtn) {
              track[trkBuffer].length = EncGet(track[trkBuffer].length, 10);
            } else {
              track[trkBuffer].length = EncGet(track[trkBuffer].length, 1);
            }
            track[trkBuffer].length = constrain(track[trkBuffer].length, 0, 999);
            static unsigned int prevTrkLength;
            if (track[trkBuffer].length != prevTrkLength) {
              prevTrkLength = track[trkBuffer].length;
              trackNeedSaved = TRUE;
              needLcdUpdate = TRUE;
            }
            break;
          case 3:  //track number
            //toDo
            break;
        }
    }
  }

  ///////////////////////////////////KEYBOARD MODE////////////////////////////////////
  else if (keyboardMode) {
    switch (curIndex) {
      //track position
      case 0:  //external instrument note index
        if (instBtn) {
          noteIndex = EncGet(noteIndex, 10);
        } else {
          noteIndex = EncGet(noteIndex, 1);
        }
        noteIndex = constrain(noteIndex, 0, 99);
        static unsigned int prevNoteIndex;
        if (noteIndex != prevNoteIndex) {
          prevNoteIndex = noteIndex;
          needLcdUpdate = TRUE;
          break;
          case 1:  // [TR-909] External instrument encoder - not used in TR-909 mode
            // TR-909 uses fixed chromatic notes (C2-D#3), no encoder editing
            // Track and note selection handled in key.ino
            break;
          case 2:  // Unused in TR-909 style EXT INSTRUMENT implementation [SIZZLE FW]
            // In the TR-909, this encoder function does nothing [SIZZLE FW]
            break;
          case 3:  //octave
            keybOct = EncGet(keybOct, 1);
            keybOct = constrain(keybOct, 0, 7);
            static unsigned int prevKeybOct;
            if (keybOct != prevKeybOct) {
              prevKeybOct = keybOct;
              needLcdUpdate = TRUE;
            }
            break;
        }
    }
  }

  ///////////////////////////////////CONFIG MODE////////////////////////////////////
  //[oort] comment: The Config Mode is very cryptic with three letter abbreviations.
  //Tt would benefit from a complete rewrite with one item per page instead.

  else if (seq.configMode) {  //  [zabox] rewrite for two complete pages & no wrong encoder updates

    if (seq.configPage == 1) {

      //---------------------Page 1----------------------------------------------------

      switch (curIndex) {
        //track position
        case 0:

          seq.sync = EncGet(seq.sync, 1);  //sync select
          seq.sync = constrain(seq.sync, 0, 2);
          static byte prevSeqSync;
          if (seq.sync != prevSeqSync) {
            prevSeqSync = seq.sync;
            seq.syncChanged = TRUE;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
        case 1:

          seq.defaultBpm = EncGet(seq.defaultBpm, 1);
          seq.defaultBpm = constrain(seq.defaultBpm, MIN_BPM, MAX_BPM);
          static unsigned int prevDefaultBpm;
          if (seq.defaultBpm != prevDefaultBpm) {
            prevDefaultBpm = seq.defaultBpm;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
        case 2:;                                     //[oort] There's no point setting the TX channel when Drum Notes OUT are off
#if MIDI_DRUMNOTES_OUT                               // Channel 0 is used to indicate "do not transmit drumnotes"
          seq.TXchannel = EncGet(seq.TXchannel, 1);  //[oort] moved inside #if
          seq.TXchannel = constrain(seq.TXchannel, 0, 16);
#else
          //seq.TXchannel = constrain(seq.TXchannel, 1, 16);  //[oort] comment: this serves no pupropse if it's not sending drum notes
#endif
          static unsigned int prevTX;
          if (seq.TXchannel != prevTX) {
            prevTX = seq.TXchannel;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
        case 3:  //[oort] comment: This only applies to expander mode
          seq.RXchannel = EncGet(seq.RXchannel, 1);
          seq.RXchannel = constrain(seq.RXchannel, 1, 16);
          static unsigned int prevRX;
          if (seq.RXchannel != prevRX) {
            prevRX = seq.RXchannel;
            MIDI.setInputChannel(seq.RXchannel);
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
      }
    } else if (seq.configPage == 2) {

      //---------------------Page 2----------------------------------------------------

      switch (curIndex) {
        //track position
        case 0:
          seq.ptrnChangeSync = EncGet(seq.ptrnChangeSync, 1);  //pattern change sync select
          seq.ptrnChangeSync = constrain(seq.ptrnChangeSync, 0, 1);
          static boolean prevPtrnSyncChange;
          if (seq.ptrnChangeSync != prevPtrnSyncChange) {
            prevPtrnSyncChange = seq.ptrnChangeSync;
            // seq.syncChanged = TRUE;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
        case 1:
          seq.muteModeHH = EncGet(seq.muteModeHH, 1);  // [zabox]
          seq.muteModeHH = constrain(seq.muteModeHH, 0, 1);
          static boolean prev_muteModeHH;
          if (seq.muteModeHH != prev_muteModeHH) {
            prev_muteModeHH = seq.muteModeHH;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
        case 2:
#if MIDI_EXT_CHANNEL
          seq.EXTchannel = EncGet(seq.EXTchannel, 1);
          seq.EXTchannel = constrain(seq.EXTchannel, 1, 16);
          static unsigned int prevEXT;
          if (seq.EXTchannel != prevEXT) {
            prevEXT = seq.EXTchannel;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
          break;
#endif
        case 3:
#if CONFIG_BOOTMODE
          seq.BootMode = (SeqMode)EncGet(seq.BootMode, 1);
          seq.BootMode = (SeqMode)constrain((unsigned int)seq.BootMode, 0, 4);
          static SeqMode prevBootMode;
          if (seq.BootMode != prevBootMode) {
            prevBootMode = seq.BootMode;
            seq.setupNeedSaved = TRUE;
            needLcdUpdate = TRUE;
          }
#endif
          break;
      }
    }
#if MIDI_HAS_SYSEX
    else if (seq.configPage == 3) {
      switch (curIndex) {
        case 0:
          {
            sysExDump = EncGet(sysExDump, 1);  //type select
            sysExDump = constrain(sysExDump, 0, 3);
            static byte prevsysExDump;
            if (sysExDump != prevsysExDump) {
              prevsysExDump = sysExDump;
              needLcdUpdate = TRUE;
            }
            break;
          }
        case 1:
          {
            if (sysExDump < SYSEX_MAXPARAM) {
              static byte prevsysExParam;
              sysExParam = EncGet(sysExParam, 1);  //bank select
              if (sysExDump == 0) {
                sysExParam = constrain(sysExParam, 0, MAX_BANK);  // Banks
              } else if (sysExDump == 1) {
                sysExParam = constrain(sysExParam, 0, MAX_PTRN - 1);  // Patterns
              } else if (sysExDump == 2) {
                sysExParam = constrain(sysExParam, 0, MAX_TRACK - 1);  // Tracks
              }

              if (sysExParam != prevsysExParam) {
                prevsysExParam = sysExParam;
                needLcdUpdate = TRUE;
              }
            }
            break;
          }
        case 2:
        case 3:
          {
            break;
          }
      }
    }
#endif
  } else {
    seq.bpm = EncGet(seq.bpm, 1);
    if (seq.bpm <= MIN_BPM) seq.bpm = MIN_BPM;
    if (seq.bpm >= MAX_BPM) seq.bpm = MAX_BPM;
    static unsigned int curBpm;
    if (seq.bpm != curBpm) {
      curBpm = seq.bpm;
      TimerSetFrequency();
      if (curSeqMode != PTRN_STEP || tempoBtn.pressed) needLcdUpdate = TRUE;
    }
  }
}

//Get encoder value--------------------------------------------------------
//[oort] not an ideal solution but less jumpy than previous version
int EncGet(int value, int dif) {
  static int encCount;
  static int prevCount;
  encoder_A = PINB & B1;  // Read encoder pins
  encoder_B = PINB & B10;
  if ((!encoder_A) && (encoder_A_prev)) {  // A has gone from high to low
    if (encoder_B) encCount++;             // B is high so clockwise
    else encCount--;
  }
  if (instBtn) dif *= 10;
  if (encCount > prevCount + 1) {  //[oort] two clicks needed before change, less jumpy, but not elegant
    prevCount = encCount;
    value += dif;
  } else if (encCount < prevCount - 1) {
    prevCount = encCount;
    value -= dif;
    value = (value < 0) ? 0 : value;
  }
  encoder_A_prev = encoder_A;  // Store value of A for next time
  return value;
}