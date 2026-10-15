#include <Arduino.h>

// ============================================================
// G-TAG 6 + ESP8266
//
// Белый      DIO   -> D2
// Коричневый CLK   -> D1
// Синий      CS    -> D5
// Оранжевый  RESET -> D6
// Черный     GND   -> GND
//
// Желтый DISCLOCK 29 kHz НЕ подключаем.
// Ценник питается от своей батарейки.
// ============================================================

#define PIN_DIO    D2
#define PIN_CLK    D1
#define PIN_CS     D5
#define PIN_RESET  D6

#define LCD_W 256
#define LCD_H 128
#define FB_SIZE 4096

uint8_t fb[FB_SIZE];

// ============================================================
// CLOCK
// ============================================================

static inline void pulseClock()
{
  digitalWrite(PIN_CLK, HIGH);
  delayMicroseconds(1);

  digitalWrite(PIN_CLK, LOW);
  delayMicroseconds(1);
}

// ============================================================
// 9-BIT MSB
// Команды и параметры инициализации
// ============================================================

static inline void send9_MSB_raw(bool data, uint8_t value)
{
  // 9-й бит: command/data
  digitalWrite(PIN_DIO, data ? HIGH : LOW);
  pulseClock();

  for (int i = 7; i >= 0; i--)
  {
    digitalWrite(PIN_DIO, (value >> i) & 1);
    pulseClock();
  }
}

void send9_MSB(bool data, uint8_t value)
{
  digitalWrite(PIN_CS, LOW);
  send9_MSB_raw(data, value);
  digitalWrite(PIN_CS, HIGH);

  delayMicroseconds(2);
}

// ============================================================
// PIXEL BYTE
//
// Повторяет принцип send_9bit_LSB автора:
//
// DATA bit = 1
// затем framebuffer:
// LSB first
// плюс инверсия уровня.
//
// CS здесь НЕ трогаем.
// ============================================================

static inline void sendPixelByte(uint8_t value)
{
  // DATA = 1
  digitalWrite(PIN_DIO, HIGH);
  pulseClock();

  for (uint8_t i = 0; i < 8; i++)
  {
    // В рабочем коде автора:
    // 1 -> LOW
    // 0 -> HIGH

    digitalWrite(
      PIN_DIO,
      (value & 0x01) ? LOW : HIGH
    );

    pulseClock();

    value >>= 1;
  }
}

// ============================================================
// LCD INIT
// ============================================================

void lcdInit()
{
  Serial.println("RESET");

  digitalWrite(PIN_CS, HIGH);

  digitalWrite(PIN_RESET, LOW);
  delayMicroseconds(50);

  digitalWrite(PIN_RESET, HIGH);
  delay(50);

  // ----------------------------------------------------------
  // Sleep out
  // ----------------------------------------------------------

  Serial.println("Sleep out");

  send9_MSB(false, 0x11);

  // ----------------------------------------------------------
  // Очистка LCD
  // ----------------------------------------------------------

  Serial.println("Initial clear");

  digitalWrite(PIN_CS, LOW);

  send9_MSB_raw(false, 0x2A);
  send9_MSB_raw(true,  0x00);
  send9_MSB_raw(true,  0x00);

  send9_MSB_raw(false, 0x2C);

  for (uint16_t i = 0; i < 4096; i++)
  {
    send9_MSB_raw(true, 0xFF);
  }

  digitalWrite(PIN_CS, HIGH);

  delay(10);

  // ----------------------------------------------------------
  // 0x4C
  // ----------------------------------------------------------

  digitalWrite(PIN_CS, LOW);

  send9_MSB_raw(false, 0x4C);
  send9_MSB_raw(true,  0x0C);
  send9_MSB_raw(true,  0x00);
  send9_MSB_raw(true,  0x00);

  digitalWrite(PIN_CS, HIGH);

  delay(4);

  // ----------------------------------------------------------
  // 0x4D
  // ----------------------------------------------------------

  digitalWrite(PIN_CS, LOW);

  send9_MSB_raw(false, 0x4D);
  send9_MSB_raw(true,  0xFF);
  send9_MSB_raw(true,  0x00);
  send9_MSB_raw(true,  0x7F);

  digitalWrite(PIN_CS, HIGH);

  delay(1);

  // ----------------------------------------------------------
  // 0x4E
  // ----------------------------------------------------------

  digitalWrite(PIN_CS, LOW);

  send9_MSB_raw(false, 0x4E);
  send9_MSB_raw(true,  0x60);

  digitalWrite(PIN_CS, HIGH);

  Serial.println("LCD INIT DONE");
}

// ============================================================
// FRAMEBUFFER
//
// 256 / 8 = 32 байта в каждой строке.
//
// Это ВАЖНО:
//
// y=0:
// byte 0 = pixels x0..7
// byte 1 = pixels x8..15
//
// y=1:
// byte 32 = pixels x0..7
//
// и т.д.
// ============================================================

void clearFB()
{
  memset(fb, 0x00, sizeof(fb));
}

void setPixel(int x, int y, bool black = true)
{
  if (x < 0 || x >= LCD_W ||
      y < 0 || y >= LCD_H)
    return;

  uint16_t index =
      ((uint16_t)y * 32) + (x >> 3);

  // ВАЖНО:
  // x=0 -> bit 7
  // x=1 -> bit 6
  // ...
  // x=7 -> bit 0
  uint8_t mask = 0x80 >> (x & 7);

  if (black)
    fb[index] |= mask;
  else
    fb[index] &= ~mask;
}

// ============================================================
// ГРАФИКА
// ============================================================

void fillRect(int x, int y, int w, int h, bool black = true)
{
  for (int yy = y; yy < y + h; yy++)
  {
    for (int xx = x; xx < x + w; xx++)
    {
      setPixel(xx, yy, black);
    }
  }
}

void drawRect(int x, int y, int w, int h, int t = 1)
{
  fillRect(x, y, w, t);
  fillRect(x, y + h - t, w, t);

  fillRect(x, y, t, h);
  fillRect(x + w - t, y, t, h);
}

// ============================================================
// FONT 5x7
// ============================================================

const uint8_t FONT[26][5] =
{
  {0x7E,0x11,0x11,0x11,0x7E}, // A
  {0x7F,0x49,0x49,0x49,0x36}, // B
  {0x3E,0x41,0x41,0x41,0x22}, // C
  {0x7F,0x41,0x41,0x22,0x1C}, // D
  {0x7F,0x49,0x49,0x49,0x41}, // E
  {0x7F,0x09,0x09,0x09,0x01}, // F
  {0x3E,0x41,0x49,0x49,0x7A}, // G
  {0x7F,0x08,0x08,0x08,0x7F}, // H
  {0x00,0x41,0x7F,0x41,0x00}, // I
  {0x20,0x40,0x41,0x3F,0x01}, // J
  {0x7F,0x08,0x14,0x22,0x41}, // K
  {0x7F,0x40,0x40,0x40,0x40}, // L
  {0x7F,0x02,0x0C,0x02,0x7F}, // M
  {0x7F,0x04,0x08,0x10,0x7F}, // N
  {0x3E,0x41,0x41,0x41,0x3E}, // O
  {0x7F,0x09,0x09,0x09,0x06}, // P
  {0x3E,0x41,0x51,0x21,0x5E}, // Q
  {0x7F,0x09,0x19,0x29,0x46}, // R
  {0x46,0x49,0x49,0x49,0x31}, // S
  {0x01,0x01,0x7F,0x01,0x01}, // T
  {0x3F,0x40,0x40,0x40,0x3F}, // U
  {0x1F,0x20,0x40,0x20,0x1F}, // V
  {0x7F,0x20,0x18,0x20,0x7F}, // W
  {0x63,0x14,0x08,0x14,0x63}, // X
  {0x03,0x04,0x78,0x04,0x03}, // Y
  {0x61,0x51,0x49,0x45,0x43}  // Z
};

void drawChar(int x, int y, char c, int scale)
{
  if (c < 'A' || c > 'Z')
    return;

  const uint8_t *g = FONT[c - 'A'];

  for (int col = 0; col < 5; col++)
  {
    for (int row = 0; row < 7; row++)
    {
      if (g[col] & (1 << row))
      {
        fillRect(
          x + col * scale,
          y + row * scale,
          scale,
          scale
        );
      }
    }
  }
}

void drawText(int x, int y, const char *text, int scale)
{
  while (*text)
  {
    if (*text == ' ')
    {
      x += 4 * scale;
    }
    else
    {
      drawChar(x, y, *text, scale);
      x += 6 * scale;
    }

    text++;
  }
}

// ============================================================
// HEART
// ============================================================

void drawHeart(int x, int y, int s)
{
  const char *heart[] =
  {
    "0110110",
    "1111111",
    "1111111",
    "1111111",
    "0111110",
    "0011100",
    "0001000"
  };

  for (int yy = 0; yy < 7; yy++)
  {
    for (int xx = 0; xx < 7; xx++)
    {
      if (heart[yy][xx] == '1')
      {
        fillRect(
          x + xx * s,
          y + yy * s,
          s,
          s
        );
      }
    }
  }
}

// ============================================================
// SEND FRAMEBUFFER
// ============================================================

void sendFramebuffer()
{
  Serial.println("SEND FRAMEBUFFER");

  digitalWrite(PIN_CS, LOW);

  // address
  send9_MSB_raw(false, 0x2A);
  send9_MSB_raw(true,  0x00);
  send9_MSB_raw(true,  0x00);

  // memory write
  send9_MSB_raw(false, 0x2C);

  // Теперь графический framebuffer
  for (uint16_t i = 0; i < FB_SIZE; i++)
  {
    sendPixelByte(fb[i]);
  }

  digitalWrite(PIN_CS, HIGH);

  Serial.println("FRAMEBUFFER DONE");
}

// ============================================================
// TEST IMAGE
// ============================================================

void drawTestScreen()
{
  clearFB();

  // рамка
  drawRect(2, 2, 252, 124, 2);

  // текст
  drawText(35, 14, "HELLO WORLD", 3);

  // линия
  fillRect(12, 43, 232, 2);

  // крупнее
  drawText(48, 58, "G TAG", 4);

  // сердце
  drawHeart(111, 92, 3);

  // квадраты для контроля геометрии
  fillRect(18, 102, 16, 16);
  fillRect(222, 102, 16, 16);

  sendFramebuffer();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(300);

  pinMode(PIN_DIO, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_RESET, OUTPUT);

  digitalWrite(PIN_DIO, LOW);
  digitalWrite(PIN_CLK, LOW);
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_RESET, HIGH);

  Serial.println();
  Serial.println("======================");
  Serial.println("G-TAG 6 ESP8266");
  Serial.println("======================");

  delay(1000);

  lcdInit();

  delay(200);

  drawTestScreen();

  Serial.println("DONE");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  // Ничего не обновляем.
}