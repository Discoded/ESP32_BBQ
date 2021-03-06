#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>

// SPI
#include "driver/spi_slave.h"
#define GPIO_CS   26 // 05 //c I
#define GPIO_SCLK 18 // 18 //    I
#define GPIO_MOSI NULL // Not used 
#define GPIO_MISO 12 // Re-mapped from GPIO19, see 4.10 IO_MUX Pad List - Table 4-3

#define RCV_HOST    VSPI_HOST
#define DMA_CHAN    2

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

//Use adc2_vref_to_gpio() to obtain a better estimate
#define DEFAULT_VREF    1100        
//Multisampling
#define NO_OF_SAMPLES   64          

// ADC Constants
static esp_adc_cal_characteristics_t *adc_chars;
//GPIO34 if ADC1, GPIO14 if ADC2
static const adc2_channel_t channel = ADC2_CHANNEL_4;    
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_2;
uint32_t adc_reading = 0;

// Steinhart-Hart Constants
#define A 0.9243479102e-3
#define B 1.814402510e-4
#define C 2.954868853e-7
#define R2 5040
#define V_IN 3.3
// Kelvin to Celsius conversion
#define K_TO_C 273.15
float temperature_celsius;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST, 100000UL, 100000UL);
int counter = 0;

// SPI
uint32_t sendbuf[1];
uint32_t recvbuf[1];
spi_slave_transaction_t t;
esp_err_t ret;
int n=0;

void setup() 
{
  Serial.begin(115200);
  SetupOLED();
  SetupADC();
  SetupSPI();
  vTaskDelay(2000 / portTICK_RATE_MS);
  xTaskCreate(&TaskPrintLCD, "Hello_Task", 4096, NULL, 3, NULL);
  xTaskCreate(&TaskADC, "ADC_Task", 4096, NULL, 2, NULL);
  xTaskCreate(&TaskSPI, "SPI_Task", 8192, NULL, 1, NULL);

}

void loop() 
{
    
}

void ConvertValue(uint32_t v_out)
{
  float thermistor_resistance = R2*((V_IN*1000/v_out)-1);
  temperature_celsius = 1/(A+B*log(thermistor_resistance)+C*pow(log(thermistor_resistance), 3)) - K_TO_C;
  //printf("%d, %.2f, %.2f, %d\n", v_out, thermistor_resistance, temperature_celsius,(uint32_t)(temperature_celsius*100));
  sendbuf[0]=(uint32_t)(temperature_celsius*100);
}


void SetupADC()
{
  adc2_config_channel_atten((adc2_channel_t)channel, atten);
  //Characterize ADC
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
      printf("Characterized using Two Point Value\n");
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
      printf("Characterized using eFuse Vref\n");
  } else {
      printf("Characterized using Default Vref\n");
  }
}

void SetupOLED()
{
  //reset OLED display via software
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  //initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  // Address 0x3C for 128x32
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { 
    Serial.println(F("SSD1306 allocation failed"));
    // Don't proceed, loop forever
    for(;;); 
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0,0);
  for (int i=0; i< 10; i++)
  {
    display.print((char)i);
  }
  display.display();
  vTaskDelay(500 / portTICK_RATE_MS);
}

void SetupSPI()
{
  /* // Enable Pin Muxing to map the peripheral to their non-regular pins
  gpio_iomux_in(GPIO_MOSI, VSPID_IN_IDX);
  gpio_iomux_out(GPIO_MISO, FUNC_GPIO19_VSPIQ, false);
  gpio_iomux_in(GPIO_SCLK, VSPICLK_IN_IDX);
  gpio_iomux_in(GPIO_CS, VSPICS0_IN_IDX);
 */

  spi_bus_config_t buscfg={
      .mosi_io_num=GPIO_MOSI,
      .miso_io_num=GPIO_MISO,
      .sclk_io_num=GPIO_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 16,
  };

  spi_slave_interface_config_t slvcfg={
    .spics_io_num=GPIO_CS,
    .flags=0,
    .queue_size=3,
    .mode=1
  };

  // MISO - Master In Slave Out
  /* PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_MISO], PIN_FUNC_GPIO);
  gpio_set_direction((gpio_num_t)GPIO_MISO, GPIO_MODE_INPUT_OUTPUT);
  gpio_iomux_out(GPIO_MISO, HSPIQ_OUT_IDX, 0);
  gpio_iomux_in(GPIO_MISO, HSPIQ_IN_IDX); */

  //Initialize SPI slave interface
  ret=spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, DMA_CHAN);
  printf("ret: %s\n", esp_err_to_name(ret));
  assert(ret==ESP_OK);

  //Set up a transaction of 128 bytes to send/receive
  t.length=32;
  t.trans_len=32;
  t.tx_buffer=sendbuf;
  t.rx_buffer=recvbuf;
}

void TaskADC(void *pvParameter)
{
  
  while(1)
  {
    //Multisampling
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        int raw;
        adc2_get_raw((adc2_channel_t)channel, width, &raw);
        //raw = adc1_get_raw(channel);
        adc_reading += raw;
    }
    adc_reading /= NO_OF_SAMPLES;
    //Convert adc_reading to voltage in mV
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
    
    ConvertValue(voltage);
    vTaskDelay(500 / portTICK_RATE_MS);
  }
}

void TaskPrintLCD(void *pvParameter)
{
  while(1)
  {
    char degree = 9;
    //printf("TaskPrintLCD called\n");
    display.clearDisplay();
    display.setCursor(0, 5);
    display.print(temperature_celsius);
    display.drawCircle(97, 5, 4, 1);
    display.setCursor(105, 5);
    display.print("C");
    display.display();
    vTaskDelay(2000 / portTICK_RATE_MS);
  }
}

void TaskSPI(void *pvParameter)
{
  while(1)
  {
    sendbuf[0]=(uint32_t)(temperature_celsius*100);
    printf("sendbuf: %d ", sendbuf[0]);
    printf("Calling spi_slave_transmit\n");
    ret=spi_slave_transmit(RCV_HOST, &t, portMAX_DELAY);
    printf("Error: %s ",  esp_err_to_name(ret));
    assert( ret == ESP_OK);
    
    printf("n: %d\n", n);
    n++;
    printf("TaskSPI called\n");
    //vTaskDelay(2000 / portTICK_RATE_MS);
  }
}