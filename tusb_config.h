/*
 * TinyUSB Configuration for ESP32-S3
 * USB HID Keyboard Device Configuration
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by board.mk
#ifndef CFG_TUSB_MCU
  #define CFG_TUSB_MCU OPT_MCU_ESP32S3
#endif

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_DEVICE_RHPORT_NUM
  #define BOARD_DEVICE_RHPORT_NUM     0
#endif

// RHPort max operational speed can be defined by board.mk
// Default to Highspeed for MCU with internal HighSpeed PHY (can be port specific), otherwise FullSpeed
#ifndef BOARD_DEVICE_RHPORT_SPEED
  #if (CFG_TUSB_MCU == OPT_MCU_ESP32S2 || CFG_TUSB_MCU == OPT_MCU_ESP32S3)
    #define BOARD_DEVICE_RHPORT_SPEED   OPT_MODE_HIGH_SPEED
  #else
    #define BOARD_DEVICE_RHPORT_SPEED   OPT_MODE_FULL_SPEED
  #endif
#endif

// Device mode with rhport and speed defined by board.mk
#if   BOARD_DEVICE_RHPORT_NUM == 0
  #define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | BOARD_DEVICE_RHPORT_SPEED)
#elif BOARD_DEVICE_RHPORT_NUM == 1
  #define CFG_TUSB_RHPORT1_MODE     (OPT_MODE_DEVICE | BOARD_DEVICE_RHPORT_SPEED)
#else
  #error "Incorrect RHPort configuration"
#endif

// This example only uses device mode
#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENABLED
  #define CFG_TUD_ENABLED       1
#endif

//------------- CLASS -------------//
#define CFG_TUD_HID             1
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

// HID buffer size Should be sufficient to hold ID (if any) + Data
#define CFG_TUD_HID_EP_BUFSIZE    64

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
