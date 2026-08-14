# QRP RF Power and SWR Meter

This is a compact standalone power and SWR meter designed for field use. The rectifier diodes used in this design have relatively flat performance up to 50MHz, so it is able to cover all of the HF bands. It is built around the ATtiny 3224 microcontroller, so writing your own or modifying the existing code is easy. 

![Render Main](Images/QRP%20Power%20and%20SWR%20Meter%20Render%20Main.png)
![Render Connectors](Images/QRP%20Power%20and%20SWR%20Meter%20Render%20Connectors.png)

## Features

- Accurate Power and SWR measurements for HF + 6m
- 0.96in OLED display
  - Numerical and graph views for power and SWR
  - Peak hold indicator on power bar
- 25w max power limit
- Built in 1000mAh battery for over 40 hours of continuous use, 120 hours of realistic field use, and months on standby
- Auto dim, screen off, and standby modes, turned on by RF
- USB-C charging
  - LED turns on when charging, and will turn off when full
  - Can charge while the switch is in the off position
- Reverse polarity protection for the battery
- Watts or dBm display
- Built in calibration mode (Displays raw ADC counts, more info below)
- Basic warning features
  - When SWR or power goes over a certain threshold, that number will blink as a warning.
 

## Changelog
Rev 2 - Switched to the ATtiny 3224 microcontroller from the old and obsolete ATmega 328p, switched to a 4 layer board, added onboard lipo charging, connectorized the OLED, among various other small improvements.


## Usage
To use the meter, simply flip the switch to the on position, plug in your RF input and output, and you're off to the races. If you want to switch the unit for power measurements between Watts and dBm, simply short press the tactile button. When no RF is passed through the meter, the screen will dim, then turn off, then go into standby mode in intervals set in the [Code/SRC/config.h](Code/SRC/config.h) file. Passing RF through the meter will turn the screen back on and put it into active mode. (Note that the button does nothing when the screen is off.) You can also slide the switch off and back on again to put the device back into active mode as well. This feature is just an auto standby, not a true auto off, that is handled with the physical slide switch.

When the device is turned on via the switch, a splash screen shows indicating the software version and battery voltage. Then it jumps to the main screen. 

![Main screen](Images/QRP%20Power%20and%20SWR%20Meter%20Screen%20Main.png)

## Calibration
This board has a built in calibration mode that you can enter by long pressing the tactile button on the PCB. This mode will display the raw adc counts for the VFWD and VREV that the microcontroller reads. To be clear, you **DO NOT** need to do this in normal use, the default values in the code have already been tested and are accurate. Only do this if you have the precision equipment to do so and want to gain that last 1-5% of accuracy. (That accuracy will probably be gone anyway as the diodes' responses vary with temperature and other factors anyway). You can also check the resting VFWD count of WAKE_COUNTS in [Code/SRC/config.h](Code/SRC/config.h). The default value should work, but if you notice your value is higher than the default, the device will never go into standby, so you can change that.

If you notice that your values are off by a consistent amount at every power level, you can change the CAL_OFFSET_DB in [Code/SRC/config.h](Code/SRC/config.h). (Again, this is **NOT NEEDED** for most people, only for the edge cases where that batch of diodes may be off) (Only do this if you notice something consistently wrong when comparing with precision equipment)

To calibrate the power measurements, take a RF source of known power output and frequency and plug this into the RF input, plug a dummy load into the RF output, transmit at many different power levels, record the VFWD ADC count and the power you inputted (in dBm) for every datapoint.(adc counts is x, dBm power is y) Take the values you get and input them into [Desmos](https://www.desmos.com/calculator) in a table, then run a quartic regression: y1 ~ a(log(x1))^4 + b(log(x1))^3 + c(log(x1))^2 + d·log(x1) + k. Take the a, b, c, d, and k values you obtain and input them in CAL_A through CAL_K in [Code/SRC/config.h](Code/SRC/config.h). This "calibration factor" will work for both power and SWR readings. 

There are two hard limits with this hardware. The bottom limit is set by the 1N5711 rectifier diodes. They need ~0.33 V to conduct, so below ~0.25 W, readings will be inaccurate, power levels under ~0.1 W will read as 0. The top end limit is set at an ABSOLUTE max of 28.7 W, this is due to the 2.5v adc reference and maximum of 4095 ADC counts.

If you want to test SWR, you can put loads of known resistance on the RF output to check the reported value. (Eg: 50Ω = 1:1 100Ω = 2:1, 150Ω = 3:1, etc)

![Calibration screen](Images/QRP%20Power%20and%20SWR%20Meter%20Screen%20Calibration.png)

## Assembly
Assembly is quite simple. All of the SMD parts are 0805 size or smaller so it is easy to hand solder without a hot plate. There are plenty of tutorials on how to do this online. Some tips that I can give are to use LOTS of flux. (Seriously, this makes life so much easier) Simply solder all of the parts onto the PCB. When you solder the connector to the OLED display, make sure to double check the pinout of your specific display and match it to the PCB silkscreen. Some of the displays may have different pinouts. 

There is also an [interactive BOM](Interactive%20BOM.html) which helps greatly with locating where parts go. Simply download the file to your computer, then simply opening the file will open up the interactive BOM in your web browser.

If you do decide to use the 3D printed case, simply put in the 8 heat set inserts on each side, and screw everything together, its pretty self explanatory. You may want to attach the battery to the top panel with some glue or double sided tape to prevent it from sliding/rattling around inside of the case.

## Programming

Programming is easy, all you need is a [UPDI programmer](https://www.adafruit.com/product/5879?srsltid=AfmBOoosIHZ5qCXL0qs-41c_Th3voxFLiswGlyJfjmZCHLlL7hfAid7V) and a computer. Simply plug the programmer into your computer, connect the programmer's 3 pins into the PCB, and use PlatformIO to flash the board. The MCU runs at 10MHz because the ATtiny is only rated for 20MHz with a VDD above 4.5 V.

For the user interface, there are two fonts available to pick from in the [Code/SRC/config.h](Code/SRC/config.h) file. You can change BIG_FONT between two font styles, choose whichever one suits your liking.

For REV 2 PCBs, the screen is software I2C because I totally missed the fact that the ATtiny supports hardware I2C and put them on the wrong pins. This works fine, but will likely be fixed in a future PCB revision.

```
cd Code
pio run            # build
pio run -t upload  # flash over UPDI
```

## Ordering PCBs
This design needs 1 or 2 pcbs depending on whether or not you build the 3D printed enclosure. You will need the Main PCB and the optional Connector Panel PCB. The gerber files found for these are in the [Gerbers Folder](Gerbers). I like to order from JLCPCB as they have good quality and are relatively cheap, I got 5 of each of these PCBs for under $20 shipped. The Main PCB is a 4 layer PCB and needs the JLC04161H-7628 stackup or similar. The connector panel is just a normal 2 layer PCB. Everything else can be the JLCPCB defaults. (You can change the solder mask color to whatever suits your preference)
## BOM
A [CSV](RF%20SWR%20and%20Power%20Meter%20BOM.csv) file is included with all of the parts, specs and links. One of these meters can be built for anywhere between $30-$50 each depending on where you live and what parts you have already and how many you decide to build.   

PCB parts:
| Reference | Qty | Value | Footprint | Recommended Part |
| :--- | :---: | :--- | :--- | :--- |
| C1, C2 | 2 | 10nF | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/kyocera-avx/KGM21NR71H103KT/563493) |
| C3, C4 | 2 | 4.7uF | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/tdk/C2012X7R1E475K125AB/2443450) |
| C5 | 1 | 0.1uF | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/kyocera-avx/KGM21NR71H104KT/563505) |
| D1, D2 | 2 | 1N5711 | SOD-123 | [DigiKey Link](https://www.digikey.com/en/products/detail/good-ark-semiconductor/GS1N5711W/18667513) |
| D3 | 1 | LED Red | 1206 | [DigiKey Link](https://www.digikey.com/en/products/detail/liteon/LTST-C150KRKT/386761) |
| IC1 | 1 | ATTINY3224 | SOIC-14 | [DigiKey Link](https://www.digikey.com/en/products/detail/microchip-technology/ATTINY3224-SSFR/17631056) |
| IC2 | 1 | MCP73831 | SOT-23-5 | [DigiKey Link](https://www.digikey.com/en/products/detail/microchip-technology/MCP73831T-2ACI-OT/964301) |
| J1, J2 | 2 | BNC Connector | Molex 0731385003 | [DigiKey Link](https://www.digikey.com/en/products/detail/molex/0731385003/3303300) |
| J3 | 1 | USBC Receptacle | GCT_USB4800-03-A | [DigiKey Link](https://www.digikey.com/en/products/detail/gct/USB4800-03-A/16688032) |
| J4 | 1 | OLED Connector | JST B4B-XH-A | [DigiKey Link](https://www.digikey.com/en/products/detail/jst-sales-america-inc/B4B-XH-A/1651047) |
| J5 | 1 | UPDI Connector | 3 Pin 2.54mm Header | [DigiKey Link](https://www.digikey.com/en/products/detail/sullins-connector-solutions/PPPC031LFBN-RC/810175) |
| J6 | 1 | Battery Connector | Molex 0530480210 | [DigiKey Link](https://www.digikey.com/en/products/detail/molex/0530480210/242864) |
| Q1 | 1 | SI2301 | SOT-23-3 | [DigiKey Link](https://www.digikey.com/en/products/detail/mcc-micro-commercial-components/SI2301-TP/1793242) |
| R1, R2, R5, R6 | 4 | Thin Film 100R 1% | 1206 | [DigiKey Link](https://www.digikey.com/en/products/detail/stackpole-electronics-inc/RNCP1206FTD100R/2240316) |
| R3, R4, R7, R8 | 4 | 10k 1% | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/vishay-dale/CRCW080510K0FKEA/1175751) |
| R9, R10 | 2 | 5.1k 1% | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/yageo/RC0805FR-075K1L/727988) |
| R11 | 1 | 1.5k | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/vishay-dale/CRCW08051K50FKEA/1175655) |
| R12 | 1 | 2k 1% | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/yageo/RC0805FR-072KL/727664) |
| R15 | 1 | 100k | 805 | [DigiKey Link](https://www.digikey.com/en/products/detail/stackpole-electronics-inc/RMCF0805FG100K/1712614) |
| SW1 | 1 | Button | TS11-674-43-BK-160-RA-D | [DigiKey Link](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/TS11-674-43-BK-160-RA-D/16562767) |
| SW2 | 1 | SW_SPDT | SLW-129956-4A-RA-N-D | [DigiKey Link](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/SLW-129956-4A-RA-N-D/24399223) |
| T1 | 1 | BN-43-202 | BN-43-202 | [Kits and Parts Link](https://toroids.info/BN-43-202.php) |

Digikey cart link: https://www.digikey.com/short/31wwbcbf  
(Certain SMD parts have more than you need, usually because buying 10 is cheaper than buying the required amount)

<br>

Other Parts:
| Part | Quantity | Link |
| :--- | :---: | :--- |
| 0.96in OLED | 1 | [Amazon Link](https://www.amazon.com/Display-SSD1306-Self-Luminous-Compatible-Raspberry/dp/B0GBWXTR1Z/ref=sr_1_13?) |
| Battery | 1 | [Amazon Link](https://www.amazon.com/dp/B0DPZVBKMY?) |
| M2.5x5mm Button Head | 5 | [Amazon Link](https://www.amazon.com/XunLiu-Button-Socket-Screws-BlackNickel/dp/B0756WTM1D/ref=sr_1_6?) |
| M2x3mm | 4 | [Amazon Link](https://www.amazon.com/uxcell-100Pcs-Socket-Button-Machine/dp/B01N76IPXI/ref=sr_1_2?) |
| M2x5mm Socket Head | 8 | [Amazon Link](https://www.amazon.com/Socket-Screws-Stainless-Thread-Spanner/dp/B0GHYKFVLC/ref=sr_1_3?) |
| M2 Threaded Inserts | 8 | [Amazon Link](https://www.amazon.com/MECCANIXITY-Threaded-Printing-Electronic-M2x3-5x4mm/dp/B0F2FRSJQN/ref=sr_1_4?) |
