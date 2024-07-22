// Core Arduino and ESP32 libraries
#include <Arduino.h>
#include <SPI.h>

// Graphics and UI libraries
#include <lvgl.h>
#include <Arduino_GFX_Library.h>

// Configuration defines
#define GFX_BL 23 // Default backlight pin

// Digital I/O used for SD card (if implemented)
#define SD_CS          5
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18

//****************************************************************************************
//                                   ARDUINO_GFX                                         *
//****************************************************************************************
// Display setup
#if defined(DISPLAY_DEV_KIT)
    Arduino_GFX *gfx = create_default_Arduino_GFX();
#else
    // Custom display configuration
    Arduino_DataBus *bus = new Arduino_ESP32SPI(
        21 /* DC */, 
        15 /* CS */, 
        14 /* SCK */, 
        13 /* MOSI */, 
        GFX_NOT_DEFINED /* MISO */, 
        HSPI /* spi_num */
    );
    Arduino_GFX *gfx = new Arduino_ST7796(
        bus, 
        22 /* RST */, 
        3 /* rotation */, 
        false /* IPS */
    );
#endif

// Uncomment to enable full frame buffer
// #define DIRECT_MODE

#ifdef DIRECT_MODE
  static const bool DIRECT_MODE = true;
#else
  static const bool DIRECT_MODE = false;
#endif

// Screen configuration
static const uint32_t SCREEN_WIDTH = 480;
static const uint32_t SCREEN_HEIGHT = 320;

// LVGL draw buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;

// Button state variable (purpose not clear from the given code)
int buttonState = 1;

//****************************************************************************************
//                                       TOUCH & LOG                                     *
//****************************************************************************************
#include "touch.h"
#include "src/ui/ui.h"

#if LV_USE_LOG != 0
// Serial debugging
void my_print(const char *buf) {
    Serial.printf(buf);
    Serial.flush();
}
#endif

//****************************************************************************************
//                                  FUNCTION DECLARATION                                 *
//****************************************************************************************
// Display flushing callback for LVGL
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
// Touchpad reading callback for LVGL
void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
// Initialize serial communication for debugging
void initSerial();
// Initialize the display and touch screen
void initDisplay();
// Initialize LVGL library and set up UI
void initLVGL();
// Set up LVGL draw buffer
void setupLVGLBuffer();
// Configure LVGL display driver
void setupLVGLDriver();
// Create a simple label on the screen
void createSimpleLabel();
// Update LVGL (handle GUI tasks)
void updateLVGL();
// Update the display (flush changes to screen)
void updateDisplay();
// Draw bitmap to the screen (used in direct mode)
void drawBitmap();

//****************************************************************************************
//                                           SETUP                                       *
//****************************************************************************************
// Arduino setup function: initialize hardware and software components
void setup() {
    initSerial();
    initDisplay();
    initLVGL();
    
    Serial.println("Setup done");
}

//****************************************************************************************
//                                           LOOP                                        *
//****************************************************************************************
// Arduino main loop function: update GUI and display
void loop() {
    updateLVGL();
    updateDisplay();
    
    delay(5);
}

//****************************************************************************************
//                                   FUNCTION DEFINITION                                 *
//****************************************************************************************

// Display flushing callback for LVGL
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
#ifndef DIRECT_MODE
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    #if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    #else
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    #endif
#endif

    lv_disp_flush_ready(disp_drv);
}

// Touchpad reading callback for LVGL
void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    if (touch_has_signal()) {
        if (touch_touched()) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = touch_last_x;
            data->point.y = touch_last_y;
        } else if (touch_released()) {
            data->state = LV_INDEV_STATE_REL;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// Initialize serial communication for debugging
void initSerial() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    while(!Serial);
    Serial.println("Arduino_GFX LVGL_Arduino_v8 example");
    Serial.println("LVGL version: " + String(lv_version_major()) + "." + 
                   lv_version_minor() + "." + lv_version_patch());
}

// Initialize the display and touch screen
void initDisplay() {
    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
        return;
    }
    gfx->fillScreen(BLACK);
    
    #ifdef GFX_BL
        pinMode(GFX_BL, OUTPUT);
        digitalWrite(GFX_BL, HIGH);
    #endif
    
    touch_init(gfx->width(), gfx->height(), gfx->getRotation());
}

// Initialize LVGL library and set up UI
void initLVGL() {
    lv_init();
    
    #if LV_USE_LOG != 0
        lv_log_register_print_cb(my_print);
    #endif
    
    setupLVGLBuffer();
    setupLVGLDriver();
    createSimpleLabel();
    
    ui_init();
}

// Set up LVGL draw buffer
void setupLVGLBuffer() {
    uint32_t bufSize = DIRECT_MODE ? (SCREEN_WIDTH * SCREEN_HEIGHT) : (SCREEN_WIDTH * 40);
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_draw_buf) {
        disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
    }
    if (!disp_draw_buf) {
        Serial.println("LVGL disp_draw_buf allocate failed!");
        return;
    }
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, bufSize);
}

// Configure LVGL display driver
void setupLVGLDriver() {
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    #ifdef DIRECT_MODE
        disp_drv.direct_mode = true;
    #endif
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

// Create a simple label on the screen
void createSimpleLabel() {
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello Arduino! (V" GFX_STR(LVGL_VERSION_MAJOR) "." 
                             GFX_STR(LVGL_VERSION_MINOR) "." GFX_STR(LVGL_VERSION_PATCH) ")");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

// Update LVGL (handle GUI tasks)
void updateLVGL() {
    lv_timer_handler(); // Let the GUI do its work
}

// Update the display (flush changes to screen)
void updateDisplay() {
#ifdef DIRECT_MODE
    #if defined(CANVAS) || defined(RGB_PANEL)
        gfx->flush();
    #else
        drawBitmap();
    #endif
#else
    #ifdef CANVAS
        gfx->flush();
    #endif
#endif
}

// Draw bitmap to the screen (used in direct mode)
void drawBitmap() {
#if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, SCREEN_WIDTH, SCREEN_HEIGHT);
#else
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, SCREEN_WIDTH, SCREEN_HEIGHT);
#endif
}
