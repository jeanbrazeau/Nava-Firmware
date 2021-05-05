#ifndef _PATTERN_H_
#define _PATTERN_H_

//template< class T>
class CPattern {
  typedef enum Velo { off, low, high} Velo;
  
private:
  uint8_t nmbr;
  byte length;//0 to 15 steps
  byte scale;
  byte dir;//0=>forward, 1=>backword, 2=>ping-pong, 3=>random
  byte shuffle;
  byte flam; // 10, 14, 18, 22,26,30,34,38 ms, not affected by tempo
  unsigned int inst[NBR_INST];
  unsigned int step[NBR_STEP];
  Velo Velocity[NBR_STEP]; // off, low, high
  byte velocity[NBR_INST][NBR_STEP]; // Is this really needed ?
  byte extNote[MAX_EXT_INST_NOTE];// DUE to EEPROM 64 bytes PAGE WRITE
  byte extLength;
  byte groupPos;
  byte groupLength;
  byte totalAcc;

public:
  CPattern();
  ~CPattern() {};
  void SetHH(void);
  void Load(byte patternNbr);
  void Save(byte patternNbr);
  void CopyToBuffer(byte patternNbr);
  void PasteFromBuffer(byte patternNbr);
  void ShiftLeft(void);
  void ShiftRight(void);
  void Group(void);
};

extern CPattern Pat;
#endif
