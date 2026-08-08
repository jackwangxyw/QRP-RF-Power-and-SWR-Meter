// https://g8gyw.github.io/
//
// Copyright (c) 2021 Mike G8GYW
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// Use of the MegunoLINK filter library is licensed by the copyright owners under the terms of the
// GNU Lesser General Public License: https://github.com/Megunolink/MLP/blob/master/LICENSE.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// This program is designed to run on an Atmega328P with internal 8MHz clock and internal bandgap voltage reference.
// It takes the outputs from a Stockton bridge using a 10:1 transformer turns ratio and 1N5711 Scottky diodes,
// applies calibration factors then calculates and displays the average forward power, VSWR and battery voltage.

// ATMEGA FUSE VALUES
// E:FF   H:D7   L:E2

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Filter.h>

#define VERSION   "v1.06"
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// DEFINE VARIABLES

float Vfwd;   // Forward voltage
float Vrev;   // Reverse voltage
float Pfwd;   // Forward power
float Prev;   // Reverse power
float SWR;    // VSWR
float Gamma;  // Reflection coefficient
float Vbat;   // Battery voltage

unsigned long lastFlashTime = 0;
bool showWarning = true;
const float SWR_THRESHOLD = 3.0; // User configurable SWR limit

// CALIBRATION FACTORS
// The tolerance on the Atmega328P's internal voltage reference is 1.0 to 1.2 Volts.
// The actual voltage should be measured on pin 21 and entered below.

// The response of the 1N5711 diodes follows a curve represented by the equation:
// Pfwd = aVfwd^2 + bVfwd (and the same for Prev and Vrev)
// where a and b are constants determined by plotting Power In vs Vfwd and performing a curve fit.
// An excellent tool for this can be found at https://veusz.github.io/
// The values of a and b below can be adjusted if necessary to improve accuracy (try adjusting b first).

const float IntRef = 1.08;
const float a = .9;
const float b = 1.38;

// FUNCTIONS TO CALCULATE FORWARD AND REVERSE POWER

float CalculatePfwd ()
{
  float Vadc0 = analogRead(A0); // Read ADC0 (pin 23)
  Vadc0 = constrain(Vadc0, 1, 1023); // Prevent divide-by-zero when calculating Gamma
  Vfwd = 2.8 * ((Vadc0 + 0.5) * IntRef / 1024); // Scaled up by R3 & R4
  Pfwd = a * sq(Vfwd) + b * Vfwd;
  return Pfwd;
}

float CalculateSWR ()
{
  float Vadc0 = analogRead(A0); // Raw Forward ADC
  float Vadc1 = analogRead(A1); // Raw Reverse ADC
  
  // Prevent divide-by-zero
  if (Vadc0 <= Vadc1) return 99.9; 

  // Calculate SWR using raw voltage ratios to avoid power-calibration distortion
  float Vratio = (float)Vadc1 / (float)Vadc0;
  SWR = (1 + Vratio) / (1 - Vratio);

  // Still calculate Prev for the logic check if needed
  Vrev = 2.8 * ((Vadc1 + 0.5) * IntRef / 1024);
  Prev = a * sq(Vrev) + b * Vrev;

  SWR = constrain(SWR, 1, 99.9);
  return SWR;
}

//float CalculateSWR ()
//{
//  float Vadc1 = analogRead(A1); // Read ADC1 (pin 24)
//  Vrev = 2.8 * ((Vadc1 + 0.5) * IntRef / 1024); // Scaled up by R7 & R8
//  Prev = a * sq(Vrev) + b * Vrev;
//  if (Prev < 0.01)
//  { //exclude residual values
//    Prev = 0;
//  }
//  Gamma = sqrt(Prev / Pfwd); // Calculate reflection coefficient
//
//  SWR = (1 + Gamma) / (1 - Gamma);
//  SWR = constrain(SWR, 1, 99.9);
//  return SWR;
//}

// ADC FILTER
// A recursive filter removes jitter from the readings using the MegunoLINK filter library:
// https://www.megunolink.com/documentation/arduino-libraries/exponential-filter/

// Create new exponential filters with a weight of 90 and initial value of 0
// Adjust these values as required for a stable display

ExponentialFilter<float> FilteredPfwd(50, 0);
ExponentialFilter<float> FilteredVSWR(50, 0);

void setup()
{
  ADCSRA &= ~(bit (ADPS0) | bit (ADPS1) | bit (ADPS2)); // Clear prescaler bits
  ADCSRA |= bit (ADPS1) | bit (ADPS2);                  // Set prescaler to 64

// Enable the internal voltage reference and disable unused digital buffers
  analogReference (INTERNAL);
  bitSet (DIDR0, ADC3D);
  bitSet (DIDR0, ADC4D);
  bitSet (DIDR0, ADC5D);

  analogRead (A2);  // Read ADC2
  delay (500); // Allow ADC to settle
  float Vpot = analogRead (A2); // Read ADC again
  Vbat = 4.9 * ((Vpot + 0.5) * IntRef / 1024); // Calculate battery voltage scaled by R9 & R10

// Display startup screen
  Serial.begin(9600);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Helper variables to calculate perfect centering
  int16_t x1, y1;
  uint16_t w, h;

  // 1. Line 1: POWER/SWR METER (Centered)
  char line1[] = "POWER/SWR METER";
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.print(line1);

  // 2. Line 2: 12 Watts maximum (Centered)
  char line2[] = "12 Watts maximum";
  display.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 16);
  display.print(line2);

  // 3. Line 3: Battery: X.XXv (Formatted & Centered)
  String batString = "Battery: " + String(Vbat, 2) + "v";
  display.getTextBounds(batString, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 32);
  display.print(batString);

  // 4. Line 4: Version (Centered, no dashes)
  display.getTextBounds(VERSION, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 48);
  display.print(VERSION);

  display.display();
  delay(2500);

}

void loop()
{
  float Power = CalculatePfwd(); // Get new value of forward power
  FilteredPfwd.Filter(Power); // Apply filter to new value
  float SmoothPower = FilteredPfwd.Current(); // Return current value of filter output

  float VSWR = CalculateSWR(); // Get new value of VSWR
  FilteredVSWR.Filter(VSWR); // Apply filter to new value
  float SmoothVSWR = FilteredVSWR.Current(); // Return current value of filter output



// Handle flashing timer
if (millis() - lastFlashTime > 250) {
  showWarning = !showWarning;
  lastFlashTime = millis();
}

// Display Power and VSWR  
display.clearDisplay();
display.setTextSize(2);

// Use a fixed X-offset for values to keep them aligned
int valueX = 60; 

// --- POWER DISPLAY ---
display.setCursor(12, 12);
display.print("PWR:");

// Flash Power if > 12W 
if (SmoothPower > 12.0 && !showWarning) {
  // Stay blank for flashing effect
} else {
  display.setCursor(valueX, 12);
  display.print(SmoothPower, 1); // [cite: 43]
  display.print("W");
}

// --- SWR DISPLAY ---
display.setCursor(12, 36);
display.print("SWR:");

if (Pfwd < 0.01) {
  // Simple single-pixel dashes
  int dashWidth = 8;
  int dashSpacing = 6;
  int startX = 64;
  int startY = 42; // Adjusted slightly for a thin line look

  for (int i = 0; i < 3; i++) {
    int currentX = startX + (i * (dashWidth + dashSpacing));
    // Just draw one horizontal line per dash
    display.drawFastHLine(currentX, startY, dashWidth, WHITE);
  }
}
else if (SmoothVSWR >= SWR_THRESHOLD && !showWarning) {
  // Flash SWR if above threshold (currently set to 3.0)
} 
else {
  display.setCursor(valueX, 36);
  display.print(SmoothVSWR, 1);
}

display.display();

}
