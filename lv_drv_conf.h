/**
 * @file lv_drv_conf.h
 *
 */

#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

/*********************
 *  DISPLAY DRIVERS
 *********************/

/*-------------------
 *  SDL
 *-------------------*/
/*-------------------
 *  SDL
 *-------------------*/
#ifndef USE_SDL
#  define USE_SDL         1
#endif

#if USE_SDL
#  define SDL_HOR_RES     800
#  define SDL_VER_RES     600
#  define SDL_ZOOM        1
#endif

/*-------------------
 *  Linux frame buffer device (/dev/fbx)
 *-------------------*/
#ifndef USE_FBDEV
#  define USE_FBDEV           0
#endif

#if USE_FBDEV
#  define FBDEV_PATH          "/dev/fb0"
#endif

/*********************
 *  INPUT DEVICES
 *********************/

/*-------------------
 *  SDL
 *-------------------*/
#ifndef USE_SDL
#  define USE_SDL         1
#endif

/*-------------------
 *  EVDEV
 *-------------------*/
#ifndef USE_EVDEV
#  define USE_EVDEV           0
#endif

#if USE_EVDEV
#  define EVDEV_NAME   "/dev/input/event2"  /* Mouse */
#  define EVDEV_SWAP_AXES         0
#  define EVDEV_CALIBRATE         1
#  define EVDEV_HOR_MIN           0
#  define EVDEV_HOR_MAX           32767
#  define EVDEV_VER_MIN           0
#  define EVDEV_VER_MAX           32767
#  define EVDEV_SCALE             0
#  define EVDEV_SCALE_HOR_RES     (4096)
#  define EVDEV_SCALE_VER_RES     (4096)
#endif

#endif /*LV_DRV_CONF_H*/
