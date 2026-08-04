//-------------------------------------------------
//                  NAVA v1.x
//                  LCD update
//-------------------------------------------------

#include "nava_strings.h"

/////////////////////Function//////////////////////
// Print a MIDI note number as a name, returning the characters written so the caller
// can pad a fixed-width field.
//
// MIDI 60 is C4 here - the convention nearly every DAW and modern synth displays - so
// the ext track defaults of 48..63 read C3..D#4. The octave therefore runs -1..9, and
// "C#-1" is the widest result at four characters, which is exactly the width of the
// value field. Sharps only: choosing the flat spelling of the same pitch would need a
// key signature the sequencer does not have.
byte LcdPrintNoteName(byte note)
{
  byte idx = (note % 12) * 2;
  char letter = pgm_read_byte(&noteNames[idx]);
  char accidental = pgm_read_byte(&noteNames[idx + 1]);

  lcd.print(letter);
  byte width = 1;
  if (accidental != ' ') {
    lcd.print(accidental);
    width++;
  }

  int octave = (int)(note / 12) - 1;
  lcd.print(octave);
  width += (octave < 0) ? 2 : 1;   // "-1" is two characters, 0..9 one
  return width;
}

//Initialise IO PORT and libraries
void LcdUpdate()
{
  static byte previousMode;

  // [TR-909 STYLE] Non-blocking EXT INST splash, painted once on entry. The falling
  // edge has to request a redraw or the display stays stuck on the message.
  // The deadline is tested by signed elapsed time and disarmed once it passes: an
  // absolute millis() < deadline test reads a stale deadline as still in the future
  // across the 49.7 day wrap and would freeze the display for up to 800 seconds.
  // Keyed on the deadline rather than a painted flag: toggling the mode twice inside
  // the splash window re-arms with a new deadline, and a flag would suppress the
  // repaint and leave the panel showing the previous toggle's text.
  static unsigned long paintedDeadline = 0;
  if (extInstSplashArmed) {
    if ((long)(millis() - extInstSplashUntil) < 0) {
      if (paintedDeadline != extInstSplashUntil) {
        paintedDeadline = extInstSplashUntil;
        lcd.clear();
        lcd.setCursor(0,0);
        if (extInstSplashKind == EXT_SPLASH_DIR) {
          // F() throughout: a bare literal handed to Print is copied into RAM at startup,
          // and six of them cost 57 bytes on a build already at 82%. Flash is at a third.
          lcd.print(F("EXT TRCK DIR"));
          lcd.setCursor(0,1);
          switch (extDir) {
            case EXT_DIR_FOLLOW: lcd.print(F("FOLLOW SEQ")); break;
            case FORWARD:        lcd.print(F("FORWARD"));    break;
            case BACKWARD:       lcd.print(F("BACKWARD"));   break;
            case PING_PONG:      lcd.print(F("PING PONG"));  break;
            case RANDOM:         lcd.print(F("RANDOM"));     break;
          }
        }
        else if (extInstEditMode) {
          lcd.print("EXT TRCK EDIT ON");
          lcd.setCursor(0,1);
          // MIDI note number rather than a note name: the pitch depends on the
          // reader's octave convention, the number does not
          lcd.print("TRK:");
          lcd.print(currentExtTrack + 1);
          lcd.print(" NOTE:");
          LcdPrintNoteName(extTrackNote[currentExtTrack]);
        }
        else {
          lcd.print("EXT TRCK EDIT");
          lcd.setCursor(0,1);
          lcd.print("MODE OFF");
        }
      }
      return;
    }
    extInstSplashArmed = FALSE;
    needLcdUpdate = TRUE;
  }

  //display tempo
  if (tempoBtn.pressed) {                                                         // [1.028] 
    if(curSeqMode != PTRN_PLAY && !shiftBtn && !seq.configMode) {                 // + !seq.configMode
        lcd.setCursor(11,1);
        LcdPrintTempo();
    }
  }
  //display total  accent
  /*if (enterBtn.pressed && curSeqMode == PTRN_PLAY){
   LcdPrintTotalAcc();
   }*/
  if(needLcdUpdate){
    
    needLcdUpdate = FALSE;
    if (seq.configMode)
    {
      lcd.setCursor(0,0);
      switch (seq.configPage)
      {
        case CONF_PAGE_SYNC:// first page
        {
#if MIDI_DRUMNOTES_OUT
          lcd.print("syn bpm mTX mRX ");
#else
          lcd.print("syn bpm xxx mRX ");  //[oort] no drum notes out
#endif
          lcd.setCursor(cursorPos[curIndex],0);
          lcd.print(letterUpConfPage1[curIndex]);
          lcd.setCursor(0,1);
          LcdClearLine();
          lcd.setCursor(0,1);
          char  sync[2];
          strcpy_P(sync, (char*)pgm_read_word(&(nameSync[seq.sync])));
          lcd.print(sync);
          lcd.setCursor(4,1);
          lcd.print(seq.defaultBpm);
          lcd.setCursor(9,1);
#if MIDI_DRUMNOTES_OUT
          if ( seq.TXchannel > 0 )
          {
            lcd.print(seq.TXchannel);
          } else {
            lcd.setCursor(8,1);
            lcd.print("off");
          }
#else          
          //lcd.print(seq.TXchannel); [oort] comment: makes no sense if MIDI_DRUM_NOTES are deactivated
#endif          
          lcd.setCursor(13,1);
          lcd.print(seq.RXchannel);
          break;
        }
        case CONF_PAGE_MISC:// second page
        {
          lcd.print("pCh mte ");
#if MIDI_EXT_CHANNEL      
          lcd.print("eXT ");
#else
          lcd.print("nc. ");
#endif
#if CONFIG_BOOTMODE
          lcd.print("mod ");
#else                        
          lcd.print("nc. ");                             // [zabox]
#endif
          lcd.setCursor(cursorPos[curIndex],0);
          lcd.print(letterUpConfPage2[curIndex]);
          lcd.setCursor(0,1);
          LcdClearLine();
          lcd.setCursor(0,1);
          char  ptrnSyncChange[2];
          strcpy_P(ptrnSyncChange, (char*)pgm_read_word(&(namePtrnChange[seq.ptrnChangeSync])));
          lcd.print(ptrnSyncChange);
          lcd.setCursor(4,1);
          char  mute[2];
          strcpy_P(mute, (char*)pgm_read_word(&(nameMute[seq.muteModeHH])));
          lcd.print(mute);
          lcd.setCursor(8,1);
#if MIDI_EXT_CHANNEL        
          lcd.print(seq.EXTchannel);
#else
          lcd.print("xxx");
#endif        
          lcd.setCursor(12,1);
#if CONFIG_BOOTMODE
          char bootmode[3];
          strcpy_P(bootmode,(char*)pgm_read_word(&(nameBootMode[seq.BootMode])));
          lcd.print(bootmode);
#else        
          lcd.print("xxx");
#endif        
          break;
        }
      case CONF_PAGE_EXT_VEL:  // [TR-909 STYLE] ext instrument velocity levels
        {
          // Two fields in the first two columns; the rest of the header names the page,
          // since three-letter abbreviations alone would not say which lane this is.
          lcd.print("low hi  ext vel ");
          lcd.setCursor(cursorPos[curIndex],0);
          lcd.print(letterUpConfPageExtVel[curIndex]);
          lcd.setCursor(0,1);
          LcdClearLine();
          lcd.setCursor(0,1);
          lcd.print(seq.extVelLow);
          lcd.setCursor(4,1);
          lcd.print(seq.extVelHigh);
          lcd.setCursor(8,1);
          lcd.print("tap 1/2");  // 7 chars from col 8: the last column stays on screen
          break;
        }
#if MIDI_HAS_SYSEX
      case CONF_PAGE_SYSEX: // SysEx dump page
        {
          if ( sysExDump < SYSEX_MAXPARAM )
          {
            lcd.print("type    select  ");
          } else {
            lcd.print("type            ");
          }
          lcd.setCursor(cursorPos[curIndex*2],0);
          lcd.print(letterUpConfPage3[curIndex]);
          lcd.setCursor(0,1);
          LcdClearLine();
          lcd.setCursor(0,1);
          char  sysex[5];
          strcpy_P(sysex, (char*)pgm_read_word(&(nameSysex[sysExDump])));
          lcd.print(sysex);
          lcd.setCursor(8,1);
          if ( sysExDump < SYSEX_MAXPARAM )
          {
            if ( sysExDump == 0 )
            {
              sysExParam = constrain(sysExParam, 0, MAX_BANK); // Banks
              lcd.print(char(sysExParam+65));
            } else if (sysExDump == 1) {
              sysExParam = constrain(sysExParam, 0, MAX_PTRN-1); // Patterns
              lcd.print(char((sysExParam / 16)+65));
              lcd.print(sysExParam - ((sysExParam / 16)*NBR_PATTERN) + 1);
            } else if  (sysExDump == 2) {
              sysExParam = constrain(sysExParam, 0, MAX_TRACK-1); // Tracks
              lcd.print(sysExParam + 1);
            }
          }
          break;
        }
#endif
      case CONF_PAGE_BOOT: // last page in either build
        {
          // Both of these were 17 characters on a 16-column display, so the panel showed
          // "PRESS ENC TO ACT" and the last character went to off-screen DDRAM - the same
          // bug already fixed for the startup splash in downtown-solutions_firmware.ino.
          lcd.print("   BOOTLOADER   ");
          lcd.setCursor(0,1);
          LcdClearLine();
          lcd.setCursor(0,1);
          lcd.print("SHIFT+ENC = GO  ");
          break;
        }
      }
    }
    else if (seq.sync == EXPANDER) {                                               // [1.028] Expander
      lcd.setCursor(0,0);
      lcd.print("    Expander    ");
      lcd.setCursor(0,1);
      lcd.print("                ");
    }
    
    else{
      switch (curSeqMode){
      case PTRN_PLAY:
ptrn_play:      
        lcd.setCursor(0,0);
        lcd.print("  Pattern Play  ");
        lcd.setCursor(0,1);
        LcdClearLine(); 
        lcd.setCursor(2,1);

        //[oort] better way to display pattern names
        displayBank = curPattern/NBR_PATTERN;
        displayPattern = curPattern % NBR_PATTERN;
        lcd.print(char(displayBank+65));
        lcd.print(displayPattern + 1);                      
  
        lcd.setCursor(9,1);
        LcdPrintTempo(); 
        previousMode = PTRN_PLAY;
        break;
      case PTRN_STEP:
      case PTRN_TAP:
ptrn_step:      
        if (curInst == TOTAL_ACC){
          LcdPrintTotalAcc();
        }
        else if (shufBtn.pressed){         
          lcd.setCursor(0,0);
          lcd.print("Shuffle:        ");
          lcd.setCursor(9,0);
          LcdPrintLine(7);
          lcd.setCursor(8 + pattern[ptrnBuffer].shuffle, 0);
          lcd.print((char)219);
          lcd.setCursor(0,1);
          lcd.print("Flam:           ");
          lcd.setCursor(8,1);
          LcdPrintLine(8);
          lcd.setCursor(8 + pattern[ptrnBuffer].flam, 1);
          lcd.print((char)219);        
        }
        else{
          lcd.setCursor(0,0);
          // In ext edit mode the last column carries the note value, so the track
          // number takes over its header - the label names what the encoder edits.
          if (curInst == EXT_INST && extInstEditMode) {
            lcd.print("ptr len scl ");
            lcd.print("T");
            lcd.print(currentExtTrack + 1);  // 1-16
            if (currentExtTrack + 1 < 10) lcd.print(" ");
          }
          else {
            lcd.print("ptr len scl ins ");
          }
          lcd.setCursor(0,1);
          LcdClearLine(); 
          lcd.setCursor(0,1);
          lcd.print(char(curBank+65));
          lcd.print(curPattern - (curBank*NBR_PATTERN) + 1);                     // [zabox] step button alignement
          lcd.setCursor(5,1);
          // [TR-909 STYLE] In ext edit mode the len field reports the EXT layer's length,
          // because that is what LAST STEP writes from this page. Showing the kit's length
          // here left the panel insisting on 16 while the MIDI tracks looped over four.
          if (curInst == EXT_INST && extInstEditMode) {
            lcd.print(pattern[ptrnBuffer].extLength+1);
            if (pattern[ptrnBuffer].extLength+1 < 10) lcd.print(" ");  // clear the tens column
          }
          else lcd.print(pattern[ptrnBuffer].length+1);
          lcd.setCursor(8,1);
          LcdPrintScale();
          lcd.setCursor(12,1);
          // [TR-909 STYLE] Value field shows the selected track's note as a name,
          // under the MIDI 60 = C4 convention documented on LcdPrintNoteName()
          if (curInst == EXT_INST && extInstEditMode) {
            byte w = LcdPrintNoteName(extTrackNote[currentExtTrack]);
            while (w++ < 4) lcd.print(" ");   // clear the rest of the field
          } else {
            char instName[3];
            strcpy_P(instName, (char*)pgm_read_word(&(selectInstString[curInst])));
            if (curFlam) {                                                            // test
              instName[1] = instName[1] + 32;
            }
            lcd.print(instName);
          }
        }
        previousMode = PTRN_STEP;
        break;
      case MUTE:
        switch (previousMode) //[oort] comment: Mute mode depends on previous modes, solved with gotos, not ideal solution.
        {
          case PTRN_STEP:
            goto ptrn_step;
            break;
          case PTRN_TAP:
            goto ptrn_step;
            break;
          case PTRN_PLAY:
            goto ptrn_play;
            break;
          case TRACK_WRITE:
            goto trck_write;
            break;
          case TRACK_PLAY:
            goto trck_play;
            break;
        }
        break;
      case TRACK_WRITE:
trck_write:
        lcd.setCursor(0,0);
        lcd.print("pos ptr len num ");
        lcd.setCursor(cursorPos[curIndex],0);
        lcd.print(letterUpTrackWrite[curIndex]);
        lcd.setCursor(0,1);
        LcdClearLine();
        lcd.setCursor(0,1);
        lcd.print(trk.pos + 1);                                                 // [zabox] 
        lcd.setCursor(4,1);
        if (curPattern == END_OF_TRACK )
        {
          lcd.print("END");
        } else {
          lcd.print((char)((curPattern / 16) + 65));
          lcd.print((curPattern - (((curPattern / 16)*NBR_PATTERN)) + 1));        // [zabox] step button alignement
        }
        lcd.setCursor(8,1);
        lcd.print(track[trkBuffer].length);
        lcd.setCursor(13,1);
        lcd.print(trk.current + 1);
        previousMode = TRACK_WRITE;
        break;
      case TRACK_PLAY:
trck_play:      
        lcd.setCursor(0,0);
        lcd.print(" Track Play:    ");   //[oort] make room for track number
        lcd.setCursor(13,0);
        lcd.print(trk.current + 1); //[oort] track number
        lcd.setCursor(0,1);
        LcdClearLine(); 
        lcd.setCursor(0,1);
        lcd.print("pos:");
        lcd.print(displayTrkPlayPos + 1); 
        lcd.setCursor(8,1);
        lcd.print("ptrn:");
        if (curPattern == END_OF_TRACK )
        {
          lcd.print("END");
        } else {
          lcd.print((char)((curPattern / NBR_PATTERN) + 65));                       //[oort] changed to NBR_PATTERN instead of 16
          lcd.print(curPattern - ((curPattern / NBR_PATTERN)*NBR_PATTERN) +1);      // [oort] this looks a bit strange, seems to work
        }
        previousMode = TRACK_PLAY;
        break;
      }
    }
  }
#if DEBUG
  //[oort] for traces
  lcd.setCursor(0,0);
  lcd.print(lcdVal);
#endif
}

//clear 16 character line-------------------------------------
void LcdClearLine()
{
  lcd.print("                ");//16 empty space to clear a line
}

//print special character for scale moniotring----------------
void LcdPrintScale()
{
  switch (pattern[ptrnBuffer].scale){
  case SCALE_32:
    lcd.write(byte(0));//1/
    lcd.write(byte(4));//32
    break;
  case  SCALE_16t:
    lcd.write(byte(0));//1/
    lcd.write(byte(1));//16
    lcd.write(byte(2));//t
    break;
  case  SCALE_16:
    lcd.write(byte(0));//1/
    lcd.write(byte(1));//16
    break;
  case  SCALE_8t:
    lcd.write(byte(0));//1/
    lcd.write(byte(3));//8
    lcd.write(byte(2));//t
    break;
  }
}

//Print tempo---------------------------------------------------------
void LcdPrintTempo()
{
  lcd.write(byte(5));
  lcd.print("-");
  if (seq.sync == MASTER){
    lcd.print(seq.bpm);
  } else {
    lcd.print((char)219);
    lcd.print((char)219);
    lcd.print((char)219);
  }
  lcd.print("  ");
}

//print special line--------------------------------------------------
void LcdPrintLine (byte lineSize)
{
  for (int a = 0; a < lineSize; a++){
    lcd.print((char) 161);
  }
}

//Lcd print saved !!!!  //[oort] not always working or shown too briefly
void LcdPrintSaved()
{
  lcd.setCursor(4,0);
  lcd.print("[SAVED!]");
}

//Lcd print Total Accent
void LcdPrintTotalAcc()
{
  lcd.setCursor(0,0);
  lcd.print("Total Acc value ");
  lcd.setCursor(0,1);
  LcdClearLine();
  lcd.setCursor(0,1);
  lcd.print("-");
  lcd.setCursor(1,1);
  LcdPrintLine(14);
  lcd.setCursor(15,1);
  lcd.print("+");
  lcd.setCursor(1 + pattern[ptrnBuffer].totalAcc, 1);
  lcd.print((char)219);
}

//Lcd print initialize EEprom
void  LcdPrintEEpromInit()
{
  lcd.setCursor(0,0);
  lcd.print("  init EEprom ? ");
  lcd.setCursor(0,1);
  LcdClearLine();
  lcd.setCursor(0,1);
  lcd.print("push START+ENTER");

}


//Lcd print TM2 adjustement
void  LcdPrintTM2Adjust()
{
  lcd.setCursor(0,0);
  lcd.print("Adjust TM2 until");
  lcd.setCursor(0,1);
  LcdClearLine();
  lcd.setCursor(0,1);
  lcd.print("TP1 is +5V...");
}


//-------------------------------------------------
//              Power-on fill animation
//-------------------------------------------------
// Dissolves the panel in from blank to fully lit: dots appear scattered across
// all 32 cells at once, in random order, and the version splash follows.
//
// The panel is 32 cells of 5x8 dots - 1280 of them - and the whole design turns
// on one hardware fact: an HD44780 can display at most EIGHT distinct
// programmable glyphs at any instant. Whatever the animation does, no more than
// eight *different* partly filled patterns can be on screen at one time. That
// leaves two shapes, and only one of them dissolves the whole panel:
//
//   - eight cells each own a slot and fill dot by dot, unique patterns, but only
//     ever eight cells in motion - the panel fills in clumps of eight
//   - every cell shares ONE ladder of eight increasing fill levels, and each
//     cell walks the ladder on its own schedule - all 32 cells can be mid-fill
//
// This is the second. The ladder is built from a random permutation of the 40
// dot positions, reshuffled every power-up, so the dots a level adds are
// scattered inside the cell rather than sweeping across it. The cost of sharing
// is that two cells sitting at the same level are identical; with nine density
// levels distributed at random over the panel that reads as a dissolve, and no
// arrangement of eight glyphs can do better.
//
// 1. shuffle the 40 dot positions
// 2. upload the ladder: glyph L lights the first LEVEL_DOTS(L) of that order,
//    so each glyph is the one before it plus a few more dots
// 3. each frame, bump one random cell that is not yet full to its next level,
//    which is a single character write - the glyphs never change again
// 4. a cell past the top of the ladder is written as 0xFF, the ROM's
//    all-dots-on block, and drops out of the pick
// 5. restore font0..font5, which the ladder borrowed
#define BOOT_ANIM_CELLS    32   // 16 columns x 2 rows
#define BOOT_ANIM_DOTS     40   // 5 columns x 8 rows per cell
#define BOOT_ANIM_LEVELS    8   // programmable glyphs on an HD44780
// Dots lit at ladder level 1..BOOT_ANIM_LEVELS. Level BOOT_ANIM_LEVELS+1 is the
// ROM block, so the divisor counts that final step too and the last glyph stops
// short of a full cell - otherwise the step onto the block would be invisible.
#define BOOT_ANIM_LEVEL_DOTS(l) (((word)BOOT_ANIM_DOTS * (l)) / (BOOT_ANIM_LEVELS + 1))
// Paces the dissolve at ~1.3s. Unlike a dot-at-a-time fill, a level bump is one
// character write (~0.9ms), and 288 of them would be over in a quarter second -
// too quick to read as anything but a flicker.
#define BOOT_ANIM_FRAME_US 3000
// Hold the full screen briefly so the completed state registers as the point of
// the animation, instead of being wiped the instant the last dot lands.
#define BOOT_ANIM_HOLD_MS 150

void LcdBootAnimation()
{
  byte dotOrder[BOOT_ANIM_DOTS];   // dot positions, shuffled: the order every cell fills in
  byte glyph[8];                   // ladder level under construction, one byte per dot row
  byte pending[BOOT_ANIM_CELLS];   // cells not yet full, compacted for the random pick
  byte level[BOOT_ANIM_CELLS];     // ladder level each cell is showing

  // random() replays the same sequence every power-up unless it is seeded, and
  // every code path ahead of this one is fixed, so micros() alone would not vary
  // either. The accent input is an analog pin whose low bits dither; XOR so a
  // quiet pin still leaves the timer's contribution.
  randomSeed(micros() ^ ((unsigned long)analogRead(TRIG2_PIN) << 8));

  for (byte i = 0; i < BOOT_ANIM_DOTS; i++) dotOrder[i] = i;
  for (byte i = BOOT_ANIM_DOTS - 1; i > 0; i--) {   // Fisher-Yates
    byte j = random(i + 1);
    byte swap = dotOrder[i];
    dotOrder[i] = dotOrder[j];
    dotOrder[j] = swap;
  }

  // Build the ladder once. Each level keeps the dots of the level below and adds
  // the next few from the shuffled order, so a cell climbing it only ever gains
  // dots - it must never appear to lose one.
  memset(glyph, 0, sizeof(glyph));
  byte lit = 0;
  for (byte l = 1; l <= BOOT_ANIM_LEVELS; l++) {
    while (lit < BOOT_ANIM_LEVEL_DOTS(l)) {
      byte dot = dotOrder[lit++];
      glyph[dot / 5] |= 0x10 >> (dot % 5);   // bit 4 is the leftmost dot column
    }
    lcd.createChar(l - 1, glyph);
  }

  lcd.clear();

  for (byte i = 0; i < BOOT_ANIM_CELLS; i++) {
    pending[i] = i;
    level[i] = 0;
  }

  byte count = BOOT_ANIM_CELLS;
  while (count) {
    byte idx = random(count);
    byte cell = pending[idx];
    byte lvl = ++level[cell];

    lcd.setCursor(cell & 15, cell >> 4);
    if (lvl > BOOT_ANIM_LEVELS) {
      lcd.write((byte)0xFF);
      pending[idx] = pending[--count];   // swap the tail down: the pick stays O(1)
    }
    else {
      lcd.write((byte)(lvl - 1));        // ladder level L lives in glyph L-1
    }

    delayMicroseconds(BOOT_ANIM_FRAME_US);
  }

  delay(BOOT_ANIM_HOLD_MS);

  // Slots 6 and 7 keep the two top ladder levels; nothing prints those codes,
  // and the first six are what the interface draws with.
  lcd.createChar(0, font0);
  lcd.createChar(1, font1);
  lcd.createChar(2, font2);
  lcd.createChar(3, font3);
  lcd.createChar(4, font4);
  lcd.createChar(5, font5);
}
