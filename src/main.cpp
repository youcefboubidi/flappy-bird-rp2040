#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <LittleFS.h>               // ← LittleFS include

// ─── Hardware config ────────────────────────────────────────────────────────
#define SCREEN_W      128           // your SH1106 is 132×64
#define SCREEN_H       64
#define BUTTON_PIN     16
#define BTN_UP       19
#define BTN_DOWN     21
#define BTN_LEFT     18
#define BTN_RIGHT    20
#define BTN_SELECT   16
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);

// ─── Game config ────────────────────────────────────────────────────────────
const float GRAVITY       = 0.35;
const float FLAP_VEL      = -3.5;
const int   FRAME_MS      = 50;
const int   BIRD_X        = 30;
const int   BIRD_SIZE     = 4;
const int   PIPE_W        = 12;
const int   GAP_H         = 26;
const int   MAX_PIPES     = 3;
const int   PIPE_SPACING  = SCREEN_W / MAX_PIPES;

// ─── Highscore name storage ─────────────────────────────────────────────────
const char* HS_FILE       = "/highscore.txt";
const char* HSNAME_FILE   = "/highscore_name.txt";
int hs = 0;
char hsName[17] = {0};

// ─── Keyboard config for name entry ─────────────────────────────────────────
const uint8_t GRID_ROWS = 4;
const uint8_t GRID_COLS = 9;
const char* rows[3] = { "abcdefgh", "ijklmnopq", "rstuvwxyz" };
char nameBuf[17] = {0};
uint8_t nameLen = 0;
uint8_t curRow = 0, curCol = 0;
bool nameEntered = false;

struct Btn { uint8_t pin; bool last; };
Btn buttons[] = {{BTN_UP,HIGH},{BTN_DOWN,HIGH},{BTN_LEFT,HIGH},{BTN_RIGHT,HIGH},{BTN_SELECT,HIGH}};
bool justPressed(uint8_t i) {
  bool cur = digitalRead(buttons[i].pin);
  bool jp = (buttons[i].last==HIGH && cur==LOW);
  buttons[i].last = cur;
  return jp;
}

// ─── FS helpers ─────────────────────────────────────────────────────────────
int readHighScore() {
    // Show "Loading..." on screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr((SCREEN_W - u8g2.getStrWidth("Loading...")) / 2, 10, "Loading...");
  u8g2.sendBuffer();
  if (!LittleFS.exists(HS_FILE)) return 0;
  File f = LittleFS.open(HS_FILE, "r");
  int v = f.parseInt(); f.close();
    // Optionally clear after
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  return v;
}

void saveHighScore(int v) {
  File f = LittleFS.open(HS_FILE, "w"); f.print(v); f.close();
}

void readHighScoreName() {
  // Show "Loading..." on screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr((SCREEN_W - u8g2.getStrWidth("Loading...")) / 2, 10, "Loading...");
  u8g2.sendBuffer();


  if (!LittleFS.exists(HSNAME_FILE)) { hsName[0] = '\0'; return; }
  File f = LittleFS.open(HSNAME_FILE, "r");
  size_t len = f.readBytes(hsName, sizeof(hsName)-1);
  hsName[len] = '\0'; f.close();
    // Optionally clear after
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

void saveHighScoreName(const char* n) {
  File f = LittleFS.open(HSNAME_FILE, "w"); f.print(n); f.close();
}

// ─── Game state ─────────────────────────────────────────────────────────────
struct Pipe { int x, gapY; bool scored; } pipes[MAX_PIPES];
float birdY, birdVel;
int score;
bool gameOver;
bool newHighAchieved = false;
bool inSaveMenu = false;
int menuSel = 0; // 0=Save,1=Don't Save

// track button edge for flap
bool lastFlapState = false;

// ─── Helpers ────────────────────────────────────────────────────────────────
void spawnPipe(int i) {
  pipes[i].x = SCREEN_W + i*PIPE_SPACING;
  pipes[i].gapY = random(8, SCREEN_H - GAP_H - 8);
  pipes[i].scored = false;
}

void resetGame() {
  hs = readHighScore();
  readHighScoreName();
  birdY = SCREEN_H/2; birdVel=0; score=0; gameOver=false;
  newHighAchieved = false; inSaveMenu = false; menuSel=0;
  lastFlapState = false;
  for(int i=0;i<MAX_PIPES;i++) spawnPipe(i);
}

inline bool buttonPressed() { return digitalRead(BUTTON_PIN)==LOW; }

// ─── Name entry screen ─────────────────────────────────────────────────────// ─── Name entry screen ─────────────────────────────────────────────────────
void keyboardScreen() {
  if (justPressed(0) && curRow>0) curRow--;
  if (justPressed(1) && curRow+1<GRID_ROWS) curRow++;
  if (justPressed(2) && curCol>0) curCol--;
  if (justPressed(3) && curCol+1<GRID_COLS) curCol++;
  if (justPressed(4)) {
    if (curRow<3) {
      uint8_t len = strlen(rows[curRow]);
      if (curCol<len && nameLen<sizeof(nameBuf)-1) {
        nameBuf[nameLen++]=rows[curRow][curCol]; nameBuf[nameLen]='\0';
      }
    } else {
      if (curCol==7 && nameLen) nameBuf[--nameLen]='\0';
      else if (curCol==8) { nameEntered=true; }
    }
  }

  u8g2.clearBuffer();
  uint8_t cw = SCREEN_W / GRID_COLS;
  uint8_t ch = SCREEN_H / GRID_ROWS;

  for (uint8_t r = 0; r < GRID_ROWS; r++) {
    for (uint8_t c = 0; c < GRID_COLS; c++) {
      uint8_t x = c * cw;
      int y = r * ch - 4;
      // if bottom row, lift up a few pixels
      if (r == GRID_ROWS - 1) y -= 4;
      bool sel = (r == curRow && c == curCol);

      if (sel) {
        u8g2.setDrawColor(1);
        u8g2.drawBox(x, y, cw, ch);
        u8g2.setDrawColor(0);
      }

      char chh = ' ';
      if (r < 3) {
        if (c < (int)strlen(rows[r])) chh = rows[r][c];
      } else {
        if (c < nameLen && c < 7) chh = nameBuf[c];
        else if (c == 7) chh = '<';
        else if (c == 8) chh = '>';
      }

      u8g2.setFont(u8g2_font_7x14B_tr);
      u8g2.drawStr(x + cw/4, y + ch/4, String(chh).c_str());
      u8g2.setDrawColor(1);
    }
  }

  u8g2.sendBuffer();
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Wire.setSDA(0); Wire.setSCL(1); Wire.begin();
  u8g2.begin(); u8g2.setFontPosTop();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  randomSeed(micros());
  for(auto &b:buttons) pinMode(b.pin, INPUT_PULLUP);
  if(!LittleFS.begin()){ Serial.begin(115200); Serial.println("LittleFS mount failed!"); }
  resetGame();
}

// ─── Main loop ─────────────────────────────────────────────────────────────
void loop() {
  if (inSaveMenu) {
    // draw save menu
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(0,0,"NEW HIGH");
    char buf[16]; snprintf(buf,sizeof(buf),"%d", hs);
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(64, 20, buf);
    // options
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 34, "Save Score:");
    bool sel1 = (menuSel==1);
    if(sel1) u8g2.drawFilledEllipse(3*SCREEN_W/4,55,2,2,U8G2_DRAW_ALL);
    u8g2.drawStr(10,45,"No Name");
    bool sel0 = (menuSel==0);
    if(sel0) u8g2.drawFilledEllipse(SCREEN_W/4,55,2,2,U8G2_DRAW_ALL);
    u8g2.drawStr(70,45,"With Name");
    u8g2.sendBuffer();
    if(justPressed(2)||justPressed(3)) menuSel = 1 - menuSel;
    if(justPressed(4)) {
      if(menuSel==1) {
        nameEntered=false; nameLen=0; curRow=0; curCol=0;
        while(!nameEntered) keyboardScreen();
        strncpy(hsName, nameBuf, sizeof(hsName));
        saveHighScore(hs);
        saveHighScoreName(hsName);
      }
      if(menuSel==0){
        saveHighScore(hs);
        saveHighScoreName("unknown");
      }
      inSaveMenu=false;
      delay(200);
      resetGame();
    }
    return;
  }

  static unsigned long lastFrame=0;
  if(millis()-lastFrame<FRAME_MS) return;
  lastFrame=millis();

  bool curBtn = buttonPressed();
  // only trigger flap on button press edge
  if(!gameOver && curBtn && !lastFlapState) {
    birdVel = FLAP_VEL;
  }
  lastFlapState = curBtn;

  // update physics
  if(!gameOver) {
    birdVel+=GRAVITY; birdY+=birdVel;
    for(int i=0;i<MAX_PIPES;i++){
      pipes[i].x-=2;
      if(pipes[i].x+PIPE_W<0) spawnPipe(i);
      if(!pipes[i].scored && pipes[i].x+PIPE_W<BIRD_X){ pipes[i].scored=true; score++; }
    }
    if(birdY<0||birdY+BIRD_SIZE>SCREEN_H) gameOver=true;
    for(int i=0;i<MAX_PIPES;i++){
      int px=pipes[i].x, gy=pipes[i].gapY;
      if(BIRD_X+BIRD_SIZE>px && BIRD_X<px+PIPE_W && (birdY<gy||birdY+BIRD_SIZE>gy+GAP_H)) gameOver=true;
    }
    if(gameOver && score>hs) { hs=score; newHighAchieved=true; inSaveMenu=true; }
  }

  // draw
  u8g2.clearBuffer();
  if(!gameOver) {
    u8g2.drawBox(BIRD_X,(int)birdY,BIRD_SIZE,BIRD_SIZE);
    for(int i=0;i<MAX_PIPES;i++){
      u8g2.drawBox(pipes[i].x,0,PIPE_W,pipes[i].gapY);
      u8g2.drawBox(pipes[i].x,pipes[i].gapY+GAP_H,PIPE_W,SCREEN_H-(pipes[i].gapY+GAP_H));
    }
    char s[6]; snprintf(s,sizeof(s),"%d",score);
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(2,2,s);
  } else {
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr((SCREEN_W-u8g2.getStrWidth("GAME OVER"))/2,0,"GAME OVER");
    char buf2[16]; snprintf(buf2,sizeof(buf2),"Highscore: %d",hs);
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr((SCREEN_W-u8g2.getStrWidth(buf2))/2,20,buf2);
    if(strlen(hsName)>0) {
      char byBuf[20]; snprintf(byBuf,sizeof(byBuf),"By: %s",hsName);
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr((SCREEN_W-u8g2.getStrWidth(byBuf))/2,40,byBuf);
    }
    u8g2.setFont(u8g2_font_6x10_tr);
    if(buttonPressed()) { delay(100); while(buttonPressed()); resetGame(); }
  }
  u8g2.sendBuffer();
}
