#include <Arduino.h>

// Конфигурация пинов
const int pinIn = 0;       // Вход от датчика Холла мотора (PB0)
const int pinLed = 1;      // Встроенный светодиод-тахометр (PB1)
const int pinOut = 4;      // Выход частоты на плату кондиционера (PB4)

// Параметры алгоритма
const float K_target = 1.6;        // Максимальный коэффициент замедления
const float safety_minimum = 28.0; // Частота (Гц), ниже которой мотор может встать
const float safety_zone = 35.0;    // Частота (Гц), где начинаем снижать "обман"
const unsigned long start_delay = 4000; // Стартовая задержка (4 секунды)

// Переменные для замера частоты
volatile unsigned long last_micros = 0;
volatile unsigned long real_period = 0;
volatile unsigned int pulse_count = 0;
volatile bool led_trigger = false;

unsigned long out_period = 0;
unsigned long last_out_toggle = 0;
bool out_state = false;
unsigned long boot_time = 0;

// Переменные для фильтрации (сглаживания) частоты
float smoothed_freq = 45.0; 
const float alpha = 0.15; // Коэффициент фильтрации (чем меньше, тем плавнее)

// Обработчик прерывания по изменению уровня на входе мотора (PB0)
void onMotorPulse() {
  unsigned long current_micros = micros();
  unsigned long duration = current_micros - last_micros;
  
  if (duration > 2500) { // Защита от дребезга контактов (>200 Гц)
    real_period = duration * 2; // Полный период меандра
    last_micros = current_micros;
    
    // Считаем импульсы для визуального тахометры
    pulse_count++;
    if (pulse_count >= 10) {
      pulse_count = 0;
      led_trigger = true; // Сигнал для loop() переключить светодиод
    }
  }
}

void setup() {
  boot_time = millis();
  
  pinMode(pinIn, INPUT_PULLUP);
  pinMode(pinLed, OUTPUT);
  pinMode(pinOut, OUTPUT);
  
  digitalWrite(pinLed, LOW);
  
  // Настройка прерываний PCINT0 на пине PB0
  GIMSK |= (1 << PCIE);   
  PCMSK |= (1 << PCINT0); 
  sei();                  
}

ISR(PCINT0_vect) {
  onMotorPulse();
}

void loop() {
  unsigned long current_millis = millis();
  unsigned long current_micros = micros();

  // 1. Аппаратный визуальный тахометр (каждый 10-й импульс зажигает/гасит LED)
  if (led_trigger) {
    led_trigger = false;
    digitalWrite(pinLed, !digitalRead(pinLed));
  }

  // Защита от полной остановки (если импульсов нет дольше 250 мс)
  if (current_micros - last_micros > 250000) {
    real_period = 0;
    digitalWrite(pinLed, LOW); // Гасим светодиод, если мотор стоит
  }

  // 2. Расчет текущей частоты и коэффициента
  if (real_period == 0) {
    out_period = 0;
  } else if (current_millis - boot_time < start_delay) {
    // Период старта: транслируем частоту 1 в 1
    out_period = real_period;
  } else {
    // Рабочий режим: считаем реальную частоту в Гц
    float real_freq = 1000000.0 / (float)real_period;
    
    // Сглаживаем частоту экспоненциальным фильтром
    smoothed_freq = (alpha * real_freq) + ((1.0 - alpha) * smoothed_freq);
    
    // Реализация ВАШЕГО адаптивного алгоритма защиты
    float K = K_target;
    if (smoothed_freq < safety_zone) {
      float factor = (smoothed_freq - safety_minimum) / (safety_zone - safety_minimum);
      if (factor < 0.0) factor = 0.0;
      K = 1.0 + (K_target - 1.0) * factor; // Плавно снижаем K вплоть до 1.0
    }
    
    // Рассчитываем выходной измененный период для платы
    out_period = (unsigned long)((float)real_period / K);
  }

  // 3. Безиндукционная генерация меандра на пине P4
  if (out_period > 0) {
    unsigned long half_period = out_period / 2;
    if (current_micros - last_out_toggle >= half_period) {
      out_state = !out_state;
      digitalWrite(pinOut, out_state);
      last_out_toggle = current_micros;
    }
  } else {
    digitalWrite(pinOut, LOW);
  }
}

