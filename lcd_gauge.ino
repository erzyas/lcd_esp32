#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include <array>

using namespace esp_panel::board;

// Константы для настройки
static constexpr uint8_t TOTAL_SCREENS = 3;
static constexpr uint32_t ANIMATION_DURATION = 300;
static constexpr uint16_t SWIPE_THRESHOLD = 70;
static constexpr uint16_t TOUCH_DURATION_THRESHOLD = 500;

// Структура для данных спидометра
struct SpeedometerData {
    int min_value = -40;
    int max_value = 140;
    int current_value = 0;
    int16_t angle_range = 270; // Угловой диапазон шкалы (например, 270 градусов для трехчетвертного круга)
};

// Структура для обработки свайпов
struct TouchData {
    int16_t start_x = 0;
    int16_t start_y = 0;
    bool is_touching = false;
    uint32_t touch_start_time = 0;
};

// Глобальные переменные
static std::array<lv_obj_t*, TOTAL_SCREENS> screens;
static uint8_t current_screen_index = 0;
static bool anim_in_progress = false;
static SpeedometerData speedometer;
static TouchData touch_data;

// Объявления объектов LVGL
static lv_obj_t* meter = nullptr; // Основной объект метра
static lv_meter_indicator_t* indic = nullptr; // Индикатор метра
static lv_obj_t* value_label = nullptr; // Метка для отображения значения

// Стили LVGL
static lv_style_t style_title;
static lv_style_t style_normal;
static lv_style_t style_hint;

// Прототипы функций
void update_scale_value(int new_value);
void create_circular_scale(lv_obj_t* parent);
lv_obj_t* create_screen(const char* title, const char* content, uint8_t screen_num);
void switch_screen(uint8_t new_index, lv_scr_load_anim_t anim_type);
void anim_complete_callback(lv_anim_t* anim);

// Обработчики событий
static void btn_plus_event_handler(lv_event_t* e) {
    speedometer.current_value += 10;
    speedometer.current_value = constrain(speedometer.current_value, 
                                        speedometer.min_value, 
                                        speedometer.max_value);
    update_scale_value(speedometer.current_value);
}

static void btn_minus_event_handler(lv_event_t* e) {
    speedometer.current_value -= 10;
    speedometer.current_value = constrain(speedometer.current_value, 
                                        speedometer.min_value, 
                                        speedometer.max_value);
    update_scale_value(speedometer.current_value);
}

static void touch_event_handler(lv_event_t* e) {
    if (anim_in_progress) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &point);
        touch_data.start_x = point.x;
        touch_data.start_y = point.y;
        touch_data.is_touching = true;
        touch_data.touch_start_time = millis();
    } else if (code == LV_EVENT_RELEASED) {
        if (!touch_data.is_touching) return;

        touch_data.is_touching = false;
        lv_indev_get_point(indev, &point);

        int16_t diff_x = point.x - touch_data.start_x;
        int16_t diff_y = point.y - touch_data.start_y;
        uint32_t touch_duration = millis() - touch_data.touch_start_time;

        if (abs(diff_x) > SWIPE_THRESHOLD && 
            abs(diff_x) > abs(diff_y) * 2 && 
            touch_duration < TOUCH_DURATION_THRESHOLD) {
            
            uint8_t new_index = current_screen_index;
            lv_scr_load_anim_t anim_type;
            
            if (diff_x > 0) {
                new_index = (current_screen_index == 0) ? TOTAL_SCREENS - 1 : current_screen_index - 1;
                anim_type = LV_SCR_LOAD_ANIM_MOVE_RIGHT;
            } else {
                new_index = (current_screen_index == TOTAL_SCREENS - 1) ? 0 : current_screen_index + 1;
                anim_type = LV_SCR_LOAD_ANIM_MOVE_LEFT;
            }
            
            switch_screen(new_index, anim_type);
        }
    }
}

void update_scale_value(int new_value) {
    speedometer.current_value = new_value;
    lv_label_set_text_fmt(value_label, "%d", new_value);
    
    // Обновляем значение индикатора метра
    lv_meter_set_indicator_value(meter, indic, new_value);
}

void create_circular_scale(lv_obj_t* parent) {
    // Создаем объект метра
    meter = lv_meter_create(parent);
    
    // ⚠️ Устанавливаем размер и положение для четверти экрана
    // Предполагается, что разрешение экрана 320x240. Подстройте под ваше!
    lv_obj_set_size(meter, 160, 120); // Ширина = половина ширины экрана, высота = половина высоты экрана
    lv_obj_align(meter, LV_ALIGN_BOTTOM_RIGHT, 0, 0); // Выравниваем в правый нижний угол
    
    // Добавляем шкалу
    lv_meter_scale_t* scale = lv_meter_add_scale(meter);
    
    // Настраиваем диапазон и углы шкалы
    lv_meter_set_scale_range(meter, scale, speedometer.min_value, speedometer.max_value, speedometer.angle_range, 90 + (360 - speedometer.angle_range) / 2);
    
    // Убираем основные и второстепенные деления для чистоты (опционально)
    lv_meter_set_scale_ticks(meter, scale, 0, 0, 0, lv_color_black()); // Без делений
    lv_meter_set_scale_major_ticks(meter, scale, 0, 0, 0, lv_color_black(), 0); // Без основных делений
    
    // Добавляем индикатор типа дуга (ARC)
    indic = lv_meter_add_arc(meter, scale, 15, lv_palette_main(LV_PALETTE_BLUE), 0); // Толщина 15, синий цвет
    lv_meter_set_indicator_value(meter, indic, speedometer.current_value); // Устанавливаем начальное значение
    
    // Создаем метку для отображения численного значения
    value_label = lv_label_create(parent);
    lv_label_set_text_fmt(value_label, "%d", speedometer.current_value);
    lv_obj_add_style(value_label, &style_normal, 0);
    
    // ⚠️ Центрируем метку НАД метром в его верхней части
    lv_obj_align_to(value_label, meter, LV_ALIGN_OUT_TOP_MID, 0, -10); // Смещаем немного вверх от метра
}

lv_obj_t* create_screen(const char* title, const char* content, uint8_t screen_num) {
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    
    // Заголовок
    lv_obj_t* title_label = lv_label_create(screen);
    lv_label_set_text(title_label, title);
    lv_obj_add_style(title_label, &style_title, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Основной контент
    lv_obj_t* content_label = lv_label_create(screen);
    lv_label_set_text(content_label, content);
    lv_obj_add_style(content_label, &style_normal, 0);
    
    if (screen_num == 0) {
        create_circular_scale(screen);
        lv_obj_align(content_label, LV_ALIGN_CENTER, 0, 50);
        
        // Кнопки управления
        lv_obj_t* btn_plus = lv_btn_create(screen);
        lv_obj_set_size(btn_plus, 40, 40);
        lv_obj_align(btn_plus, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
        lv_obj_t* btn_plus_label = lv_label_create(btn_plus);
        lv_label_set_text(btn_plus_label, "+");
        lv_obj_center(btn_plus_label);
        lv_obj_add_event_cb(btn_plus, btn_plus_event_handler, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t* btn_minus = lv_btn_create(screen);
        lv_obj_set_size(btn_minus, 40, 40);
        lv_obj_align(btn_minus, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        lv_obj_t* btn_minus_label = lv_label_create(btn_minus);
        lv_label_set_text(btn_minus_label, "-");
        lv_obj_center(btn_minus_label);
        lv_obj_add_event_cb(btn_minus, btn_minus_event_handler, LV_EVENT_CLICKED, NULL);
        
        // Подсказка
        lv_obj_t* hint_right = lv_label_create(screen);
        lv_label_set_text(hint_right, "<-");
        lv_obj_add_style(hint_right, &style_hint, 0);
        lv_obj_align(hint_right, LV_ALIGN_BOTTOM_RIGHT, -20, -60);
    } else {
        lv_obj_align(content_label, LV_ALIGN_CENTER, 0, 0);
        
        // Подсказки для навигации
        if (screen_num == TOTAL_SCREENS - 1) {
            lv_obj_t* hint_left = lv_label_create(screen);
            lv_label_set_text(hint_left, "->");
            lv_obj_add_style(hint_left, &style_hint, 0);
            lv_obj_align(hint_left, LV_ALIGN_BOTTOM_MID, 0, -20);
        } else {
            lv_obj_t* hint_both = lv_label_create(screen);
            lv_label_set_text(hint_both, "<- ->");
            lv_obj_add_style(hint_both, &style_hint, 0);
            lv_obj_align(hint_both, LV_ALIGN_BOTTOM_MID, 0, -20);
        }
    }
    
    return screen;
}

void switch_screen(uint8_t new_index, lv_scr_load_anim_t anim_type) {
    if (new_index == current_screen_index || anim_in_progress) return;
    
    anim_in_progress = true;
    current_screen_index = new_index;
    
    lv_scr_load_anim(screens[current_screen_index], anim_type, ANIMATION_DURATION, 0, false);
    
    Serial.printf("Switched to screen: %d\n", current_screen_index);
    
    // Устанавливаем таймер для сброса флага анимации
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_complete_callback);
    lv_anim_set_time(&a, ANIMATION_DURATION);
    lv_anim_set_values(&a, 0, 1);
    lv_anim_start(&a);
}

void anim_complete_callback(lv_anim_t* anim) {
    anim_in_progress = false;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting display with touch...");
    
    // Инициализация платы
    Board* board = new Board();
    board->init();
    
    if (!board->begin()) {
        Serial.println("Board initialization failed!");
        while(1) delay(1000);
    }
    Serial.println("Board initialized successfully");

    // Инициализация LVGL
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("LVGL initialization failed!");
        while(1) delay(1000);
    }
    Serial.println("LVGL initialized successfully");
    
    lvgl_port_lock(-1);

    // Инициализация стилей
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_16);
    
    lv_style_init(&style_normal);
    lv_style_set_text_color(&style_normal, lv_color_white());
    lv_style_set_text_font(&style_normal, &lv_font_montserrat_14);
    
    lv_style_init(&style_hint);
    lv_style_set_text_color(&style_hint, lv_color_hex(0x888888));
    lv_style_set_text_font(&style_hint, &lv_font_montserrat_12);

    // Создание экранов
    screens[0] = create_screen("Screen 1", "Temperature Scale", 0);
    screens[1] = create_screen("Screen 2", "Second screen\nDifferent text", 1);
    screens[2] = create_screen("Screen 3", "Third screen\nMore text here", 2);

    // Установка обработчиков событий
    for (auto screen : screens) {
        lv_obj_add_event_cb(screen, touch_event_handler, LV_EVENT_ALL, NULL);
    }

    lv_scr_load(screens[0]);
    lvgl_port_unlock();
    
    Serial.println("Setup completed. Swipe left/right to change screens");
}

void loop() {
    lv_timer_handler();
    delay(5);
}