//-------------------------------------------------
//                  NAVA v1.x
//                  LCD String
// Strings to be stored as Program (not in RAM, but in Flash)
//-------------------------------------------------

#ifndef string_h
#define string_h



const char txt_INST0[] PROGMEM   ="TRG";
const char txt_INST1[] PROGMEM   ="   ";
const char txt_INST2[] PROGMEM   =" HT";
const char txt_INST3[] PROGMEM   ="RIM";
const char txt_INST4[] PROGMEM   ="HCL";
const char txt_INST5[] PROGMEM   ="   ";
const char txt_INST6[] PROGMEM   ="RID";
const char txt_INST7[] PROGMEM   ="CRH";
const char txt_INST8[] PROGMEM   =" BD";
const char txt_INST9[] PROGMEM   =" SD";
const char txt_INST10[] PROGMEM  =" LT";
const char txt_INST11[] PROGMEM  =" MT";
const char txt_INST12[] PROGMEM  ="ACC";
const char txt_INST13[] PROGMEM  ="EXT";
const char txt_INST14[] PROGMEM  =" CH";
const char txt_INST15[] PROGMEM  =" OH";

const char txt_SYNC0[] PROGMEM   ="MST";
const char txt_SYNC1[] PROGMEM   ="SLV";
const char txt_SYNC2[] PROGMEM   ="EXP";                                        // [zabox] [1.028] 

const char txt_PTRNSYNCCHANGE[] PROGMEM   ="SYN";
const char txt_PTRNFREECHANGE[] PROGMEM   ="FRE";

#if CONFIG_BOOTMODE
const char txt_BOOTMODE0[] PROGMEM = "TrPl";
const char txt_BOOTMODE1[] PROGMEM = "TrWr";
const char txt_BOOTMODE2[] PROGMEM = "Ptrn";
const char txt_BOOTMODE3[] PROGMEM = "Step";
const char txt_BOOTMODE4[] PROGMEM = "Tap ";
#endif

#if MIDI_HAS_SYSEX
const char txt_SYSEX0[] PROGMEM = "Bank";
const char txt_SYSEX1[] PROGMEM = "Pattern";
const char txt_SYSEX2[] PROGMEM = "Track";
const char txt_SYSEX3[] PROGMEM = "Config";
const char txt_SYSEX4[] PROGMEM = "Full";
PROGMEM const char * const nameSysex[]= { txt_SYSEX0, txt_SYSEX1, txt_SYSEX2, txt_SYSEX3, txt_SYSEX3 };
#endif

const char txt_MUTE0[] PROGMEM   ="C/O";                                        // [zabox] HH mute mode
const char txt_MUTE1[] PROGMEM   ="HH";                                         //

//synchro name----------------------------------------------------
PROGMEM const char * const nameSync[]={
  txt_SYNC0, txt_SYNC1, txt_SYNC2};                                            // [zabox] [1.028]
  
  //Pattern change sync name--------------------------------------
PROGMEM const char * const namePtrnChange[]={
  txt_PTRNFREECHANGE, txt_PTRNSYNCCHANGE};

#if CONFIG_BOOTMODE
PROGMEM const char * const nameBootMode[]={
  txt_BOOTMODE0, txt_BOOTMODE1, txt_BOOTMODE2, txt_BOOTMODE3, txt_BOOTMODE4};
#endif
  
//Mute Mode-------------------------------------------------------            // [zabox] HH mute mode
PROGMEM const char * const nameMute[]={
  txt_MUTE0, txt_MUTE1};
  
//instrument name-------------------------------------------------
PROGMEM const char * const selectInstString[]={
  txt_INST0,txt_INST1,txt_INST2,txt_INST3,
  txt_INST4,txt_INST5,txt_INST6,txt_INST7,
  txt_INST8,txt_INST9,txt_INST10,txt_INST11,
  txt_INST12,txt_INST13,txt_INST14,txt_INST15};

//Note names-------------------------------------------------------
// Fixed two-char cells rather than a pointer table: 24 bytes of PROGMEM against the
// ~60 that 12 strings plus 12 pointers would take, and the index is arithmetic.
// A trailing space marks a one-character name.
const char noteNames[] PROGMEM = "C C#D D#E F F#G G#A A#B ";

//Special character-----------------------------------------------
byte font0[8] = {
  0x10, 0x10, 0x11, 0x12, 0x04, 0x08, 0x10, 0x00};// 1/  font
byte font1[8] = {
  0x00, 0x00, 0x17, 0x14, 0x17, 0x15, 0x17, 0x00};// small 16 font
byte font2[8] = {
  0x00, 0x00, 0x10, 0x18, 0x10, 0x10, 0x0C, 0x00};//small T font
byte font3[8] = {
  0x00, 0x00, 0x0E, 0x0A, 0x0E, 0x0A, 0x0E, 0x00};//small 8 font
byte font4[8] = {
  0x00, 0x00, 0x1B, 0x09, 0x1B, 0x0A, 0x1B, 0x00};// Small 32 font
byte font5[8] = {
  0x01, 0x01, 0x01, 0x01, 0x01, 0x0F, 0x1F, 0x0E};//noir tempo

#endif//end if string_h
