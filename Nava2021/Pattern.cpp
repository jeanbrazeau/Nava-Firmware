#include "Pattern.h"

CPattern Pat;

CPattern::CPattern()
{
  length = 15; // steps 0 - 15
  scale = 0; // 16th notes
  shuffle = 0;
  flam = 0;
  extLength = 0;
  groupPos = 0;
  groupLength = 0;
  totalAcc = 0;
  for (int i = 0; i < NBR_INST; i++)
  {
    inst[i] = 0;
  }
  for (int i = 0; i < NBR_STEP; i++)
  {
    step[i] = 0;
    Velocity[i] = 0;
  }
  for (int i = 0; i < MAX_EXT_INST_NOTE; i++ )
  {
     extNote[i] = 0;
  }
}

void CPattern::SetHH(void)
{
  
}

void CPattern::Load(byte patternNbr)
{
  unsigned long adress = (unsigned long)(PTRN_OFFSET + patternNbr * PTRN_SIZE);
  WireBeginTX(adress); 
  Wire.endTransmission();
  Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request a 64 bytes page
  //TRIG-----------------------------------------------
  for(int i =0; i<NBR_INST;i++){
    inst[i] = (unsigned long)((Wire.read() & 0xFF) | (( Wire.read() << 8) & 0xFF00));
    // Serial.println(Wire.read());
  }
  //SETUP-----------------------------------------------
  length = Wire.read();
  scale = Wire.read();
  shuffle = Wire.read();
  flam = Wire.read();
  extLength = Wire.read();
  groupPos = Wire.read();
  groupLength = Wire.read();
  totalAcc = Wire.read();
  
  for(int a = 0; a < 24; a++){
    Wire.read();
  }
  //EXT INST-----------------------------------------------
  for(int nbrPage = 0; nbrPage < 2; nbrPage++){
    adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_SETUP_OFFSET);
    WireBeginTX(adress);
    Wire.endTransmission();
    Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of  64 bytes

    for (byte j = 0; j < MAX_PAGE_SIZE; j++){
      extNote[j + (MAX_PAGE_SIZE * nbrPage) ] = Wire.read();
    }
  }
  Serial.println();
  //VELOCITY-----------------------------------------------
  for(int nbrPage = 0; nbrPage < 4; nbrPage++){
    adress = (unsigned long)(PTRN_OFFSET + (patternNbr * PTRN_SIZE) + (MAX_PAGE_SIZE * nbrPage) + PTRN_EXT_OFFSET);
    WireBeginTX(adress);
    Wire.endTransmission();
    Wire.requestFrom(HRDW_ADDRESS,MAX_PAGE_SIZE); //request of  64 bytes
    for (byte i = 0; i < 4; i++){//loop as many instrument for a page
      char instName[4];
      strcpy_P(instName, (char*)pgm_read_word(&(selectInstString[i + 4*nbrPage])));
      Serial.print(instName);
      Serial.print(" = ");
      for (byte j = 0; j < NBR_STEP; j++){
        pattern[!ptrnBuffer].velocity[i + 4*nbrPage][j] = (Wire.read() & 0xFF);
        char hex[4];
        sprintf(hex,"%02X ",pattern[!ptrnBuffer].velocity[i + 4*nbrPage][j]);
        Serial.print(hex);
      }
      Serial.println();
    }
  }  
}

void CPattern::Save(byte patternNbr)
{
  
}

void CPatternCopyToBuffer(byte patternNbr)
{
  
}

void CPattern::PasteFromBuffer(byte patternNbr)
{
  
}

void CPattern::ShiftLeft(void)
{
  
}

void CPattern::ShiftRight(void)
{
  
}

void CPattern::Group(void)
{
  
}
