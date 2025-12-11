#include "applet.h"
#include "applet_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Calculator applet data
typedef struct {
  lv_obj_t *display;
  char display_text[32];
  double current_value;
  double stored_value;
  char operation;
  bool new_number;
} calc_data_t;

// Event handler for back button
static void calc_back_btn_clicked(lv_event_t *e) { applet_manager_back(); }

// Event handler for number buttons
static void number_btn_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  calc_data_t *data = (calc_data_t *)applet->user_data;
  const char *num = lv_event_get_user_data(e);

  if (data->new_number) {
    strcpy(data->display_text, num);
    data->new_number = false;
  } else {
    if (strlen(data->display_text) < 15) {
      strcat(data->display_text, num);
    }
  }

  lv_label_set_text(data->display, data->display_text);
}

// Event handler for operation buttons
static void operation_btn_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  calc_data_t *data = (calc_data_t *)applet->user_data;
  const char *op = lv_event_get_user_data(e);

  data->current_value = atof(data->display_text);

  // Perform previous operation if exists
  if (data->operation != 0) {
    switch (data->operation) {
    case '+':
      data->stored_value += data->current_value;
      break;
    case '-':
      data->stored_value -= data->current_value;
      break;
    case '*':
      data->stored_value *= data->current_value;
      break;
    case '/':
      if (data->current_value != 0) {
        data->stored_value /= data->current_value;
      }
      break;
    }
    snprintf(data->display_text, sizeof(data->display_text), "%.2f",
             data->stored_value);
    lv_label_set_text(data->display, data->display_text);
  } else {
    data->stored_value = data->current_value;
  }

  data->operation = op[0];
  data->new_number = true;
}

// Event handler for equals button
static void equals_btn_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  calc_data_t *data = (calc_data_t *)applet->user_data;

  data->current_value = atof(data->display_text);

  if (data->operation != 0) {
    switch (data->operation) {
    case '+':
      data->stored_value += data->current_value;
      break;
    case '-':
      data->stored_value -= data->current_value;
      break;
    case '*':
      data->stored_value *= data->current_value;
      break;
    case '/':
      if (data->current_value != 0) {
        data->stored_value /= data->current_value;
      }
      break;
    }
    snprintf(data->display_text, sizeof(data->display_text), "%.2f",
             data->stored_value);
    lv_label_set_text(data->display, data->display_text);
    data->operation = 0;
    data->new_number = true;
  }
}

// Event handler for clear button
static void clear_btn_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  calc_data_t *data = (calc_data_t *)applet->user_data;

  strcpy(data->display_text, "0");
  data->current_value = 0;
  data->stored_value = 0;
  data->operation = 0;
  data->new_number = true;
  lv_label_set_text(data->display, data->display_text);
}

static int calculator_init(applet_t *applet) {
  log_info("CalculatorApplet", "Initializing");

  // Allocate applet data
  calc_data_t *data = lv_mem_alloc(sizeof(calc_data_t));
  if (!data)
    return -1;

  strcpy(data->display_text, "0");
  data->current_value = 0;
  data->stored_value = 0;
  data->operation = 0;
  data->new_number = true;
  applet->user_data = data;

  // Create header with back button
  lv_obj_t *header = lv_obj_create(applet->screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(header, 10, 0);

  // Back button
  lv_obj_t *back_btn = lv_btn_create(header);
  lv_obj_set_size(back_btn, 50, 40);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, calc_back_btn_clicked, LV_EVENT_CLICKED, NULL);

  // Title
  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Calculator");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_pad_left(title, 20, 0);

  // Create display
  lv_obj_t *display_container = lv_obj_create(applet->screen);
  lv_obj_set_size(display_container, LV_PCT(90), 80);
  lv_obj_align(display_container, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_style_bg_color(display_container, lv_color_hex(0x333333), 0);

  data->display = lv_label_create(display_container);
  lv_label_set_text(data->display, "0");
  lv_obj_set_style_text_font(data->display, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(data->display, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(data->display, LV_ALIGN_RIGHT_MID, -10, 0);

  // Create button grid
  lv_obj_t *btn_grid = lv_obj_create(applet->screen);
  lv_obj_set_size(btn_grid, LV_PCT(90), LV_PCT(55));
  lv_obj_align(btn_grid, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(btn_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(btn_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(btn_grid, 5, 0);
  lv_obj_set_style_pad_gap(btn_grid, 5, 0);

  // Button layout: 4x5 grid
  const char *buttons[] = {"7", "8", "9", "/", "4", "5", "6", "*",
                           "1", "2", "3", "-", "C", "0", "=", "+"};

  for (int i = 0; i < 16; i++) {
    lv_obj_t *btn = lv_btn_create(btn_grid);
    lv_obj_set_size(btn, 60, 60);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, buttons[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);

    // Add appropriate event handler
    if (buttons[i][0] >= '0' && buttons[i][0] <= '9') {
      lv_obj_add_event_cb(btn, number_btn_clicked, LV_EVENT_CLICKED,
                          (void *)buttons[i]);
    } else if (buttons[i][0] == 'C') {
      lv_obj_add_event_cb(btn, clear_btn_clicked, LV_EVENT_CLICKED, NULL);
    } else if (buttons[i][0] == '=') {
      lv_obj_add_event_cb(btn, equals_btn_clicked, LV_EVENT_CLICKED, NULL);
    } else {
      lv_obj_add_event_cb(btn, operation_btn_clicked, LV_EVENT_CLICKED,
                          (void *)buttons[i]);
    }
  }

  return 0;
}

static void calculator_start(applet_t *applet) {
  log_info("CalculatorApplet", "Started");
}

static void calculator_pause(applet_t *applet) {
  log_debug("CalculatorApplet", "Paused");
}

static void calculator_resume(applet_t *applet) {
  log_debug("CalculatorApplet", "Resumed");
}

static void calculator_stop(applet_t *applet) {
  log_info("CalculatorApplet", "Stopped");
}

static void calculator_destroy(applet_t *applet) {
  log_info("CalculatorApplet", "Destroying");
  if (applet->user_data) {
    lv_mem_free(applet->user_data);
    applet->user_data = NULL;
  }
}

// Define the calculator applet
APPLET_DEFINE(calculator_applet, "Calculator", "Simple Calculator",
              LV_SYMBOL_EDIT);

// Initialize callbacks
void calculator_applet_register(void) {
  calculator_applet.callbacks.init = calculator_init;
  calculator_applet.callbacks.start = calculator_start;
  calculator_applet.callbacks.pause = calculator_pause;
  calculator_applet.callbacks.resume = calculator_resume;
  calculator_applet.callbacks.stop = calculator_stop;
  calculator_applet.callbacks.destroy = calculator_destroy;

  applet_manager_register(&calculator_applet);
}
