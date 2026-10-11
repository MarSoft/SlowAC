#include <Arduino.h>

// Конфигурация пинов
const int pinIn = 0;       // Вход от датчика Холла мотора (PB0)
const int pinLed = 1;      // Встроенный светодиод-тахометр (PB1)
const int pinOut = 4;      // Выход частоты на плату кондиционера (PB4)

// Стартовая задержка (4 секунды)
const unsigned long start_delay = 4000; 

// Переменные для замера частоты
volatile unsigned long last_micros = 0;
volatile unsigned long real_period = 0;
volatile unsigned int pulse_count = 0;
volatile bool led_trigger = false;

unsigned long out_period = 0;
unsigned long last_out_toggle = 0;
bool out_state = false;
unsigned long boot_time = 0;

// Переменные для фильтрации частоты
float smoothed_freq = 45.0; 
const float alpha = 0.15; 

void onMotorPulse() {
  unsigned long current_micros = micros();
  unsigned long duration = current_micros - last_micros;
  
  if (duration > 2500) { 
    real_period = duration * 2; 
    last_micros = current_micros;
    
    pulse_count++;
    if (pulse_count >= 10) {
      pulse_count = 0;
      led_trigger = true; 
    }
  }
}

void setup() {
  boot_time = millis();
  
  pinMode(pinIn, INPUT_PULLUP);
  pinMode(pinLed, OUTPUT);
  pinMode(pinOut, OUTPUT);
  
  digitalWrite(pinLed, LOW);
  
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

  // Визуальный тахометр
  if (led_trigger) {
    led_trigger = false;
    digitalWrite(pinLed, !digitalRead(pinLed));
  }

  // Защита от полной остановки
  if (current_micros - last_micros > 250000) {
    real_period = 0;
    digitalWrite(pinLed, LOW); 
  }

  // Расчет выходного периода
  if (real_period == 0) {
    out_period = 0;
  } else if (current_millis - boot_time < start_delay) {
    out_period = real_period; // Старт 1 в 1
  } else {
    float real_freq = 1000000.0 / (float)real_period;
    
    // Сглаживаем частоту
    smoothed_freq = (alpha * real_freq) + ((1.0 - alpha) * smoothed_freq);
    
    // НЕЛИНЕЙНЫЙ АЛГОРИТМ:
    // На 45Гц (минимум) К = 1.85 (сильный обман)
    // На 80Гц (максимум) К = 1.15 (слабый обман)
    float K = 1.85 - ((smoothed_freq - 45.0) * (1.85 - 1.15) / (80.0 - 45.0));
    
    // Безопасные рамки для К
    if (K > 1.95) K = 1.95;
    if (K < 1.10) K = 1.10;
    
    out_period = (unsigned long)((float)real_period / K);
  }

  // Генерация меандра на P4
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

