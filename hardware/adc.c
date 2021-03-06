#include <adc.h>
#include <nrf_soc.h>
#include <nrf51.h>
#include <nrf51_bitfields.h>
#include <stdbool.h>
#include "nrf_soc.h"
#include <sys/_stdint.h>
#include "RTC.h"
#include "hardware_settings.h"
#include "nrf_gpio.h"

static uint8_t*		vdd_voltage;
volatile uint8_t   	adc_conversion_in_progress = 0;


void ADC_IRQHandler()
{
	RTC_Cancel_Timeout();
	///	Clear event flag
	NRF_ADC->EVENTS_END = 0;
	///	Get the converted voltage
	*vdd_voltage = NRF_ADC->RESULT;

	
	adc_conversion_in_progress = 0;
}


void ADC_Init()
{
	nrf_gpio_cfg_input(ADC_PIN, NRF_GPIO_PIN_NOPULL);
	///	Reset to defdault value
	NRF_ADC->CONFIG = 0;
	///	Set the resolution to 8 bit. 3V (max voltage) * 1/(2^8) =~ 0,0117 V 
	NRF_ADC->CONFIG |= ADC_CONFIG_RES_8bit << ADC_CONFIG_RES_Pos;
	///	Max input voltage after prescaling for the internal ADC module is 1.2V, so we have to use 1/3 VDD prescaler
	NRF_ADC->CONFIG |= ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos;
	///	Use internal reference voltage (because we do not possess any other)
	NRF_ADC->CONFIG |= ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos;
	///	We want to measeure only VDD voltage so we do not need any AIN pin
	NRF_ADC->CONFIG |= ADC_CONFIG_PSEL_AnalogInput7 << ADC_CONFIG_PSEL_Pos;
	///	We use internal reference voltage (VBG) so we do not need external reference voltage source pin
	NRF_ADC->CONFIG &= ~ADC_CONFIG_EXTREFSEL_Msk;
	
	sd_nvic_ClearPendingIRQ(ADC_IRQn);
}


void ADC_Get_Bat_Voltage(uint8_t* buffer)
{
	vdd_voltage = buffer;
	///	Enable the Conversion End interrupt
	NRF_ADC->INTENSET = ADC_INTENSET_END_Msk;
	///	Enable the peripheral
	NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Msk;
	///	Start conversion
	NRF_ADC->TASKS_START = 1;
	adc_conversion_in_progress = 1;

	RTC_Timeout(RTC_MS_TO_TICKS(100));
	///	Sleep until conversion hasn't ended
	while(adc_conversion_in_progress && !timeout_flag)
	{
		__WFE();
	}
	if(timeout_flag)
	{
		RTC_Cancel_Timeout();
		timeout_flag = 0;
		adc_conversion_in_progress = 0; /// Clear the flag in case timeout has been triggered
	}
	///	Disable the peripheral
	NRF_ADC->ENABLE = 0;
}

