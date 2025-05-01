#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ─── Hardware config ────────────────────────────────────────────────────────
#define SCREEN_W 132
#define SCREEN_H 64
#define BUTTON_PIN 16
#define LED_FLAP_PIN 17
#define LED_DEAD_PIN 18

// SH1106 132×64 via I2C, rotated 180°
U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R2, /* clock=*/1, /* data=*/0, /* reset=*/U8X8_PIN_NONE);

// ─── Game config ────────────────────────────────────────────────────────────
const float GRAVITY = 0.35;
const float FLAP_VEL = -2.8;
const int FRAME_MS = 50;
const int BIRD_X = 30;
const int BIRD_SIZE = 4;
const int PIPE_W = 12;
const int GAP_H = 26;
const int MAX_PIPES = 3;
const int PIPE_SPACING = SCREEN_W / MAX_PIPES;

// ─── Game state ─────────────────────────────────────────────────────────────
struct Pipe
{
  int x, gapY;
  bool scored;
} pipes[MAX_PIPES];
float birdY, birdVel;
int score;
bool gameOver;

// ─── Input tracking ─────────────────────────────────────────────────────────
bool lastBtnState = LOW;

// ─── Helpers ────────────────────────────────────────────────────────────────
void spawnPipe(int i)
{
  pipes[i].x = SCREEN_W + i * PIPE_SPACING;
  pipes[i].gapY = random(8, SCREEN_H - GAP_H - 8);
  pipes[i].scored = false;
}

void resetGame()
{
  birdY = SCREEN_H / 2;
  birdVel = 0;
  score = 0;
  gameOver = false;
  for (int i = 0; i < MAX_PIPES; i++)
    spawnPipe(i);
  digitalWrite(LED_DEAD_PIN, LOW);
}

inline bool buttonPressed()
{
  return digitalRead(BUTTON_PIN) == HIGH;
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup()
{
  u8g2.begin();
  u8g2.setFontPosTop();

  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_FLAP_PIN, OUTPUT);
  pinMode(LED_DEAD_PIN, OUTPUT);

  randomSeed(micros());
  resetGame();
}

// ─── Main loop ─────────────────────────────────────────────────────────────
void loop()
{
  static unsigned long lastFrame = 0;
  if (millis() - lastFrame < FRAME_MS)
    return;
  lastFrame = millis();

  // ── Input & edge‐detect flap ─────────────────────────────────────────────
  bool curBtn = buttonPressed();
  if (!gameOver && curBtn && !lastBtnState)
  {
    // rising edge: flap once
    birdVel = FLAP_VEL;
    digitalWrite(LED_FLAP_PIN, HIGH);
  }
  else
  {
    digitalWrite(LED_FLAP_PIN, LOW);
  }
  lastBtnState = curBtn;

  // ── Physics ──────────────────────────────────────────────────────────────
  if (!gameOver)
  {
    birdVel += GRAVITY;
    birdY += birdVel;
  }

  // ── Move pipes & scoring ─────────────────────────────────────────────────
  if (!gameOver)
  {
    for (int i = 0; i < MAX_PIPES; i++)
    {
      pipes[i].x -= 2;
      if (pipes[i].x + PIPE_W < 0)
        spawnPipe(i);
      if (!pipes[i].scored && pipes[i].x + PIPE_W < BIRD_X)
      {
        pipes[i].scored = true;
        score++;
      }
    }
  }

  // ── Collision ─────────────────────────────────────────────────────────────
  if (!gameOver)
  {
    if (birdY < 0 || birdY + BIRD_SIZE > SCREEN_H)
      gameOver = true;
    for (int i = 0; i < MAX_PIPES; i++)
    {
      int px = pipes[i].x, gy = pipes[i].gapY;
      if (BIRD_X + BIRD_SIZE > px && BIRD_X < px + PIPE_W)
      {
        if (birdY < gy || birdY + BIRD_SIZE > gy + GAP_H)
        {
          gameOver = true;
        }
      }
    }
    if (gameOver)
      digitalWrite(LED_DEAD_PIN, HIGH);
  }

  // ── Draw ─────────────────────────────────────────────────────────────────
  u8g2.clearBuffer();
  if (!gameOver)
  {
    // Bird
    u8g2.drawBox(BIRD_X, (int)birdY, BIRD_SIZE, BIRD_SIZE);
    // Pipes
    for (int i = 0; i < MAX_PIPES; i++)
    {
      int x = pipes[i].x, gy = pipes[i].gapY;
      u8g2.drawBox(x, 0, PIPE_W, gy);
      u8g2.drawBox(x, gy + GAP_H, PIPE_W, SCREEN_H - (gy + GAP_H));
    }
    // Score
    char s[5];
    snprintf(s, sizeof(s), "%d", score);
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(2, 2, s);
  }
  else
  {
    // Game Over screen
    char s[16];
    snprintf(s, sizeof(s), "Score: %d", score);
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr((SCREEN_W - u8g2.getStrWidth("GAME OVER")) / 2, 10, "GAME OVER");
    u8g2.drawStr((SCREEN_W - u8g2.getStrWidth(s)) / 2, 26, s);
    u8g2.drawStr((SCREEN_W - u8g2.getStrWidth("Retry?")) / 2, 42, "Retry?");
    if (buttonPressed())
    {
      delay(100);
      while (buttonPressed())
        ;
      resetGame();
    }
  }
  u8g2.sendBuffer();
}
