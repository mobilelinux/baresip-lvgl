#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "database_manager.h"
#include "logger.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../ui/ui_helpers.h"

// Forward declare external Contacts API (or include header if available)
extern void contacts_applet_open_new(const char *number);
extern applet_t contacts_applet;

//static applet_t *g_applet = NULL;
// Structs
typedef struct {
    lv_obj_t *list_view;
    lv_obj_t *detail_view;
    
    // Detail View Components
    lv_obj_t *msg_list;
    lv_obj_t *input_ta;
    lv_obj_t *kb;
    // Compose View Components
    lv_obj_t *compose_view;
    lv_obj_t *to_ta;
    lv_obj_t *compose_input_ta;
    lv_obj_t *compose_kb;
    
    // Picker
    lv_obj_t *picker_modal;
    
    // ... existing ... 
    char current_peer[128];
} chat_data_t;

// Selection Mode Globals
#define MAX_CHAT_THREADS 50
static bool g_chat_selection_mode = false;
static bool g_chat_selected_mask[MAX_CHAT_THREADS];
static chat_message_t g_chat_threads[MAX_CHAT_THREADS];
static int g_chat_thread_count = 0;
static lv_obj_t *g_trash_btn = NULL;

static chat_data_t *g_data = NULL;

// Forward
static void show_list_screen(void);
static void show_detail_screen(const char *peer);
static void show_compose_screen(void);
static void create_message_bubble(lv_obj_t *parent, const char *text, int direction);

// --- Helpers ---
static void format_time(long timestamp, char *buf, size_t size) {
    if (!buf || size == 0) return;
    struct tm *tm_info = localtime(&timestamp);
    if (tm_info) {
        strftime(buf, size, "%H:%M", tm_info);
    } else {
        buf[0] = '\0';
    }
}

// Clean URI Helper
static void get_display_name(const char *uri, char *out_name, size_t size) {
    if(!uri || !out_name) return;
    
    char clean_uri[128];
    strncpy(clean_uri, uri, sizeof(clean_uri)-1);
    clean_uri[sizeof(clean_uri)-1] = '\0';
    char *p = strchr(clean_uri, ';'); if(p) *p = '\0';
    char *s = clean_uri;
    if(strncmp(s, "sip:", 4)==0) s+=4;
    else if(strncmp(s, "sips:", 5)==0) s+=5;
    
    // Try Contact
    char contact[128];
    if(db_contact_find(s, contact, sizeof(contact)) == 0) {
        snprintf(out_name, size, "%s", contact);
    } else {
        snprintf(out_name, size, "%s", s);
    }
}

// --- Contact Picker ---

static void close_picker(void) {
    if (g_data && g_data->picker_modal) {
        lv_obj_del(g_data->picker_modal);
        g_data->picker_modal = NULL;
    }
}

static void picker_cancel(lv_event_t *e) {
  (void)e;
    close_picker();
}

static void picker_item_clicked(lv_event_t *e) {
    const char *number = (const char *)lv_event_get_user_data(e);
    if (g_data && g_data->to_ta && number) {
        const char *current = lv_textarea_get_text(g_data->to_ta);
        if (current && strlen(current) > 0) {
            lv_textarea_add_text(g_data->to_ta, "; ");
        }
        lv_textarea_add_text(g_data->to_ta, number);
        close_picker();
    }
}

static void show_contact_picker(void) {
    if (!g_data || g_data->picker_modal) return;
    
    g_data->picker_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_data->picker_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_data->picker_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_data->picker_modal, LV_OPA_50, 0);
    lv_obj_set_flex_flow(g_data->picker_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_data->picker_modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *panel = lv_obj_create(g_data->picker_modal);
    lv_obj_set_size(panel, LV_PCT(80), LV_PCT(80));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 10, 0);
    
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Is Select Contact");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_center(title);
    
    // List
    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(80)); // Leave room for cancel
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    
    // Allocate on HEAP to prevent stack overflow
    db_contact_t *contacts = calloc(50, sizeof(db_contact_t));
    if (contacts) {
        int count = db_get_contacts(contacts, 50);
        
        for(int i=0; i<count; i++) {
            lv_obj_t *btn = lv_btn_create(list);
            lv_obj_set_width(btn, LV_PCT(100));
            
            lv_obj_t *lbl = lv_label_create(btn);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s (%s)", contacts[i].name, contacts[i].number);
            lv_label_set_text(lbl, buf);
            lv_obj_center(lbl);
            
            char *num_copy = strdup(contacts[i].number); // Need cleanup?
            lv_obj_add_event_cb(btn, picker_item_clicked, LV_EVENT_CLICKED, num_copy);
        }
        free(contacts);
    }
    
    // Cancel
    lv_obj_t *btn_cancel = lv_btn_create(panel);
    lv_obj_set_width(btn_cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(btn_cancel, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(lv_label_create(btn_cancel), "Cancel");
    lv_obj_add_event_cb(btn_cancel, picker_cancel, LV_EVENT_CLICKED, NULL);
}

// --- Compose Logic ---

static void picker_btn_clicked(lv_event_t *e) {
    (void)e;
    show_contact_picker();
}

static void send_compose_clicked(lv_event_t *e) {
    (void)e;
    if (!g_data) return;
    const char *to = lv_textarea_get_text(g_data->to_ta);
    const char *text = lv_textarea_get_text(g_data->compose_input_ta);
    
    if (!to || strlen(to)==0 || !text || strlen(text)==0) return;
    
    // Parse recipients
    char to_buf[256];
    strncpy(to_buf, to, sizeof(to_buf)-1); to_buf[sizeof(to_buf)-1]='\0';
    
    char last_recipient[128] = "";
    
    char *ctx = NULL;
    char *token = strtok_r(to_buf, ";", &ctx);
    while (token) {
        // Trim whitespace
        while(*token && *token == ' ') token++;
        // Remove trailing
        char *end = token + strlen(token) - 1;
        while(end > token && *end == ' ') { *end = '\0'; end--; }
        
        if (strlen(token) > 0) {
            log_info("ChatApplet", "Sending to %s: %s", token, text);
            baresip_manager_send_message(token, text);
            // Save last recipient for navigation
            strncpy(last_recipient, token, sizeof(last_recipient)-1);
        }
        token = strtok_r(NULL, ";", &ctx);
    }
    
    // Done -> Go to Conversation (Detail) if valid, else List
    if (strlen(last_recipient) > 0) {
        show_detail_screen(last_recipient);
        // Optimistic Update: Manually add the bubble since DB might not have it yet
        if (g_data && g_data->msg_list) {
            create_message_bubble(g_data->msg_list, text, 1);
            // Scroll
            uint32_t cnt = lv_obj_get_child_cnt(g_data->msg_list);
            if (cnt > 0) {
                lv_obj_t *last = lv_obj_get_child(g_data->msg_list, -1);
                if (last) {
                    lv_obj_update_layout(last);
                    lv_obj_scroll_to_view(last, LV_ANIM_OFF);
                }
            }
        }
    } else {
        show_list_screen();
    }
}

static void compose_back_clicked(lv_event_t *e) {
    (void)e;
    show_list_screen();
}

static void show_compose_screen(void) {
    if (!g_data) return;
    
    lv_obj_add_flag(g_data->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_data->detail_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_data->compose_view, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_clean(g_data->compose_view);
    
    // Header
    ui_create_title_bar(g_data->compose_view, "New Message", true, compose_back_clicked, NULL);
    
    // To: Field
    lv_obj_t *row_to = lv_obj_create(g_data->compose_view);
    lv_obj_set_size(row_to, LV_PCT(100), 60);
    lv_obj_set_flex_flow(row_to, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_to, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row_to, 5, 0);
    
    lv_label_set_text(lv_label_create(row_to), "To:");
    
    g_data->to_ta = lv_textarea_create(row_to);
    lv_obj_set_flex_grow(g_data->to_ta, 1);
    lv_textarea_set_one_line(g_data->to_ta, true);
    lv_textarea_set_placeholder_text(g_data->to_ta, "Name/Number");
    
    lv_obj_t *btn_add = lv_btn_create(row_to);
    lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_LIST); // Selector
    lv_obj_add_event_cb(btn_add, picker_btn_clicked, LV_EVENT_CLICKED, NULL);
    
    // Content View (Placeholder)
    lv_obj_t *content = lv_obj_create(g_data->compose_view);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_style_bg_color(content, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    
    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, "Enter recipients separated by ';'");
    lv_obj_center(hint);
    
    // Input Area
    lv_obj_t *input_cont = lv_obj_create(g_data->compose_view);
    lv_obj_set_size(input_cont, LV_PCT(100), 80);
    lv_obj_set_flex_flow(input_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_align(input_cont, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_pad_all(input_cont, 5, 0);

    g_data->compose_input_ta = lv_textarea_create(input_cont);
    lv_obj_set_flex_grow(g_data->compose_input_ta, 1);
    lv_textarea_set_placeholder_text(g_data->compose_input_ta, "Type message...");
    
    lv_obj_t *btn_send = lv_btn_create(input_cont);
    lv_label_set_text(lv_label_create(btn_send), LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(btn_send, send_compose_clicked, LV_EVENT_CLICKED, NULL);
}

static void new_chat_clicked(lv_event_t *e) {
    (void)e;
    show_compose_screen();
}

// --- Detail View Logic ---

static void create_message_bubble(lv_obj_t *parent, const char *text, int direction) {
    if (!parent || !text) return;

    lv_obj_t *bubble = lv_obj_create(parent);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, 250, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_set_style_radius(bubble, 10, 0);

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, 200); // Max text width

    if (direction == 0) { // Incoming (Left)
        lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(bubble, lv_palette_lighten(LV_PALETTE_GREY, 3), 0);
        lv_obj_set_align(bubble, LV_ALIGN_TOP_LEFT);
    } else { // Outgoing (Right)
        lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(bubble, lv_palette_lighten(LV_PALETTE_BLUE, 3), 0);
        lv_obj_set_align(bubble, LV_ALIGN_TOP_RIGHT);
    }
}

static void send_clicked(lv_event_t *e) {
    (void)e;
    if (!g_data) return;
    const char *text = lv_textarea_get_text(g_data->input_ta);
    if (!text || strlen(text) == 0) return;
    
    // Send
    log_info("ChatApplet", "Sending to %s: %s", g_data->current_peer, text);
    baresip_manager_send_message(g_data->current_peer, text);
    
    // Optimistic UI Update
    if (g_data && g_data->msg_list) {
         create_message_bubble(g_data->msg_list, text, 1); // 1 = Outgoing
         
         // Scroll to bottom safely
         uint32_t cnt = lv_obj_get_child_cnt(g_data->msg_list);
         if (cnt > 0) {
            lv_obj_t *last = lv_obj_get_child(g_data->msg_list, -1);
            if (last) {
                // Ensure the new bubble has a valid size before scrolling
                lv_obj_update_layout(last); 
                lv_obj_scroll_to_view(last, LV_ANIM_OFF);
            }
         }
    }

    // Clear Input
    if (g_data->input_ta) {
        lv_textarea_set_text(g_data->input_ta, "");
    }
}

static void back_to_list_clicked(lv_event_t *e) {
    (void)e;
    show_list_screen();
}

static void show_detail_screen(const char *peer) {
    if (!g_data) return;
    
    // Hide List, Show Detail
    lv_obj_add_flag(g_data->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_data->compose_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_data->detail_view, LV_OBJ_FLAG_HIDDEN);
    
    strncpy(g_data->current_peer, peer, sizeof(g_data->current_peer)-1);
    g_data->current_peer[sizeof(g_data->current_peer)-1] = '\0';
    
    // Rebuild Detail
    lv_obj_clean(g_data->detail_view);
    
    // Header
    char display[128];
    get_display_name(peer, display, sizeof(display));
    
    // Use common title bar
    ui_create_title_bar(g_data->detail_view, display, true, back_to_list_clicked, NULL);
    
    // Message List (Scrollable)
    g_data->msg_list = lv_obj_create(g_data->detail_view);
    lv_obj_set_size(g_data->msg_list, LV_PCT(100), LV_PCT(60)); // Leave room for input
    lv_obj_set_flex_flow(g_data->msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_data->msg_list, 10, 0);
    lv_obj_set_style_pad_gap(g_data->msg_list, 10, 0);
    
    // Load History - ALLOCATE ON HEAP (~58KB stack fix)
    chat_message_t *msgs = calloc(50, sizeof(chat_message_t));
    if (msgs) {
        int count = db_chat_get_history(peer, msgs, 50);
        for (int i=0; i<count; i++) {
            create_message_bubble(g_data->msg_list, msgs[i].content, msgs[i].direction);
        }
        free(msgs);
    }
    // Scroll to bottom (Safe)
    if (lv_obj_get_child_cnt(g_data->msg_list) > 0) {
        lv_obj_t *last = lv_obj_get_child(g_data->msg_list, -1);
        if (last) lv_obj_scroll_to_view(last, LV_ANIM_OFF);
    }

    // Input Area
    lv_obj_t *input_cont = lv_obj_create(g_data->detail_view);
    lv_obj_set_size(input_cont, LV_PCT(100), LV_PCT(25));
    lv_obj_set_flex_flow(input_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_align(input_cont, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_pad_all(input_cont, 5, 0);

    g_data->input_ta = lv_textarea_create(input_cont);
    lv_obj_set_flex_grow(g_data->input_ta, 1);
    lv_textarea_set_one_line(g_data->input_ta, true);
    lv_textarea_set_placeholder_text(g_data->input_ta, "Type message...");
    
    // Keyboard
    g_data->kb = lv_keyboard_create(g_data->detail_view);
    lv_keyboard_set_textarea(g_data->kb, g_data->input_ta);
    lv_obj_add_flag(g_data->kb, LV_OBJ_FLAG_HIDDEN); // Show on focus?
    
    lv_obj_t *btn_send = lv_btn_create(input_cont);
    lv_label_set_text(lv_label_create(btn_send), LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(btn_send, send_clicked, LV_EVENT_CLICKED, NULL);
}

// --- List View Logic ---

static void back_from_list_clicked(lv_event_t *e) {
    (void)e;
    applet_manager_back();
}

static void populate_chat_list(void);

static void toggle_selection_clicked(lv_event_t *e) {
    (void)e;
    if (g_chat_selection_mode) {
        // Execute Delete
        for(int i=0; i<g_chat_thread_count; i++) {
            if (g_chat_selected_mask[i]) {
                log_info("ChatApplet", "Deleting thread: %s", g_chat_threads[i].peer_uri);
                db_chat_delete_thread(g_chat_threads[i].peer_uri);
            }
        }
        
        // Exit Mode
        g_chat_selection_mode = false;
        
        // Load Fresh Data
        g_chat_thread_count = db_chat_get_threads(g_chat_threads, MAX_CHAT_THREADS);
        
        // Reset Icon
         if(g_trash_btn) {
             lv_label_set_text(lv_obj_get_child(g_trash_btn, 0), LV_SYMBOL_TRASH);
            lv_obj_set_style_text_color(lv_obj_get_child(g_trash_btn, 0), lv_color_white(), 0);
        }
        
        populate_chat_list();
    } else {
        // Enter Mode
        g_chat_selection_mode = true;
        memset(g_chat_selected_mask, 0, sizeof(g_chat_selected_mask));
        
        // Change Icon to Check/OK
        if(g_trash_btn) {
            lv_label_set_text(lv_obj_get_child(g_trash_btn, 0), LV_SYMBOL_OK);
            lv_obj_set_style_text_color(lv_obj_get_child(g_trash_btn, 0), lv_color_white(), 0);
        }
        
        populate_chat_list();
    }
}

static lv_obj_t *g_context_menu = NULL;
static char g_context_menu_peer[128];
static bool g_chat_long_pressed = false;

static void close_context_menu(void) {
    if (g_context_menu) {
        lv_obj_del(g_context_menu);
        g_context_menu = NULL;
    }
}

static void context_bg_clicked(lv_event_t *e) {
    (void)e;
    close_context_menu();
}

static void menu_add_contact_clicked(lv_event_t *e) {
    (void)e;
    close_context_menu();
    // Launch Contacts Applet with Editor
    log_info("ChatApplet", "Add to contact: %s", g_context_menu_peer);
    contacts_applet_open_new(g_context_menu_peer);
    applet_manager_launch_applet(&contacts_applet);
}

static void menu_move_top_clicked(lv_event_t *e) {
    (void)e;
    close_context_menu();
    log_info("ChatApplet", "Bump thread: %s", g_context_menu_peer);
    db_chat_bump_thread(g_context_menu_peer);
    
    // Refresh List
    g_chat_thread_count = db_chat_get_threads(g_chat_threads, MAX_CHAT_THREADS);
    populate_chat_list();
}

static void menu_delete_clicked(lv_event_t *e) {
    (void)e;
    close_context_menu();
    log_info("ChatApplet", "Delete thread: %s", g_context_menu_peer);
    db_chat_delete_thread(g_context_menu_peer);
    
    // Refresh List
    g_chat_thread_count = db_chat_get_threads(g_chat_threads, MAX_CHAT_THREADS);
    populate_chat_list();
}

static void show_context_menu(const char *peer_uri) {
    if (g_context_menu) return;
    
    strncpy(g_context_menu_peer, peer_uri, sizeof(g_context_menu_peer)-1);
    
    g_context_menu = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_context_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_context_menu, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_context_menu, LV_OPA_40, 0);
    lv_obj_add_event_cb(g_context_menu, context_bg_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *menu = lv_obj_create(g_context_menu);
    lv_obj_set_width(menu, 250);
    lv_obj_set_height(menu, LV_SIZE_CONTENT);
    lv_obj_center(menu);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(menu, 10, 0);
    
    // Check if contact exists
    char name_buf[128];
    if (db_contact_find(peer_uri, name_buf, sizeof(name_buf)) != 0) {
        // Not found -> Show Add
        lv_obj_t *btn_add = lv_btn_create(menu);
        lv_obj_set_width(btn_add, LV_PCT(100));
        lv_obj_t *lbl_add = lv_label_create(btn_add);
        lv_label_set_text(lbl_add, LV_SYMBOL_PLUS " Add to Contacts");
        lv_obj_align(lbl_add, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_add_event_cb(btn_add, menu_add_contact_clicked, LV_EVENT_CLICKED, NULL);
    }
    
    // Move to Top
    lv_obj_t *btn_top = lv_btn_create(menu);
    lv_obj_set_width(btn_top, LV_PCT(100));
    lv_obj_t *lbl_top = lv_label_create(btn_top);
    lv_label_set_text(lbl_top, LV_SYMBOL_UPLOAD " Move to Top");
    lv_obj_align(lbl_top, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn_top, menu_move_top_clicked, LV_EVENT_CLICKED, NULL);
    
    // Delete
    lv_obj_t *btn_del = lv_btn_create(menu);
    lv_obj_set_width(btn_del, LV_PCT(100));
    lv_obj_set_style_bg_color(btn_del, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_t *lbl_del = lv_label_create(btn_del);
    lv_label_set_text(lbl_del, LV_SYMBOL_TRASH " Delete");
    lv_obj_align(lbl_del, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn_del, menu_delete_clicked, LV_EVENT_CLICKED, NULL);
}

static void list_item_clicked(lv_event_t *e) {
    int idx = (intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_LONG_PRESSED) {
        if (!g_chat_selection_mode && idx >= 0 && idx < g_chat_thread_count) {
             g_chat_long_pressed = true;
             show_context_menu(g_chat_threads[idx].peer_uri);
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (g_chat_long_pressed) {
            g_chat_long_pressed = false;
            return;
        }

        if (g_chat_selection_mode) {
            if (idx >= 0 && idx < g_chat_thread_count) {
                g_chat_selected_mask[idx] = !g_chat_selected_mask[idx];
                // Simple Redraw (Inefficient but robust)
                populate_chat_list();
            }
        } else {
            if (idx >= 0 && idx < g_chat_thread_count) {
                show_detail_screen(g_chat_threads[idx].peer_uri);
            }
        }
    }
}

static void populate_chat_list(void) {
    if (!g_data || !g_data->list_view) return;
    
    // Find list container (It's the 2nd child of list_view? Or search?)
    // In show_list_screen, we create header then list.
    // Let's store list pointer or find it.
    // Hack: list_view has children. 0=Header, 1=List, 2=FAB.
    if (lv_obj_get_child_cnt(g_data->list_view) < 2) return;
    lv_obj_t *list = lv_obj_get_child(g_data->list_view, 1);
    
    lv_obj_clean(list);
    
    if (g_chat_thread_count == 0) {
        lv_obj_t *lbl = lv_label_create(list);
        lv_label_set_text(lbl, "No messages");
        lv_obj_center(lbl);
    }
    
    for (int i=0; i<g_chat_thread_count; i++) {
        lv_obj_t *item = lv_obj_create(list);
        lv_obj_set_size(item, LV_PCT(95), 80);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(item, 10, 0);
        lv_obj_set_style_pad_column(item, 15, 0); // Gap between Avatar/Text/Time
        
        if (g_chat_selection_mode) {
             lv_obj_t *chk = lv_checkbox_create(item);
             lv_checkbox_set_text(chk, "");
             if (g_chat_selected_mask[i]) lv_obj_add_state(chk, LV_STATE_CHECKED);
             lv_obj_add_flag(chk, LV_OBJ_FLAG_EVENT_BUBBLE); 
        }
        
        // Avatar
        lv_obj_t *av = lv_obj_create(item);
        lv_obj_set_size(av, 50, 50);
        lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(av, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_clear_flag(av, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(av, LV_OBJ_FLAG_CLICKABLE); // Pass click to item
        
        lv_obj_t *av_lbl = lv_label_create(av);
        lv_label_set_text(av_lbl, LV_SYMBOL_IMAGE);
        lv_obj_center(av_lbl);
        
        // Text Container (Grow)
        lv_obj_t *txts = lv_obj_create(item);
        lv_obj_set_flex_grow(txts, 1);
        lv_obj_set_flex_flow(txts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(txts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START); // Align left
        lv_obj_set_style_border_width(txts, 0, 0);
        lv_obj_set_style_pad_all(txts, 0, 0);
        lv_obj_set_style_pad_gap(txts, 5, 0); // Gap between Name and Message
        lv_obj_clear_flag(txts, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(txts, LV_OBJ_FLAG_CLICKABLE); // Pass click to item
        
        // Name (Top Line)
        lv_obj_t *name_lbl = lv_label_create(txts);
        char display[128];
        get_display_name(g_chat_threads[i].peer_uri, display, sizeof(display));
        lv_label_set_text(name_lbl, display);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_width(name_lbl, LV_PCT(100));
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        
        // Last Message (Bottom Line)
        lv_obj_t *msg_lbl = lv_label_create(txts);
        lv_label_set_text(msg_lbl, g_chat_threads[i].content);
        lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(msg_lbl, LV_PCT(100)); // Fill width
        lv_obj_set_style_max_height(msg_lbl, 20, 0); // Limit height to approx 1 line
        lv_obj_set_style_text_color(msg_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
        
        // Time (Right)
        char time_buf[16];
        format_time(g_chat_threads[i].timestamp, time_buf, sizeof(time_buf));
        lv_obj_t *time_lbl = lv_label_create(item);
        lv_label_set_text(time_lbl, time_buf);
        lv_obj_set_width(time_lbl, LV_SIZE_CONTENT);
        // lv_obj_set_flex_shrink(time_lbl, 0); // Not supported in this LVGL version
        
        lv_obj_add_event_cb(item, list_item_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_add_event_cb(item, list_item_clicked, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
    }
    
    // FAB Visibility
    if (lv_obj_get_child_cnt(g_data->list_view) > 2) {
        lv_obj_t *fab = lv_obj_get_child(g_data->list_view, 2);
        if (g_chat_selection_mode) lv_obj_add_flag(fab, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(fab, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_list_screen(void) {
    if (!g_data) return;
    
    lv_obj_clear_flag(g_data->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_data->detail_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_data->compose_view, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_clean(g_data->list_view);
    
    // Header
    lv_obj_t *header_cont = ui_create_title_bar(g_data->list_view, "Messages", true, back_from_list_clicked, NULL);
    
    // Trash Button
    g_trash_btn = ui_header_add_action_btn(header_cont, LV_SYMBOL_TRASH, toggle_selection_clicked, NULL);

    
    // List Container (Scrollable)
    lv_obj_t *list = lv_obj_create(g_data->list_view);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    
    // Initial Load
    g_chat_thread_count = db_chat_get_threads(g_chat_threads, MAX_CHAT_THREADS);
    g_chat_selection_mode = false;
    
    populate_chat_list();
    
    // List Loop Moved to populate_chat_list

    // New Chat FAB (Created last for Z-Order)
    lv_obj_t *fab = lv_btn_create(g_data->list_view);
    lv_obj_set_size(fab, 56, 56);
    lv_obj_set_style_radius(fab, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(fab, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_flag(fab, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(fab, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_t *icon = lv_label_create(fab);
    lv_label_set_text(icon, LV_SYMBOL_PLUS);
    lv_obj_center(icon);
    lv_obj_add_event_cb(fab, new_chat_clicked, LV_EVENT_CLICKED, NULL);
}

// --- Applet Interface ---

static int chat_init(applet_t *applet) {
    g_data = lv_mem_alloc(sizeof(chat_data_t));
    memset(g_data, 0, sizeof(chat_data_t));
    applet->user_data = g_data;
    
    // Screens Container
    lv_obj_t *cont = lv_obj_create(applet->screen);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    
    g_data->list_view = lv_obj_create(cont);
    lv_obj_set_size(g_data->list_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(g_data->list_view, 0, 0);
    lv_obj_set_style_border_width(g_data->list_view, 0, 0);
    lv_obj_set_style_radius(g_data->list_view, 0, 0);
    lv_obj_set_flex_flow(g_data->list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(g_data->list_view, LV_SCROLLBAR_MODE_OFF); 
    lv_obj_clear_flag(g_data->list_view, LV_OBJ_FLAG_SCROLLABLE);
    
    g_data->detail_view = lv_obj_create(cont);
    lv_obj_set_size(g_data->detail_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(g_data->detail_view, 0, 0);
    lv_obj_set_style_border_width(g_data->detail_view, 0, 0);
    lv_obj_set_style_radius(g_data->detail_view, 0, 0);
    lv_obj_set_flex_flow(g_data->detail_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(g_data->detail_view, LV_OBJ_FLAG_HIDDEN);
    
    g_data->compose_view = lv_obj_create(cont);
    lv_obj_set_size(g_data->compose_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(g_data->compose_view, 0, 0);
    lv_obj_set_style_border_width(g_data->compose_view, 0, 0);
    lv_obj_set_style_radius(g_data->compose_view, 0, 0);
    lv_obj_set_flex_flow(g_data->compose_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(g_data->compose_view, LV_OBJ_FLAG_HIDDEN);
    
    return 0;
}

static void chat_start(applet_t *applet) {
    (void)applet;
    show_list_screen();
}

void chat_applet_open_peer(const char *peer) {
    if (!g_data) return;
    
    // Check if thread exists
    bool found = false;
    for(int i=0; i<g_chat_thread_count; i++) {
        // Simple URI matching
        if (strstr(g_chat_threads[i].peer_uri, peer)) {
             show_detail_screen(g_chat_threads[i].peer_uri);
             found = true;
             break;
        }
    }
    
    if (!found) {
        show_compose_screen();
        if (g_data->to_ta) {
            lv_textarea_set_text(g_data->to_ta, peer);
        }
    }
}

static void chat_stop(applet_t *applet) {
    (void)applet;
    // Cleanup
}

static void chat_destroy(applet_t *applet) {
    if (g_data) {
        lv_mem_free(g_data);
        g_data = NULL;
    }
    applet->user_data = NULL;
}

APPLET_DEFINE(chat_applet, "Messages", "Chat", LV_SYMBOL_ENVELOPE);

void chat_applet_register(void) {
    chat_applet.callbacks.init = chat_init;
    chat_applet.callbacks.start = chat_start;
    chat_applet.callbacks.stop = chat_stop;
    chat_applet.callbacks.destroy = chat_destroy;
    applet_manager_register(&chat_applet);
}
