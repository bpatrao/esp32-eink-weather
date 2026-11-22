// Driver test for LilyGO T5 V2.3 2.13" e-paper.
// Draws a border, the driver name and a few shapes. If the image is clean,
// sharp and fills the whole panel, the driver is right.

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// T5 V2.3 display pins
#define EPD_CS   5
#define EPD_DC   17
#define EPD_RST  16
#define EPD_BUSY 4

#if defined(PANEL_B73)
  #define DRIVER_NAME "GDEH0213B73"
  GxEPD2_BW<GxEPD2_213_B73, GxEPD2_213_B73::HEIGHT> display(GxEPD2_213_B73(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#elif defined(PANEL_B1)
  #define DRIVER_NAME "GDE0213B1"
  GxEPD2_BW<GxEPD2_213, GxEPD2_213::HEIGHT> display(GxEPD2_213(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#elif defined(PANEL_BN)
  #define DRIVER_NAME "DEPG0213BN"
  GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#else
  #error "Select a panel: build with -DPANEL_B73, -DPANEL_B1 or -DPANEL_BN"
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nT5 V2.3 e-paper test — driver: %s\n", DRIVER_NAME);

  display.init(115200);
  display.setRotation(1);  // landscape, 250x122
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    int w = display.width(), h = display.height();

    // Border hugging the panel edges — if it's cut off or offset, wrong driver.
    display.drawRect(0, 0, w, h, GxEPD_BLACK);
    display.drawRect(2, 2, w - 4, h - 4, GxEPD_BLACK);

    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(10, 30);
    display.print("Hello, T5!");

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 55);
    display.print(DRIVER_NAME);
    display.setCursor(10, 75);
    display.printf("%dx%d", w, h);

    // Shapes in the bottom-right corner
    display.fillCircle(w - 30, h - 30, 18, GxEPD_BLACK);
    display.fillRect(w - 75, h - 48, 30, 30, GxEPD_BLACK);
    display.drawLine(10, h - 12, w - 90, h - 12, GxEPD_BLACK);
  } while (display.nextPage());

  display.hibernate();
  Serial.println("Done. Check the screen.");
}

void loop() {}
