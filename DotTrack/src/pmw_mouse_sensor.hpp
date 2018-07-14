#include <Arduino.h>
#include <M5Stack.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>
#include "waldo.hpp"
#include "select.hpp"
#include "simulator.hpp"
#include "image.hpp"
//#include "muc_logo.hpp"
#include "tools.hpp"

#define SIMULATE_INPUT 0

#define DEBUG_LEVEL 0

#define EYES_DEMO 1

// ESP32 Pins (M5STACK)
//#define PIN0 0 // Motion
#define PIN17 17 // NCS
#define PIN18 18 // SCK
#define PIN19 19 // MISO
#define PIN23 23 // MOSI
// mapped to better pin names
//#define PIN_MOTION PIN0
#define PIN_NCS PIN17
#define PIN_SCLK PIN18
#define PIN_MISO PIN19
#define PIN_MOSI PIN23

// register addresses of pmw3360 (see p. 29)
#define REGISTER_PRODUCT_ID       0x00
#define REGISTER_REVISION_ID      0x01
#define REGISTER_MOTION           0x02
#define REGISTER_DELTA_X_L        0x03
#define REGISTER_DELTA_X_H        0x04
#define REGISTER_DELTA_Y_L        0x05
#define REGISTER_DELTA_Y_H        0x06
#define REGISTER_SQUAL            0x07
#define REGISTER_RAW_DATA_SUM     0x08
#define REGISTER_MAXIMUM_RAW_DATA 0x09
#define REGISTER_MINIMUM_RAW_DATA 0x0A
#define REGISTER_SHUTTER_LOWER    0x0B
#define REGISTER_SHUTTER_UPPER    0x0C
#define REGISTER_CONTROL          0x0D
#define REGISTER_CONFIG1          0x0F
#define REGISTER_CONFIG2          0x10
#define REGISTER_ANGLE_TUNE       0x11
#define REGISTER_FRAME_CAPTURE    0x12
#define REGISTER_SROM_ENABLE      0x13
#define REGISTER_RUN_DOWNSHIFT    0x14
#define REGISTER_REST1_RATE_LOWER 0x15
#define REGISTER_REST1_RATE_UPPER 0x16
#define REGISTER_REST1_DOWNSHIFT  0x17
#define REGISTER_REST2_RATE_LOWER 0x18
#define REGISTER_REST2_RATE_UPPER 0x19
#define REGISTER_REST2_DOWNSHIFT  0x1A
#define REGISTER_REST3_RATE_LOWER 0x1B
#define REGISTER_REST3_RATE_UPPER 0x1C
#define REGISTER_OBSERVATION      0x24
#define REGISTER_DATA_OUT_LOWER   0x25
#define REGISTER_DATA_OUT_UPPER   0x26
#define REGISTER_RAW_DATA_DUMP    0x29
#define REGISTER_SROM_ID          0x2A
#define REGISTER_MIN_SQ_RUN       0x2B
#define REGISTER_RAW_DATA_THRESHOLD 0x2C
#define REGISTER_CONFIG5          0x2F
#define REGISTER_POWER_UP_RESET   0x3A
#define REGISTER_SHUTDOWN         0x3B
#define REGISTER_INVERSE_PRODUCT_ID 0x3F
#define REGISTER_LIFTCUTOFF_TUNE3 0x41
#define REGISTER_ANGLE_SNAP       0x42
#define REGISTER_LIFTCUTOFF_TUNE1 0x4A
#define REGISTER_MOTION_BURST     0x50
#define REGISTER_LIFTCUTOFF_TUNE_TIMEOUT    0x58
#define REGISTER_LIFTCUTOFF_TUNE_MIN_LENGTH 0x5A
#define REGISTER_SROM_LOAD_BURST  0x62
#define REGISTER_LIFT_CONFIG      0x63
#define REGISTER_RAW_DATA_BURST   0x64
#define REGISTER_LIFT_CUTOFF_TUNE2  0x65

// Register bit masks (and states) (see datasheet p. 30-56)
// Motion Register (REGISTER_MOTION) (p. 30-31):
#define REG_MOTION_MOT_BIT 0x80 // MOT bit (0 = no motion; 1 = motion)
#define REG_MOTION_RDATA_1ST 0x10 // RData_1st bit
#define REG_MOTION_LIFT_STAT 0x08 // Lift_Stat bit
#define REG_MOTION_OP_MODE 0x06 // OP_Mode[1:0] bits
#define REG_MOTION_FRAME_RDATA_1ST 0x01 // FRAME_RData_1st bit
// Observation Register (REGISTER_OBSERVATION) (p. 46):
#define REG_OBS_SROM_RUN 0x40
// Config2 Register (REGISTER_CONFIG2) (p. 39)
#define REG_CONF2_REST_EN 0x20 // Rest_En bit (0 = rest mode disabled; 1 = rest mode enabled [default])
#define REG_CONF2_RPT_MOD 0x04 // Rpt_Mod bit (0 = X CPI == Y CPI; 1 = X and Y CPI can be configured independent from each other)


// write commands have a 1 as MSB, read commands have a 0
#define WRITE_MASK  0x80
#define READ_MASK   0x7F

// SPI transaction settings (see p. 14 Table 3)
#define F_SCLK 2000000

// SPI delay times in microseconds (p. 15/16 of datasheet)
#define T_SCLK_NCS_WRITE 35
#define T_SCLK_NCS_READ 1
#define T_NCS_SCLK 1
#define T_SWW 180
#define T_SWR 180
#define T_SRW 20
#define T_SRR T_SRW
#define T_SRAD 160
#define T_SRAD_MOTBR 35
#define T_BEXIT 1 // 500ns
#define T_LOAD 15

// Current SROM/firmware version (for REGISTER_SROM_ID read)
#define FIRMWARE_VERSION 0x04
#define SROM_ID_SUCCESS FIRMWARE_VERSION
#define SROM_ID_FAIL 0x00

// Minimum CPI
#define MIN_CPI 100
#define DEFAULT_CPI 5000
#define MAX_CPI 12000

// Frame capture data variables
#define W_IMG 36
#define H_IMG 36
#define IMG_SIZE 1296 // W_IMG * H_IMG --> 36 * 36 = 1296

// Lift off buffer length
#define LIFT_OFF_BUF_LEN 5

// M5Stack
// M5Stack Display: 320x240
#define W_DISP 320
#define H_DISP 240
// Resize factor for every image pixel to maximize display size
#define PIX_RSZ 6 // H_DISP / H_IMG --> 240 / 36 = 6
// Offsets to center image on the display
#define X_OFFSET 52 // (W_DISP - W_IMG * PIX_RSZ) / 2 --> (320-36*6) / 2 = 52
#define Y_OFFSET 12 // (H_DISP - H_IMG * PIX_RSZ) / 2 --> (240-36*6) / 2 = 12

const unsigned short firmwareLength = 4094;
const unsigned short rawDataLength = IMG_SIZE;
// Read motion burst mode
const unsigned short motBrLength = 12;

// true as soon as the startup sequence has been completed
bool initComplete = false;
// switch between frame capture and motion mode
bool frameCapture = false;
// if true prints motion burst data to display
bool printMotBrToDisplay = false;
// prevents apps being exited from liftOff state
bool preventAppExit = false;

// set true by the motion interrupt when new motion data is available, set false when motion data has been processed
bool hasMoved = false;
// false when on ground, true when lift off ground (according to Lift_Stat bit in Motion register)
bool liftOff = false;
// LO state from previous loop
bool prevLiftOff = false;
// cumulative lift off state (switches to true if liftOffBuffer is all true and reverse)
bool cumLiftOff = liftOff;
// values between  (according to OP_Mode bit in Motion register)
uint8_t opMode = 0;
// SROM_RUN value (Observation byte/register)
// true/1 = SROM running
// false/0 = SROM NOT running
bool sromRun = false;
// Rest_En value
// false = rest mode disabled
// true = rest mode enabled [default]
bool restEn = true;
// Rpt_Mod value
// false = X CPI == Y CPI [default]
// true = X and Y CPI can be configured independently
bool rptMod = false;
// CPI (counts per inch) value (-1 = disabled [see Rpt_Mod p. 39])
int16_t cpi = DEFAULT_CPI;
// X axis CPI (counts per inch) value (-1 = disabled [see Rpt_Mod p. 39])
int16_t cpiX = DEFAULT_CPI;
// Y axis CPI (counts per inch) value (-1 = disabled [see Rpt_Mod p. 39])
int16_t cpiY = DEFAULT_CPI;
// SQUAL value
uint8_t squal = 0;
// Number of surface features (calculated from SQUAL value)
uint16_t numFeatures = 0;
// Raw_Data_Sum value
uint8_t rawDataSum = 0;
// Average Raw Data (calculated from Raw_Data_Sum value)
uint8_t avgRawData = 0;
// Maximum_Raw_Data value
uint8_t maxRawData = 0;
// Minimum_Raw_Data value
uint8_t minRawData = 0;
// Shutter value
uint16_t shutter = 0;

int32_t absX = 0;
int32_t absY = 0;

uint8_t prevApp = 0;
uint8_t app = 0;

// INFO: volatile not needed when using polling (and not using interrupts)
// set to true when reading motion registers (motion & delta registers) was initialized (see datasheet p. 30)
volatile bool readingMotion = false;

// lift off buffer
uint8_t liftOffBuffer[LIFT_OFF_BUF_LEN];
// Raw data image data
uint8_t rawData[IMG_SIZE];
// Raw motion burst data
uint8_t rawMotBr[motBrLength];
// movement distance since last update, [0] = x, [1] = y, expected values: around -1 to 1 (but can be more extreme)
int16_t xyDelta[2];

void writeRegister(uint8_t address, uint8_t data);
uint8_t readRegister(uint8_t address);
void resetSPIPort();
void resetDevice();
void performSROMdownload();
void configureRegisters();
void readConfigRegisters();
void onMovement();
void captureRawImage(uint8_t* result, int resultLength);
void drawImageToDisplay();
void sendRawOverSerial();
void readMotionBurst(uint8_t* result, int resultLength);
void updateMotBrValues();
void drawMotBrToDisplay();
void sendMotBrOverSerial();
void findAppPosition();
void drawWelcomeScreen();
void evalLiftOffBuffer();

#define AP_SSID "DotTrack"
#define AP_PASS "blubblub"
#define COM_DELAY 1 // in ms

int32_t trackX = 0;
int32_t trackY = 0;
int16_t trackBearing = 0; // -1 if lift off
// Send / receive lift off state
bool trackLiftOff = false;

int32_t circleX = 0;
int32_t circleY = 0;
bool noEyeTrack = false;

// Length of packet buffer.
// For two 32-bit and one 8-bit values (4 bytes * 2 + 1 byte = 9 bytes).
const uint8_t packetBufLen = sizeof(int32_t) * 2 + sizeof(bool);
// Buffer to hold incoming/outgoing packet.
// Holds absX, absY (each int32_t) and liftOff (bool).
byte packetBuffer[packetBufLen];

void receiveDataUdp();
void sendDataUdp();
void calcBearing();
void printWiFiStatus();
void handleWiFiEvent(WiFiEvent_t event);

// Coords of left position
#define POS1_X 17716;
#define POS1_Y 0;
// Coords of center position
#define POS2_X 0;
#define POS2_Y 0;
// Coords of right position
#define POS3_X -17716;
#define POS3_Y 0;

// Server start position
#define START_POS_SERV_X POS3_X
#define START_POS_SERV_Y POS3_Y
// Client start position
#define START_POS_CLI_X POS1_X
#define START_POS_CLI_Y POS1_Y

#define EYE_SCLERA_COLOR WHITE
#define EYE_IRIS_COLOR BLACK
#define EYE_PUPIL_COLOR RED

#define EYE_SCLERA_RADIUS 120
#define EYE_IRIS_RADIUS 70
#define EYE_PUPIL_RADIUS 30

// Maximum CPI value to consider tracked object to be "near"
// Calculation: inches * cpi    or  (cm / 2.54) * cpi
// 12500 counts are 2.5in or 6.35cm (when the CPI is 5000 [default])
#define MAX_CPI_NEAR 12500

// Distance from tracked object
double trackDist = 0.0;

// Firmware "PMW3360DM_srom_0x04"
const uint8_t firmwareData[] = {
0x01, 0x04, 0x8e, 0x96, 0x6e, 0x77, 0x3e, 0xfe, 0x7e, 0x5f, 0x1d, 0xb8, 0xf2, 0x66, 0x4e, 
0xff, 0x5d, 0x19, 0xb0, 0xc2, 0x04, 0x69, 0x54, 0x2a, 0xd6, 0x2e, 0xbf, 0xdd, 0x19, 0xb0, 
0xc3, 0xe5, 0x29, 0xb1, 0xe0, 0x23, 0xa5, 0xa9, 0xb1, 0xc1, 0x00, 0x82, 0x67, 0x4c, 0x1a, 
0x97, 0x8d, 0x79, 0x51, 0x20, 0xc7, 0x06, 0x8e, 0x7c, 0x7c, 0x7a, 0x76, 0x4f, 0xfd, 0x59, 
0x30, 0xe2, 0x46, 0x0e, 0x9e, 0xbe, 0xdf, 0x1d, 0x99, 0x91, 0xa0, 0xa5, 0xa1, 0xa9, 0xd0, 
0x22, 0xc6, 0xef, 0x5c, 0x1b, 0x95, 0x89, 0x90, 0xa2, 0xa7, 0xcc, 0xfb, 0x55, 0x28, 0xb3, 
0xe4, 0x4a, 0xf7, 0x6c, 0x3b, 0xf4, 0x6a, 0x56, 0x2e, 0xde, 0x1f, 0x9d, 0xb8, 0xd3, 0x05, 
0x88, 0x92, 0xa6, 0xce, 0x1e, 0xbe, 0xdf, 0x1d, 0x99, 0xb0, 0xe2, 0x46, 0xef, 0x5c, 0x07, 
0x11, 0x5d, 0x98, 0x0b, 0x9d, 0x94, 0x97, 0xee, 0x4e, 0x45, 0x33, 0x6b, 0x44, 0xc7, 0x29, 
0x56, 0x27, 0x30, 0xc6, 0xa7, 0xd5, 0xf2, 0x56, 0xdf, 0xb4, 0x38, 0x62, 0xcb, 0xa0, 0xb6, 
0xe3, 0x0f, 0x84, 0x06, 0x24, 0x05, 0x65, 0x6f, 0x76, 0x89, 0xb5, 0x77, 0x41, 0x27, 0x82, 
0x66, 0x65, 0x82, 0xcc, 0xd5, 0xe6, 0x20, 0xd5, 0x27, 0x17, 0xc5, 0xf8, 0x03, 0x23, 0x7c, 
0x5f, 0x64, 0xa5, 0x1d, 0xc1, 0xd6, 0x36, 0xcb, 0x4c, 0xd4, 0xdb, 0x66, 0xd7, 0x8b, 0xb1, 
0x99, 0x7e, 0x6f, 0x4c, 0x36, 0x40, 0x06, 0xd6, 0xeb, 0xd7, 0xa2, 0xe4, 0xf4, 0x95, 0x51, 
0x5a, 0x54, 0x96, 0xd5, 0x53, 0x44, 0xd7, 0x8c, 0xe0, 0xb9, 0x40, 0x68, 0xd2, 0x18, 0xe9, 
0xdd, 0x9a, 0x23, 0x92, 0x48, 0xee, 0x7f, 0x43, 0xaf, 0xea, 0x77, 0x38, 0x84, 0x8c, 0x0a, 
0x72, 0xaf, 0x69, 0xf8, 0xdd, 0xf1, 0x24, 0x83, 0xa3, 0xf8, 0x4a, 0xbf, 0xf5, 0x94, 0x13, 
0xdb, 0xbb, 0xd8, 0xb4, 0xb3, 0xa0, 0xfb, 0x45, 0x50, 0x60, 0x30, 0x59, 0x12, 0x31, 0x71, 
0xa2, 0xd3, 0x13, 0xe7, 0xfa, 0xe7, 0xce, 0x0f, 0x63, 0x15, 0x0b, 0x6b, 0x94, 0xbb, 0x37, 
0x83, 0x26, 0x05, 0x9d, 0xfb, 0x46, 0x92, 0xfc, 0x0a, 0x15, 0xd1, 0x0d, 0x73, 0x92, 0xd6, 
0x8c, 0x1b, 0x8c, 0xb8, 0x55, 0x8a, 0xce, 0xbd, 0xfe, 0x8e, 0xfc, 0xed, 0x09, 0x12, 0x83, 
0x91, 0x82, 0x51, 0x31, 0x23, 0xfb, 0xb4, 0x0c, 0x76, 0xad, 0x7c, 0xd9, 0xb4, 0x4b, 0xb2, 
0x67, 0x14, 0x09, 0x9c, 0x7f, 0x0c, 0x18, 0xba, 0x3b, 0xd6, 0x8e, 0x14, 0x2a, 0xe4, 0x1b, 
0x52, 0x9f, 0x2b, 0x7d, 0xe1, 0xfb, 0x6a, 0x33, 0x02, 0xfa, 0xac, 0x5a, 0xf2, 0x3e, 0x88, 
0x7e, 0xae, 0xd1, 0xf3, 0x78, 0xe8, 0x05, 0xd1, 0xe3, 0xdc, 0x21, 0xf6, 0xe1, 0x9a, 0xbd, 
0x17, 0x0e, 0xd9, 0x46, 0x9b, 0x88, 0x03, 0xea, 0xf6, 0x66, 0xbe, 0x0e, 0x1b, 0x50, 0x49, 
0x96, 0x40, 0x97, 0xf1, 0xf1, 0xe4, 0x80, 0xa6, 0x6e, 0xe8, 0x77, 0x34, 0xbf, 0x29, 0x40, 
0x44, 0xc2, 0xff, 0x4e, 0x98, 0xd3, 0x9c, 0xa3, 0x32, 0x2b, 0x76, 0x51, 0x04, 0x09, 0xe7, 
0xa9, 0xd1, 0xa6, 0x32, 0xb1, 0x23, 0x53, 0xe2, 0x47, 0xab, 0xd6, 0xf5, 0x69, 0x5c, 0x3e, 
0x5f, 0xfa, 0xae, 0x45, 0x20, 0xe5, 0xd2, 0x44, 0xff, 0x39, 0x32, 0x6d, 0xfd, 0x27, 0x57, 
0x5c, 0xfd, 0xf0, 0xde, 0xc1, 0xb5, 0x99, 0xe5, 0xf5, 0x1c, 0x77, 0x01, 0x75, 0xc5, 0x6d, 
0x58, 0x92, 0xf2, 0xb2, 0x47, 0x00, 0x01, 0x26, 0x96, 0x7a, 0x30, 0xff, 0xb7, 0xf0, 0xef, 
0x77, 0xc1, 0x8a, 0x5d, 0xdc, 0xc0, 0xd1, 0x29, 0x30, 0x1e, 0x77, 0x38, 0x7a, 0x94, 0xf1, 
0xb8, 0x7a, 0x7e, 0xef, 0xa4, 0xd1, 0xac, 0x31, 0x4a, 0xf2, 0x5d, 0x64, 0x3d, 0xb2, 0xe2, 
0xf0, 0x08, 0x99, 0xfc, 0x70, 0xee, 0x24, 0xa7, 0x7e, 0xee, 0x1e, 0x20, 0x69, 0x7d, 0x44, 
0xbf, 0x87, 0x42, 0xdf, 0x88, 0x3b, 0x0c, 0xda, 0x42, 0xc9, 0x04, 0xf9, 0x45, 0x50, 0xfc, 
0x83, 0x8f, 0x11, 0x6a, 0x72, 0xbc, 0x99, 0x95, 0xf0, 0xac, 0x3d, 0xa7, 0x3b, 0xcd, 0x1c, 
0xe2, 0x88, 0x79, 0x37, 0x11, 0x5f, 0x39, 0x89, 0x95, 0x0a, 0x16, 0x84, 0x7a, 0xf6, 0x8a, 
0xa4, 0x28, 0xe4, 0xed, 0x83, 0x80, 0x3b, 0xb1, 0x23, 0xa5, 0x03, 0x10, 0xf4, 0x66, 0xea, 
0xbb, 0x0c, 0x0f, 0xc5, 0xec, 0x6c, 0x69, 0xc5, 0xd3, 0x24, 0xab, 0xd4, 0x2a, 0xb7, 0x99, 
0x88, 0x76, 0x08, 0xa0, 0xa8, 0x95, 0x7c, 0xd8, 0x38, 0x6d, 0xcd, 0x59, 0x02, 0x51, 0x4b, 
0xf1, 0xb5, 0x2b, 0x50, 0xe3, 0xb6, 0xbd, 0xd0, 0x72, 0xcf, 0x9e, 0xfd, 0x6e, 0xbb, 0x44, 
0xc8, 0x24, 0x8a, 0x77, 0x18, 0x8a, 0x13, 0x06, 0xef, 0x97, 0x7d, 0xfa, 0x81, 0xf0, 0x31, 
0xe6, 0xfa, 0x77, 0xed, 0x31, 0x06, 0x31, 0x5b, 0x54, 0x8a, 0x9f, 0x30, 0x68, 0xdb, 0xe2, 
0x40, 0xf8, 0x4e, 0x73, 0xfa, 0xab, 0x74, 0x8b, 0x10, 0x58, 0x13, 0xdc, 0xd2, 0xe6, 0x78, 
0xd1, 0x32, 0x2e, 0x8a, 0x9f, 0x2c, 0x58, 0x06, 0x48, 0x27, 0xc5, 0xa9, 0x5e, 0x81, 0x47, 
0x89, 0x46, 0x21, 0x91, 0x03, 0x70, 0xa4, 0x3e, 0x88, 0x9c, 0xda, 0x33, 0x0a, 0xce, 0xbc, 
0x8b, 0x8e, 0xcf, 0x9f, 0xd3, 0x71, 0x80, 0x43, 0xcf, 0x6b, 0xa9, 0x51, 0x83, 0x76, 0x30, 
0x82, 0xc5, 0x6a, 0x85, 0x39, 0x11, 0x50, 0x1a, 0x82, 0xdc, 0x1e, 0x1c, 0xd5, 0x7d, 0xa9, 
0x71, 0x99, 0x33, 0x47, 0x19, 0x97, 0xb3, 0x5a, 0xb1, 0xdf, 0xed, 0xa4, 0xf2, 0xe6, 0x26, 
0x84, 0xa2, 0x28, 0x9a, 0x9e, 0xdf, 0xa6, 0x6a, 0xf4, 0xd6, 0xfc, 0x2e, 0x5b, 0x9d, 0x1a, 
0x2a, 0x27, 0x68, 0xfb, 0xc1, 0x83, 0x21, 0x4b, 0x90, 0xe0, 0x36, 0xdd, 0x5b, 0x31, 0x42, 
0x55, 0xa0, 0x13, 0xf7, 0xd0, 0x89, 0x53, 0x71, 0x99, 0x57, 0x09, 0x29, 0xc5, 0xf3, 0x21, 
0xf8, 0x37, 0x2f, 0x40, 0xf3, 0xd4, 0xaf, 0x16, 0x08, 0x36, 0x02, 0xfc, 0x77, 0xc5, 0x8b, 
0x04, 0x90, 0x56, 0xb9, 0xc9, 0x67, 0x9a, 0x99, 0xe8, 0x00, 0xd3, 0x86, 0xff, 0x97, 0x2d, 
0x08, 0xe9, 0xb7, 0xb3, 0x91, 0xbc, 0xdf, 0x45, 0xc6, 0xed, 0x0f, 0x8c, 0x4c, 0x1e, 0xe6, 
0x5b, 0x6e, 0x38, 0x30, 0xe4, 0xaa, 0xe3, 0x95, 0xde, 0xb9, 0xe4, 0x9a, 0xf5, 0xb2, 0x55, 
0x9a, 0x87, 0x9b, 0xf6, 0x6a, 0xb2, 0xf2, 0x77, 0x9a, 0x31, 0xf4, 0x7a, 0x31, 0xd1, 0x1d, 
0x04, 0xc0, 0x7c, 0x32, 0xa2, 0x9e, 0x9a, 0xf5, 0x62, 0xf8, 0x27, 0x8d, 0xbf, 0x51, 0xff, 
0xd3, 0xdf, 0x64, 0x37, 0x3f, 0x2a, 0x6f, 0x76, 0x3a, 0x7d, 0x77, 0x06, 0x9e, 0x77, 0x7f, 
0x5e, 0xeb, 0x32, 0x51, 0xf9, 0x16, 0x66, 0x9a, 0x09, 0xf3, 0xb0, 0x08, 0xa4, 0x70, 0x96, 
0x46, 0x30, 0xff, 0xda, 0x4f, 0xe9, 0x1b, 0xed, 0x8d, 0xf8, 0x74, 0x1f, 0x31, 0x92, 0xb3, 
0x73, 0x17, 0x36, 0xdb, 0x91, 0x30, 0xd6, 0x88, 0x55, 0x6b, 0x34, 0x77, 0x87, 0x7a, 0xe7, 
0xee, 0x06, 0xc6, 0x1c, 0x8c, 0x19, 0x0c, 0x48, 0x46, 0x23, 0x5e, 0x9c, 0x07, 0x5c, 0xbf, 
0xb4, 0x7e, 0xd6, 0x4f, 0x74, 0x9c, 0xe2, 0xc5, 0x50, 0x8b, 0xc5, 0x8b, 0x15, 0x90, 0x60, 
0x62, 0x57, 0x29, 0xd0, 0x13, 0x43, 0xa1, 0x80, 0x88, 0x91, 0x00, 0x44, 0xc7, 0x4d, 0x19, 
0x86, 0xcc, 0x2f, 0x2a, 0x75, 0x5a, 0xfc, 0xeb, 0x97, 0x2a, 0x70, 0xe3, 0x78, 0xd8, 0x91, 
0xb0, 0x4f, 0x99, 0x07, 0xa3, 0x95, 0xea, 0x24, 0x21, 0xd5, 0xde, 0x51, 0x20, 0x93, 0x27, 
0x0a, 0x30, 0x73, 0xa8, 0xff, 0x8a, 0x97, 0xe9, 0xa7, 0x6a, 0x8e, 0x0d, 0xe8, 0xf0, 0xdf, 
0xec, 0xea, 0xb4, 0x6c, 0x1d, 0x39, 0x2a, 0x62, 0x2d, 0x3d, 0x5a, 0x8b, 0x65, 0xf8, 0x90, 
0x05, 0x2e, 0x7e, 0x91, 0x2c, 0x78, 0xef, 0x8e, 0x7a, 0xc1, 0x2f, 0xac, 0x78, 0xee, 0xaf, 
0x28, 0x45, 0x06, 0x4c, 0x26, 0xaf, 0x3b, 0xa2, 0xdb, 0xa3, 0x93, 0x06, 0xb5, 0x3c, 0xa5, 
0xd8, 0xee, 0x8f, 0xaf, 0x25, 0xcc, 0x3f, 0x85, 0x68, 0x48, 0xa9, 0x62, 0xcc, 0x97, 0x8f, 
0x7f, 0x2a, 0xea, 0xe0, 0x15, 0x0a, 0xad, 0x62, 0x07, 0xbd, 0x45, 0xf8, 0x41, 0xd8, 0x36, 
0xcb, 0x4c, 0xdb, 0x6e, 0xe6, 0x3a, 0xe7, 0xda, 0x15, 0xe9, 0x29, 0x1e, 0x12, 0x10, 0xa0, 
0x14, 0x2c, 0x0e, 0x3d, 0xf4, 0xbf, 0x39, 0x41, 0x92, 0x75, 0x0b, 0x25, 0x7b, 0xa3, 0xce, 
0x39, 0x9c, 0x15, 0x64, 0xc8, 0xfa, 0x3d, 0xef, 0x73, 0x27, 0xfe, 0x26, 0x2e, 0xce, 0xda, 
0x6e, 0xfd, 0x71, 0x8e, 0xdd, 0xfe, 0x76, 0xee, 0xdc, 0x12, 0x5c, 0x02, 0xc5, 0x3a, 0x4e, 
0x4e, 0x4f, 0xbf, 0xca, 0x40, 0x15, 0xc7, 0x6e, 0x8d, 0x41, 0xf1, 0x10, 0xe0, 0x4f, 0x7e, 
0x97, 0x7f, 0x1c, 0xae, 0x47, 0x8e, 0x6b, 0xb1, 0x25, 0x31, 0xb0, 0x73, 0xc7, 0x1b, 0x97, 
0x79, 0xf9, 0x80, 0xd3, 0x66, 0x22, 0x30, 0x07, 0x74, 0x1e, 0xe4, 0xd0, 0x80, 0x21, 0xd6, 
0xee, 0x6b, 0x6c, 0x4f, 0xbf, 0xf5, 0xb7, 0xd9, 0x09, 0x87, 0x2f, 0xa9, 0x14, 0xbe, 0x27, 
0xd9, 0x72, 0x50, 0x01, 0xd4, 0x13, 0x73, 0xa6, 0xa7, 0x51, 0x02, 0x75, 0x25, 0xe1, 0xb3, 
0x45, 0x34, 0x7d, 0xa8, 0x8e, 0xeb, 0xf3, 0x16, 0x49, 0xcb, 0x4f, 0x8c, 0xa1, 0xb9, 0x36, 
0x85, 0x39, 0x75, 0x5d, 0x08, 0x00, 0xae, 0xeb, 0xf6, 0xea, 0xd7, 0x13, 0x3a, 0x21, 0x5a, 
0x5f, 0x30, 0x84, 0x52, 0x26, 0x95, 0xc9, 0x14, 0xf2, 0x57, 0x55, 0x6b, 0xb1, 0x10, 0xc2, 
0xe1, 0xbd, 0x3b, 0x51, 0xc0, 0xb7, 0x55, 0x4c, 0x71, 0x12, 0x26, 0xc7, 0x0d, 0xf9, 0x51, 
0xa4, 0x38, 0x02, 0x05, 0x7f, 0xb8, 0xf1, 0x72, 0x4b, 0xbf, 0x71, 0x89, 0x14, 0xf3, 0x77, 
0x38, 0xd9, 0x71, 0x24, 0xf3, 0x00, 0x11, 0xa1, 0xd8, 0xd4, 0x69, 0x27, 0x08, 0x37, 0x35, 
0xc9, 0x11, 0x9d, 0x90, 0x1c, 0x0e, 0xe7, 0x1c, 0xff, 0x2d, 0x1e, 0xe8, 0x92, 0xe1, 0x18, 
0x10, 0x95, 0x7c, 0xe0, 0x80, 0xf4, 0x96, 0x43, 0x21, 0xf9, 0x75, 0x21, 0x64, 0x38, 0xdd, 
0x9f, 0x1e, 0x95, 0x16, 0xda, 0x56, 0x1d, 0x4f, 0x9a, 0x53, 0xb2, 0xe2, 0xe4, 0x18, 0xcb, 
0x6b, 0x1a, 0x65, 0xeb, 0x56, 0xc6, 0x3b, 0xe5, 0xfe, 0xd8, 0x26, 0x3f, 0x3a, 0x84, 0x59, 
0x72, 0x66, 0xa2, 0xf3, 0x75, 0xff, 0xfb, 0x60, 0xb3, 0x22, 0xad, 0x3f, 0x2d, 0x6b, 0xf9, 
0xeb, 0xea, 0x05, 0x7c, 0xd8, 0x8f, 0x6d, 0x2c, 0x98, 0x9e, 0x2b, 0x93, 0xf1, 0x5e, 0x46, 
0xf0, 0x87, 0x49, 0x29, 0x73, 0x68, 0xd7, 0x7f, 0xf9, 0xf0, 0xe5, 0x7d, 0xdb, 0x1d, 0x75, 
0x19, 0xf3, 0xc4, 0x58, 0x9b, 0x17, 0x88, 0xa8, 0x92, 0xe0, 0xbe, 0xbd, 0x8b, 0x1d, 0x8d, 
0x9f, 0x56, 0x76, 0xad, 0xaf, 0x29, 0xe2, 0xd9, 0xd5, 0x52, 0xf6, 0xb5, 0x56, 0x35, 0x57, 
0x3a, 0xc8, 0xe1, 0x56, 0x43, 0x19, 0x94, 0xd3, 0x04, 0x9b, 0x6d, 0x35, 0xd8, 0x0b, 0x5f, 
0x4d, 0x19, 0x8e, 0xec, 0xfa, 0x64, 0x91, 0x0a, 0x72, 0x20, 0x2b, 0xbc, 0x1a, 0x4a, 0xfe, 
0x8b, 0xfd, 0xbb, 0xed, 0x1b, 0x23, 0xea, 0xad, 0x72, 0x82, 0xa1, 0x29, 0x99, 0x71, 0xbd, 
0xf0, 0x95, 0xc1, 0x03, 0xdd, 0x7b, 0xc2, 0xb2, 0x3c, 0x28, 0x54, 0xd3, 0x68, 0xa4, 0x72, 
0xc8, 0x66, 0x96, 0xe0, 0xd1, 0xd8, 0x7f, 0xf8, 0xd1, 0x26, 0x2b, 0xf7, 0xad, 0xba, 0x55, 
0xca, 0x15, 0xb9, 0x32, 0xc3, 0xe5, 0x88, 0x97, 0x8e, 0x5c, 0xfb, 0x92, 0x25, 0x8b, 0xbf, 
0xa2, 0x45, 0x55, 0x7a, 0xa7, 0x6f, 0x8b, 0x57, 0x5b, 0xcf, 0x0e, 0xcb, 0x1d, 0xfb, 0x20, 
0x82, 0x77, 0xa8, 0x8c, 0xcc, 0x16, 0xce, 0x1d, 0xfa, 0xde, 0xcc, 0x0b, 0x62, 0xfe, 0xcc, 
0xe1, 0xb7, 0xf0, 0xc3, 0x81, 0x64, 0x73, 0x40, 0xa0, 0xc2, 0x4d, 0x89, 0x11, 0x75, 0x33, 
0x55, 0x33, 0x8d, 0xe8, 0x4a, 0xfd, 0xea, 0x6e, 0x30, 0x0b, 0xd7, 0x31, 0x2c, 0xde, 0x47, 
0xe3, 0xbf, 0xf8, 0x55, 0x42, 0xe2, 0x7f, 0x59, 0xe5, 0x17, 0xef, 0x99, 0x34, 0x69, 0x91, 
0xb1, 0x23, 0x8e, 0x20, 0x87, 0x2d, 0xa8, 0xfe, 0xd5, 0x8a, 0xf3, 0x84, 0x3a, 0xf0, 0x37, 
0xe4, 0x09, 0x00, 0x54, 0xee, 0x67, 0x49, 0x93, 0xe4, 0x81, 0x70, 0xe3, 0x90, 0x4d, 0xef, 
0xfe, 0x41, 0xb7, 0x99, 0x7b, 0xc1, 0x83, 0xba, 0x62, 0x12, 0x6f, 0x7d, 0xde, 0x6b, 0xaf, 
0xda, 0x16, 0xf9, 0x55, 0x51, 0xee, 0xa6, 0x0c, 0x2b, 0x02, 0xa3, 0xfd, 0x8d, 0xfb, 0x30, 
0x17, 0xe4, 0x6f, 0xdf, 0x36, 0x71, 0xc4, 0xca, 0x87, 0x25, 0x48, 0xb0, 0x47, 0xec, 0xea, 
0xb4, 0xbf, 0xa5, 0x4d, 0x9b, 0x9f, 0x02, 0x93, 0xc4, 0xe3, 0xe4, 0xe8, 0x42, 0x2d, 0x68, 
0x81, 0x15, 0x0a, 0xeb, 0x84, 0x5b, 0xd6, 0xa8, 0x74, 0xfb, 0x7d, 0x1d, 0xcb, 0x2c, 0xda, 
0x46, 0x2a, 0x76, 0x62, 0xce, 0xbc, 0x5c, 0x9e, 0x8b, 0xe7, 0xcf, 0xbe, 0x78, 0xf5, 0x7c, 
0xeb, 0xb3, 0x3a, 0x9c, 0xaa, 0x6f, 0xcc, 0x72, 0xd1, 0x59, 0xf2, 0x11, 0x23, 0xd6, 0x3f, 
0x48, 0xd1, 0xb7, 0xce, 0xb0, 0xbf, 0xcb, 0xea, 0x80, 0xde, 0x57, 0xd4, 0x5e, 0x97, 0x2f, 
0x75, 0xd1, 0x50, 0x8e, 0x80, 0x2c, 0x66, 0x79, 0xbf, 0x72, 0x4b, 0xbd, 0x8a, 0x81, 0x6c, 
0xd3, 0xe1, 0x01, 0xdc, 0xd2, 0x15, 0x26, 0xc5, 0x36, 0xda, 0x2c, 0x1a, 0xc0, 0x27, 0x94, 
0xed, 0xb7, 0x9b, 0x85, 0x0b, 0x5e, 0x80, 0x97, 0xc5, 0xec, 0x4f, 0xec, 0x88, 0x5d, 0x50, 
0x07, 0x35, 0x47, 0xdc, 0x0b, 0x3b, 0x3d, 0xdd, 0x60, 0xaf, 0xa8, 0x5d, 0x81, 0x38, 0x24, 
0x25, 0x5d, 0x5c, 0x15, 0xd1, 0xde, 0xb3, 0xab, 0xec, 0x05, 0x69, 0xef, 0x83, 0xed, 0x57, 
0x54, 0xb8, 0x64, 0x64, 0x11, 0x16, 0x32, 0x69, 0xda, 0x9f, 0x2d, 0x7f, 0x36, 0xbb, 0x44, 
0x5a, 0x34, 0xe8, 0x7f, 0xbf, 0x03, 0xeb, 0x00, 0x7f, 0x59, 0x68, 0x22, 0x79, 0xcf, 0x73, 
0x6c, 0x2c, 0x29, 0xa7, 0xa1, 0x5f, 0x38, 0xa1, 0x1d, 0xf0, 0x20, 0x53, 0xe0, 0x1a, 0x63, 
0x14, 0x58, 0x71, 0x10, 0xaa, 0x08, 0x0c, 0x3e, 0x16, 0x1a, 0x60, 0x22, 0x82, 0x7f, 0xba, 
0xa4, 0x43, 0xa0, 0xd0, 0xac, 0x1b, 0xd5, 0x6b, 0x64, 0xb5, 0x14, 0x93, 0x31, 0x9e, 0x53, 
0x50, 0xd0, 0x57, 0x66, 0xee, 0x5a, 0x4f, 0xfb, 0x03, 0x2a, 0x69, 0x58, 0x76, 0xf1, 0x83, 
0xf7, 0x4e, 0xba, 0x8c, 0x42, 0x06, 0x60, 0x5d, 0x6d, 0xce, 0x60, 0x88, 0xae, 0xa4, 0xc3, 
0xf1, 0x03, 0xa5, 0x4b, 0x98, 0xa1, 0xff, 0x67, 0xe1, 0xac, 0xa2, 0xb8, 0x62, 0xd7, 0x6f, 
0xa0, 0x31, 0xb4, 0xd2, 0x77, 0xaf, 0x21, 0x10, 0x06, 0xc6, 0x9a, 0xff, 0x1d, 0x09, 0x17, 
0x0e, 0x5f, 0xf1, 0xaa, 0x54, 0x34, 0x4b, 0x45, 0x8a, 0x87, 0x63, 0xa6, 0xdc, 0xf9, 0x24, 
0x30, 0x67, 0xc6, 0xb2, 0xd6, 0x61, 0x33, 0x69, 0xee, 0x50, 0x61, 0x57, 0x28, 0xe7, 0x7e, 
0xee, 0xec, 0x3a, 0x5a, 0x73, 0x4e, 0xa8, 0x8d, 0xe4, 0x18, 0xea, 0xec, 0x41, 0x64, 0xc8, 
0xe2, 0xe8, 0x66, 0xb6, 0x2d, 0xb6, 0xfb, 0x6a, 0x6c, 0x16, 0xb3, 0xdd, 0x46, 0x43, 0xb9, 
0x73, 0x00, 0x6a, 0x71, 0xed, 0x4e, 0x9d, 0x25, 0x1a, 0xc3, 0x3c, 0x4a, 0x95, 0x15, 0x99, 
0x35, 0x81, 0x14, 0x02, 0xd6, 0x98, 0x9b, 0xec, 0xd8, 0x23, 0x3b, 0x84, 0x29, 0xaf, 0x0c, 
0x99, 0x83, 0xa6, 0x9a, 0x34, 0x4f, 0xfa, 0xe8, 0xd0, 0x3c, 0x4b, 0xd0, 0xfb, 0xb6, 0x68, 
0xb8, 0x9e, 0x8f, 0xcd, 0xf7, 0x60, 0x2d, 0x7a, 0x22, 0xe5, 0x7d, 0xab, 0x65, 0x1b, 0x95, 
0xa7, 0xa8, 0x7f, 0xb6, 0x77, 0x47, 0x7b, 0x5f, 0x8b, 0x12, 0x72, 0xd0, 0xd4, 0x91, 0xef, 
0xde, 0x19, 0x50, 0x3c, 0xa7, 0x8b, 0xc4, 0xa9, 0xb3, 0x23, 0xcb, 0x76, 0xe6, 0x81, 0xf0, 
0xc1, 0x04, 0x8f, 0xa3, 0xb8, 0x54, 0x5b, 0x97, 0xac, 0x19, 0xff, 0x3f, 0x55, 0x27, 0x2f, 
0xe0, 0x1d, 0x42, 0x9b, 0x57, 0xfc, 0x4b, 0x4e, 0x0f, 0xce, 0x98, 0xa9, 0x43, 0x57, 0x03, 
0xbd, 0xe7, 0xc8, 0x94, 0xdf, 0x6e, 0x36, 0x73, 0x32, 0xb4, 0xef, 0x2e, 0x85, 0x7a, 0x6e, 
0xfc, 0x6c, 0x18, 0x82, 0x75, 0x35, 0x90, 0x07, 0xf3, 0xe4, 0x9f, 0x3e, 0xdc, 0x68, 0xf3, 
0xb5, 0xf3, 0x19, 0x80, 0x92, 0x06, 0x99, 0xa2, 0xe8, 0x6f, 0xff, 0x2e, 0x7f, 0xae, 0x42, 
0xa4, 0x5f, 0xfb, 0xd4, 0x0e, 0x81, 0x2b, 0xc3, 0x04, 0xff, 0x2b, 0xb3, 0x74, 0x4e, 0x36, 
0x5b, 0x9c, 0x15, 0x00, 0xc6, 0x47, 0x2b, 0xe8, 0x8b, 0x3d, 0xf1, 0x9c, 0x03, 0x9a, 0x58, 
0x7f, 0x9b, 0x9c, 0xbf, 0x85, 0x49, 0x79, 0x35, 0x2e, 0x56, 0x7b, 0x41, 0x14, 0x39, 0x47, 
0x83, 0x26, 0xaa, 0x07, 0x89, 0x98, 0x11, 0x1b, 0x86, 0xe7, 0x73, 0x7a, 0xd8, 0x7d, 0x78, 
0x61, 0x53, 0xe9, 0x79, 0xf5, 0x36, 0x8d, 0x44, 0x92, 0x84, 0xf9, 0x13, 0x50, 0x58, 0x3b, 
0xa4, 0x6a, 0x36, 0x65, 0x49, 0x8e, 0x3c, 0x0e, 0xf1, 0x6f, 0xd2, 0x84, 0xc4, 0x7e, 0x8e, 
0x3f, 0x39, 0xae, 0x7c, 0x84, 0xf1, 0x63, 0x37, 0x8e, 0x3c, 0xcc, 0x3e, 0x44, 0x81, 0x45, 
0xf1, 0x4b, 0xb9, 0xed, 0x6b, 0x36, 0x5d, 0xbb, 0x20, 0x60, 0x1a, 0x0f, 0xa3, 0xaa, 0x55, 
0x77, 0x3a, 0xa9, 0xae, 0x37, 0x4d, 0xba, 0xb8, 0x86, 0x6b, 0xbc, 0x08, 0x50, 0xf6, 0xcc, 
0xa4, 0xbd, 0x1d, 0x40, 0x72, 0xa5, 0x86, 0xfa, 0xe2, 0x10, 0xae, 0x3d, 0x58, 0x4b, 0x97, 
0xf3, 0x43, 0x74, 0xa9, 0x9e, 0xeb, 0x21, 0xb7, 0x01, 0xa4, 0x86, 0x93, 0x97, 0xee, 0x2f, 
0x4f, 0x3b, 0x86, 0xa1, 0x41, 0x6f, 0x41, 0x26, 0x90, 0x78, 0x5c, 0x7f, 0x30, 0x38, 0x4b, 
0x3f, 0xaa, 0xec, 0xed, 0x5c, 0x6f, 0x0e, 0xad, 0x43, 0x87, 0xfd, 0x93, 0x35, 0xe6, 0x01, 
0xef, 0x41, 0x26, 0x90, 0x99, 0x9e, 0xfb, 0x19, 0x5b, 0xad, 0xd2, 0x91, 0x8a, 0xe0, 0x46, 
0xaf, 0x65, 0xfa, 0x4f, 0x84, 0xc1, 0xa1, 0x2d, 0xcf, 0x45, 0x8b, 0xd3, 0x85, 0x50, 0x55, 
0x7c, 0xf9, 0x67, 0x88, 0xd4, 0x4e, 0xe9, 0xd7, 0x6b, 0x61, 0x54, 0xa1, 0xa4, 0xa6, 0xa2, 
0xc2, 0xbf, 0x30, 0x9c, 0x40, 0x9f, 0x5f, 0xd7, 0x69, 0x2b, 0x24, 0x82, 0x5e, 0xd9, 0xd6, 
0xa7, 0x12, 0x54, 0x1a, 0xf7, 0x55, 0x9f, 0x76, 0x50, 0xa9, 0x95, 0x84, 0xe6, 0x6b, 0x6d, 
0xb5, 0x96, 0x54, 0xd6, 0xcd, 0xb3, 0xa1, 0x9b, 0x46, 0xa7, 0x94, 0x4d, 0xc4, 0x94, 0xb4, 
0x98, 0xe3, 0xe1, 0xe2, 0x34, 0xd5, 0x33, 0x16, 0x07, 0x54, 0xcd, 0xb7, 0x77, 0x53, 0xdb, 
0x4f, 0x4d, 0x46, 0x9d, 0xe9, 0xd4, 0x9c, 0x8a, 0x36, 0xb6, 0xb8, 0x38, 0x26, 0x6c, 0x0e, 
0xff, 0x9c, 0x1b, 0x43, 0x8b, 0x80, 0xcc, 0xb9, 0x3d, 0xda, 0xc7, 0xf1, 0x8a, 0xf2, 0x6d, 
0xb8, 0xd7, 0x74, 0x2f, 0x7e, 0x1e, 0xb7, 0xd3, 0x4a, 0xb4, 0xac, 0xfc, 0x79, 0x48, 0x6c, 
0xbc, 0x96, 0xb6, 0x94, 0x46, 0x57, 0x2d, 0xb0, 0xa3, 0xfc, 0x1e, 0xb9, 0x52, 0x60, 0x85, 
0x2d, 0x41, 0xd0, 0x43, 0x01, 0x1e, 0x1c, 0xd5, 0x7d, 0xfc, 0xf3, 0x96, 0x0d, 0xc7, 0xcb, 
0x2a, 0x29, 0x9a, 0x93, 0xdd, 0x88, 0x2d, 0x37, 0x5d, 0xaa, 0xfb, 0x49, 0x68, 0xa0, 0x9c, 
0x50, 0x86, 0x7f, 0x68, 0x56, 0x57, 0xf9, 0x79, 0x18, 0x39, 0xd4, 0xe0, 0x01, 0x84, 0x33, 
0x61, 0xca, 0xa5, 0xd2, 0xd6, 0xe4, 0xc9, 0x8a, 0x4a, 0x23, 0x44, 0x4e, 0xbc, 0xf0, 0xdc, 
0x24, 0xa1, 0xa0, 0xc4, 0xe2, 0x07, 0x3c, 0x10, 0xc4, 0xb5, 0x25, 0x4b, 0x65, 0x63, 0xf4, 
0x80, 0xe7, 0xcf, 0x61, 0xb1, 0x71, 0x82, 0x21, 0x87, 0x2c, 0xf5, 0x91, 0x00, 0x32, 0x0c, 
0xec, 0xa9, 0xb5, 0x9a, 0x74, 0x85, 0xe3, 0x36, 0x8f, 0x76, 0x4f, 0x9c, 0x6d, 0xce, 0xbc, 
0xad, 0x0a, 0x4b, 0xed, 0x76, 0x04, 0xcb, 0xc3, 0xb9, 0x33, 0x9e, 0x01, 0x93, 0x96, 0x69, 
0x7d, 0xc5, 0xa2, 0x45, 0x79, 0x9b, 0x04, 0x5c, 0x84, 0x09, 0xed, 0x88, 0x43, 0xc7, 0xab, 
0x93, 0x14, 0x26, 0xa1, 0x40, 0xb5, 0xce, 0x4e, 0xbf, 0x2a, 0x42, 0x85, 0x3e, 0x2c, 0x3b, 
0x54, 0xe8, 0x12, 0x1f, 0x0e, 0x97, 0x59, 0xb2, 0x27, 0x89, 0xfa, 0xf2, 0xdf, 0x8e, 0x68, 
0x59, 0xdc, 0x06, 0xbc, 0xb6, 0x85, 0x0d, 0x06, 0x22, 0xec, 0xb1, 0xcb, 0xe5, 0x04, 0xe6, 
0x3d, 0xb3, 0xb0, 0x41, 0x73, 0x08, 0x3f, 0x3c, 0x58, 0x86, 0x63, 0xeb, 0x50, 0xee, 0x1d, 
0x2c, 0x37, 0x74, 0xa9, 0xd3, 0x18, 0xa3, 0x47, 0x6e, 0x93, 0x54, 0xad, 0x0a, 0x5d, 0xb8, 
0x2a, 0x55, 0x5d, 0x78, 0xf6, 0xee, 0xbe, 0x8e, 0x3c, 0x76, 0x69, 0xb9, 0x40, 0xc2, 0x34, 
0xec, 0x2a, 0xb9, 0xed, 0x7e, 0x20, 0xe4, 0x8d, 0x00, 0x38, 0xc7, 0xe6, 0x8f, 0x44, 0xa8, 
0x86, 0xce, 0xeb, 0x2a, 0xe9, 0x90, 0xf1, 0x4c, 0xdf, 0x32, 0xfb, 0x73, 0x1b, 0x6d, 0x92, 
0x1e, 0x95, 0xfe, 0xb4, 0xdb, 0x65, 0xdf, 0x4d, 0x23, 0x54, 0x89, 0x48, 0xbf, 0x4a, 0x2e, 
0x70, 0xd6, 0xd7, 0x62, 0xb4, 0x33, 0x29, 0xb1, 0x3a, 0x33, 0x4c, 0x23, 0x6d, 0xa6, 0x76, 
0xa5, 0x21, 0x63, 0x48, 0xe6, 0x90, 0x5d, 0xed, 0x90, 0x95, 0x0b, 0x7a, 0x84, 0xbe, 0xb8, 
0x0d, 0x5e, 0x63, 0x0c, 0x62, 0x26, 0x4c, 0x14, 0x5a, 0xb3, 0xac, 0x23, 0xa4, 0x74, 0xa7, 
0x6f, 0x33, 0x30, 0x05, 0x60, 0x01, 0x42, 0xa0, 0x28, 0xb7, 0xee, 0x19, 0x38, 0xf1, 0x64, 
0x80, 0x82, 0x43, 0xe1, 0x41, 0x27, 0x1f, 0x1f, 0x90, 0x54, 0x7a, 0xd5, 0x23, 0x2e, 0xd1, 
0x3d, 0xcb, 0x28, 0xba, 0x58, 0x7f, 0xdc, 0x7c, 0x91, 0x24, 0xe9, 0x28, 0x51, 0x83, 0x6e, 
0xc5, 0x56, 0x21, 0x42, 0xed, 0xa0, 0x56, 0x22, 0xa1, 0x40, 0x80, 0x6b, 0xa8, 0xf7, 0x94, 
0xca, 0x13, 0x6b, 0x0c, 0x39, 0xd9, 0xfd, 0xe9, 0xf3, 0x6f, 0xa6, 0x9e, 0xfc, 0x70, 0x8a, 
0xb3, 0xbc, 0x59, 0x3c, 0x1e, 0x1d, 0x6c, 0xf9, 0x7c, 0xaf, 0xf9, 0x88, 0x71, 0x95, 0xeb, 
0x57, 0x00, 0xbd, 0x9f, 0x8c, 0x4f, 0xe1, 0x24, 0x83, 0xc5, 0x22, 0xea, 0xfd, 0xd3, 0x0c, 
0xe2, 0x17, 0x18, 0x7c, 0x6a, 0x4c, 0xde, 0x77, 0xb4, 0x53, 0x9b, 0x4c, 0x81, 0xcd, 0x23, 
0x60, 0xaa, 0x0e, 0x25, 0x73, 0x9c, 0x02, 0x79, 0x32, 0x30, 0xdf, 0x74, 0xdf, 0x75, 0x19, 
0xf4, 0xa5, 0x14, 0x5c, 0xf7, 0x7a, 0xa8, 0xa5, 0x91, 0x84, 0x7c, 0x60, 0x03, 0x06, 0x3b, 
0xcd, 0x50, 0xb6, 0x27, 0x9c, 0xfe, 0xb1, 0xdd, 0xcc, 0xd3, 0xb0, 0x59, 0x24, 0xb2, 0xca, 
0xe2, 0x1c, 0x81, 0x22, 0x9d, 0x07, 0x8f, 0x8e, 0xb9, 0xbe, 0x4e, 0xfa, 0xfc, 0x39, 0x65, 
0xba, 0xbf, 0x9d, 0x12, 0x37, 0x5e, 0x97, 0x7e, 0xf3, 0x89, 0xf5, 0x5d, 0xf5, 0xe3, 0x09, 
0x8c, 0x62, 0xb5, 0x20, 0x9d, 0x0c, 0x53, 0x8a, 0x68, 0x1b, 0xd2, 0x8f, 0x75, 0x17, 0x5d, 
0xd4, 0xe5, 0xda, 0x75, 0x62, 0x19, 0x14, 0x6a, 0x26, 0x2d, 0xeb, 0xf8, 0xaf, 0x37, 0xf0, 
0x6c, 0xa4, 0x55, 0xb1, 0xbc, 0xe2, 0x33, 0xc0, 0x9a, 0xca, 0xb0, 0x11, 0x49, 0x4f, 0x68, 
0x9b, 0x3b, 0x6b, 0x3c, 0xcc, 0x13, 0xf6, 0xc7, 0x85, 0x61, 0x68, 0x42, 0xae, 0xbb, 0xdd, 
0xcd, 0x45, 0x16, 0x29, 0x1d, 0xea, 0xdb, 0xc8, 0x03, 0x94, 0x3c, 0xee, 0x4f, 0x82, 0x11, 
0xc3, 0xec, 0x28, 0xbd, 0x97, 0x05, 0x99, 0xde, 0xd7, 0xbb, 0x5e, 0x22, 0x1f, 0xd4, 0xeb, 
0x64, 0xd9, 0x92, 0xd9, 0x85, 0xb7, 0x6a, 0x05, 0x6a, 0xe4, 0x24, 0x41, 0xf1, 0xcd, 0xf0, 
0xd8, 0x3f, 0xf8, 0x9e, 0x0e, 0xcd, 0x0b, 0x7a, 0x70, 0x6b, 0x5a, 0x75, 0x0a, 0x6a, 0x33, 
0x88, 0xec, 0x17, 0x75, 0x08, 0x70, 0x10, 0x2f, 0x24, 0xcf, 0xc4, 0xe9, 0x42, 0x00, 0x61, 
0x94, 0xca, 0x1f, 0x3a, 0x76, 0x06, 0xfa, 0xd2, 0x48, 0x81, 0xf0, 0x77, 0x60, 0x03, 0x45, 
0xd9, 0x61, 0xf4, 0xa4, 0x6f, 0x3d, 0xd9, 0x30, 0xc3, 0x04, 0x6b, 0x54, 0x2a, 0xb7, 0xec, 
0x3b, 0xf4, 0x4b, 0xf5, 0x68, 0x52, 0x26, 0xce, 0xff, 0x5d, 0x19, 0x91, 0xa0, 0xa3, 0xa5, 
0xa9, 0xb1, 0xe0, 0x23, 0xc4, 0x0a, 0x77, 0x4d, 0xf9, 0x51, 0x20, 0xa3, 0xa5, 0xa9, 0xb1, 
0xc1, 0x00, 0x82, 0x86, 0x8e, 0x7f, 0x5d, 0x19, 0x91, 0xa0, 0xa3, 0xc4, 0xeb, 0x54, 0x0b, 
0x75, 0x68, 0x52, 0x07, 0x8c, 0x9a, 0x97, 0x8d, 0x79, 0x70, 0x62, 0x46, 0xef, 0x5c, 0x1b, 
0x95, 0x89, 0x71, 0x41, 0xe1, 0x21, 0xa1, 0xa1, 0xa1, 0xc0, 0x02, 0x67, 0x4c, 0x1a, 0xb6, 
0xcf, 0xfd, 0x78, 0x53, 0x24, 0xab, 0xb5, 0xc9, 0xf1, 0x60, 0x23, 0xa5, 0xc8, 0x12, 0x87, 
0x6d, 0x58, 0x13, 0x85, 0x88, 0x92, 0x87, 0x6d, 0x58, 0x32, 0xc7, 0x0c, 0x9a, 0x97, 0xac, 
0xda, 0x36, 0xee, 0x5e, 0x3e, 0xdf, 0x1d, 0xb8, 0xf2, 0x66, 0x2f, 0xbd, 0xf8, 0x72, 0x47, 
0xed, 0x58, 0x13, 0x85, 0x88, 0x92, 0x87, 0x8c, 0x7b, 0x55, 0x09, 0x90, 0xa2, 0xc6, 0xef, 
0x3d, 0xf8, 0x53, 0x24, 0xab, 0xd4, 0x2a, 0xb7, 0xec, 0x5a, 0x36, 0xee, 0x5e, 0x3e, 0xdf, 
0x3c, 0xfa, 0x76, 0x4f, 0xfd, 0x59, 0x30, 0xe2, 0x46, 0xef, 0x3d, 0xf8, 0x53, 0x05, 0x69, 
0x31, 0xc1, 0x00, 0x82, 0x86, 0x8e, 0x7f, 0x5d, 0x19, 0xb0, 0xe2, 0x27, 0xcc, 0xfb, 0x74, 
0x4b, 0x14, 0x8b, 0x94, 0x8b, 0x75, 0x68, 0x33, 0xc5, 0x08, 0x92, 0x87, 0x8c, 0x9a, 0xb6, 
0xcf, 0x1c, 0xba, 0xd7, 0x0d, 0x98, 0xb2, 0xe6, 0x2f, 0xdc, 0x1b, 0x95, 0x89, 0x71, 0x60, 
0x23, 0xc4, 0x0a, 0x96, 0x8f, 0x9c, 0xba, 0xf6, 0x6e, 0x3f, 0xfc, 0x5b, 0x15, 0xa8, 0xd2, 
0x26, 0xaf, 0xbd, 0xf8, 0x72, 0x66, 0x2f, 0xdc, 0x1b, 0xb4, 0xcb, 0x14, 0x8b, 0x94, 0xaa, 
0xb7, 0xcd, 0xf9, 0x51, 0x01, 0x80, 0x82, 0x86, 0x6f, 0x3d, 0xd9, 0x30, 0xe2, 0x27, 0xcc, 
0xfb, 0x74, 0x4b, 0x14, 0xaa, 0xb7, 0xcd, 0xf9, 0x70, 0x43, 0x04, 0x6b, 0x35, 0xc9, 0xf1, 
0x60, 0x23, 0xa5, 0xc8, 0xf3, 0x45, 0x08, 0x92, 0x87, 0x6d, 0x58, 0x32, 0xe6, 0x2f, 0xbd, 
0xf8, 0x72, 0x66, 0x4e, 0x1e, 0xbe, 0xfe, 0x7e, 0x7e, 0x7e, 0x5f, 0x1d, 0x99, 0x91, 0xa0, 
0xa3, 0xc4, 0x0a, 0x77, 0x4d, 0x18, 0x93, 0xa4, 0xab, 0xd4, 0x0b, 0x75, 0x49, 0x10, 0xa2, 
0xc6, 0xef, 0x3d, 0xf8, 0x53, 0x24, 0xab, 0xb5, 0xe8, 0x33, 0xe4, 0x4a, 0x16, 0xae, 0xde, 
0x1f, 0xbc, 0xdb, 0x15, 0xa8, 0xb3, 0xc5, 0x08, 0x73, 0x45, 0xe9, 0x31, 0xc1, 0xe1, 0x21, 
0xa1, 0xa1, 0xa1, 0xc0, 0x02, 0x86, 0x6f, 0x5c, 0x3a, 0xd7, 0x0d, 0x98, 0x93, 0xa4, 0xca, 
0x16, 0xae, 0xde, 0x1f, 0x9d, 0x99, 0xb0, 0xe2, 0x46, 0xef, 0x3d, 0xf8, 0x72, 0x47, 0x0c, 
0x9a, 0xb6, 0xcf, 0xfd, 0x59, 0x11, 0xa0, 0xa3, 0xa5, 0xc8, 0xf3, 0x45, 0x08, 0x92, 0x87, 
0x6d, 0x39, 0xf0, 0x43, 0x04, 0x8a, 0x96, 0xae, 0xde, 0x3e, 0xdf, 0x1d, 0x99, 0x91, 0xa0, 
0xc2, 0x06, 0x6f, 0x3d, 0xf8, 0x72, 0x47, 0x0c, 0x9a, 0x97, 0x8d, 0x98, 0x93, 0x85, 0x88, 
0x73, 0x45, 0xe9, 0x31, 0xe0, 0x23, 0xa5, 0xa9, 0xd0, 0x03, 0x84, 0x8a, 0x96, 0xae, 0xde, 
0x1f, 0xbc, 0xdb, 0x15, 0xa8, 0xd2, 0x26, 0xce, 0xff, 0x5d, 0x19, 0x91, 0x81, 0x80, 0x82, 
0x67, 0x2d, 0xd8, 0x13, 0xa4, 0xab, 0xd4, 0x0b, 0x94, 0xaa, 0xb7, 0xcd, 0xf9, 0x51, 0x20, 
0xa3, 0xa5, 0xc8, 0xf3, 0x45, 0xe9, 0x50, 0x22, 0xc6, 0xef, 0x5c, 0x3a, 0xd7, 0x0d, 0x98, 
0x93, 0x85, 0x88, 0x73, 0x64, 0x4a, 0xf7, 0x4d, 0xf9, 0x51, 0x20, 0xa3, 0xc4, 0x0a, 0x96, 
0xae, 0xde, 0x3e, 0xfe, 0x7e, 0x7e, 0x7e, 0x5f, 0x3c, 0xfa, 0x76, 0x4f, 0xfd, 0x78, 0x72, 
0x66, 0x2f, 0xbd, 0xd9, 0x30, 0xc3, 0xe5, 0x48, 0x12, 0x87, 0x8c, 0x7b, 0x55, 0x28, 0xd2, 
0x07, 0x8c, 0x9a, 0x97, 0xac, 0xda, 0x17, 0x8d, 0x79, 0x51, 0x20, 0xa3, 0xc4, 0xeb, 0x54, 
0x0b, 0x94, 0x8b, 0x94, 0xaa, 0xd6, 0x2e, 0xbf, 0xfc, 0x5b, 0x15, 0xa8, 0xd2, 0x26, 0xaf, 
0xdc, 0x1b, 0xb4, 0xea, 0x37, 0xec, 0x3b, 0xf4, 0x6a, 0x37, 0xcd, 0x18, 0x93, 0x85, 0x69, 
0x31, 0xc1, 0xe1, 0x40, 0xe3, 0x25, 0xc8, 0x12, 0x87, 0x8c, 0x9a, 0xb6, 0xcf, 0xfd, 0x59, 
0x11, 0xa0, 0xc2, 0x06, 0x8e, 0x7f, 0x5d, 0x38, 0xf2, 0x47, 0x0c, 0x7b, 0x74, 0x6a, 0x37, 
0xec, 0x5a, 0x36, 0xee, 0x3f, 0xfc, 0x7a, 0x76, 0x4f, 0x1c, 0x9b, 0x95, 0x89, 0x71, 0x41, 
0x00, 0x63, 0x44, 0xeb, 0x54, 0x2a, 0xd6, 0x0f, 0x9c, 0xba, 0xd7, 0x0d, 0x98, 0x93, 0x85, 
0x69, 0x31, 0xc1, 0x00, 0x82, 0x86, 0x8e, 0x9e, 0xbe, 0xdf, 0x3c, 0xfa, 0x57, 0x2c, 0xda, 
0x36, 0xee, 0x3f, 0xfc, 0x5b, 0x15, 0x89, 0x71, 0x41, 0x00, 0x82, 0x86, 0x8e, 0x7f, 0x5d, 
0x38, 0xf2, 0x47, 0xed, 0x58, 0x13, 0xa4, 0xca, 0xf7, 0x4d, 0xf9, 0x51, 0x01, 0x80, 0x63, 
0x44, 0xeb, 0x54, 0x2a, 0xd6, 0x2e, 0xbf, 0xdd, 0x19, 0x91, 0xa0, 0xa3, 0xa5, 0xa9, 0xb1, 
0xe0, 0x42, 0x06, 0x8e, 0x7f, 0x5d, 0x19, 0x91, 0xa0, 0xa3, 0xc4, 0x0a, 0x96, 0x8f, 0x7d, 
0x78, 0x72, 0x47, 0x0c, 0x7b, 0x74, 0x6a, 0x56, 0x2e, 0xde, 0x1f, 0xbc, 0xfa, 0x57, 0x0d, 
0x79, 0x51, 0x01, 0x61, 0x21, 0xa1, 0xc0, 0xe3, 0x25, 0xa9, 0xb1, 0xc1, 0xe1, 0x40, 0x02, 
0x67, 0x4c, 0x1a, 0x97, 0x8d, 0x98, 0x93, 0xa4, 0xab, 0xd4, 0x2a, 0xd6, 0x0f, 0x9c, 0x9b, 
0xb4, 0xcb, 0x14, 0xaa, 0xb7, 0xcd, 0xf9, 0x51, 0x20, 0xa3, 0xc4, 0xeb, 0x35, 0xc9, 0xf1, 
0x60, 0x42, 0x06, 0x8e, 0x7f, 0x7c, 0x7a, 0x76, 0x6e, 0x3f, 0xfc, 0x7a, 0x76, 0x6e, 0x5e, 
0x3e, 0xfe, 0x7e, 0x5f, 0x3c, 0xdb, 0x15, 0x89, 0x71, 0x41, 0xe1, 0x21, 0xc0, 0xe3, 0x44, 
0xeb, 0x54, 0x2a, 0xb7, 0xcd, 0xf9, 0x70, 0x62, 0x27, 0xad, 0xd8, 0x32, 0xc7, 0x0c, 0x7b, 
0x74, 0x4b, 0x14, 0xaa, 0xb7, 0xec, 0x3b, 0xd5, 0x28, 0xd2, 0x07, 0x6d, 0x39, 0xd1, 0x20, 
0xc2, 0xe7, 0x4c, 0x1a, 0x97, 0x8d, 0x98, 0xb2, 0xc7, 0x0c, 0x59, 0x28, 0xf3, 0x9b };
