/*
 * display.c
 *
 *  Created on: 15 gru 2015
 *      Author: Konrad
 */


#include "display.h"
#include "spi.h"
#include "hardware_settings.h"
#include "nrf_gpio.h"
#include "nrf_soc.h"
#include "nrf51.h"
#include "stddef.h"
#include "stdint.h"
#include "RTC.h"
#include "sharp_display_font.h"
#include "stdlib.h"
#include "GPS.h"
#include "ble_gwatch.h"
#include "ble_types.h"


/* Structure : first byte - command
 * 				line 1    -	line_number, data,
 * 				line 2	  -	line_number, data
 * 					.
 * 					.
 * 					.
 */
uint8_t 			display_array[96*14] = {0};
volatile uint8_t 	disp_updt_time = 0;
extern uint8_t 		battery_level;

/**
 * \brief This function configures the SPI module for communication with Sharp 96x96 memory display
 */
static void Display_SPI_Config()
{
	nrf_gpio_cfg_output(DISP_CS);
	nrf_gpio_cfg_output(DISP_MOSI);
	nrf_gpio_cfg_output(DISP_SCK);

	NRF_SPI1->PSELSCK = DISP_SCK;
	NRF_SPI1->PSELMOSI = DISP_MOSI;

	///	Enable the SPI Interrupt
	NRF_SPI1->INTENSET = SPI_INTENSET_READY_Msk;

	///	Clear configuration
	NRF_SPI1->CONFIG = 0;

	///	Set the CPHA0 and CPOL0 and MSB bit first
	NRF_SPI1->CONFIG = (SPI_CONFIG_CPHA_Leading << SPI_CONFIG_CPHA_Pos) | (SPI_CONFIG_CPOL_ActiveHigh << SPI_CONFIG_CPOL_Pos) | (SPI_CONFIG_ORDER_LsbFirst << SPI_CONFIG_ORDER_Pos);

	///	Set the Display SPI CLK freqency to 0.5 MHz
	NRF_SPI1->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M1;
}


/**
 * \brief This function configures the EXTCOMIN pin to generate 50% duty cycle PWM signal needed to refresh the LCD Sharp Memory Display
 */
static void Sharp_VCOM_Config()
{
	nrf_gpio_cfg_output(DISP_TOGGLE_PIN);
	///	Set the pin in gpiote mode
	NRF_GPIOTE->CONFIG[0] = (GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos) | (GPIOTE_CONFIG_POLARITY_Toggle << GPIOTE_CONFIG_POLARITY_Pos) | (GPIOTE_CONFIG_OUTINIT_Low << GPIOTE_CONFIG_OUTINIT_Pos);
	/// Set the pin
	NRF_GPIOTE->CONFIG[0] |= DISP_TOGGLE_PIN << GPIOTE_CONFIG_PSEL_Pos;
	///	Configure the PPI channel
	uint32_t err_code = sd_ppi_channel_assign(0, &NRF_RTC1->EVENTS_COMPARE[3], &NRF_GPIOTE->TASKS_OUT[0]);
	///	Enable the PPI channel
	err_code = sd_ppi_channel_enable_set(1);

	///	Set the value to RTC CC which triggers VCOM signal toggling (16Hz)
	NRF_RTC1->CC[3] = NRF_RTC1->COUNTER + 1024;
	///	Enable the RTC CC which triggers VCOM signal toggling
	NRF_RTC1->EVTENSET = RTC_EVTENSET_COMPARE3_Msk;

	NRF_RTC1->INTENSET = RTC_INTENSET_COMPARE3_Msk;
}

void Display_Config()
{
	Display_SPI_Config();
	Sharp_VCOM_Config();
	for(uint8_t i=0; i<96; i++)
	{
		display_array[i*14] = i+1;
		display_array[i*14 + 13] = 0;
		for(uint8_t j=1; j < 13; j++)
			display_array[i*14 + j] = 0x00;
	}

	Display_Clear();
	RTC_Wait(RTC_MS_TO_TICKS(5));
	Display_Flush_Buffer();

	uint8_t text[] = "SAMPLING";
	Display_Write_Buffer(text, 8, DISPLAY_SAMPLING_STATUS_LINE, 0, true);

}

/**
 * \brief This function writes single line from the display_array buffer to the display
 *
 * \param line_number - the line number from (0 to 95)
 */
void Display_Write_Line(uint8_t line_number)
{
	static uint8_t* line_buffer = NULL;

	line_buffer = malloc(16);

	///	Copy the write command
	line_buffer[0] = SHARP_WRITE_LINE_CMD;
	///	Copy the line number and pixel data
	memcpy(&line_buffer[1], &display_array[line_number*14], 13);
	line_buffer[14] = 0;
	line_buffer[15] = 0;
	SPI_Transfer_Non_Blocking(NRF_SPI1, line_buffer, 16, NULL, 0, DISP_CS, true);
	//SPI_Transfer_Blocking(NRF_SPI1, line_buffer, 16, NULL, 0, DISP_CS);
}

/**
 * \brief This function writes more than one line from the display_array buffer to the display
 *
 * \param start_line - the start line index (0-95)
 * \param end)line - the end line index(start_line - 95)
 */
__attribute__((optimize("O2")))
void Display_Write_Consecutive_Lines(uint8_t start_line, uint8_t end_line)
{
	uint8_t cmd = SHARP_WRITA_MULTIPLE_LINES_CMD;

	NRF_GPIO->OUTSET = 1 << DISP_CS;
	SPI_Transfer_Non_Blocking(NRF_SPI1, &cmd, sizeof(cmd), NULL, 0, SPI_CS_MANUALLY_CHANGED, false);

	SPI_Wait_For_Transmission_End(NRF_SPI1);

	SPI_Transfer_Non_Blocking(NRF_SPI1, &display_array[start_line*14], (end_line - start_line)*14, NULL, 0, DISP_CS, false);
	SPI_Wait_For_Transmission_End(NRF_SPI1);
}

/**
 * \brief This function clears entire display and sets it backgrount to white
 */
void Display_Clear()
{
	uint8_t* ptr;
	ptr = malloc(2);
	ptr[0] = SHARP_CLEAR_SCREEN;
	ptr[1] = 0;
	SPI_Transfer_Non_Blocking(NRF_SPI1, ptr, 2, NULL, 0, DISP_CS, true);
	SPI_Wait_For_Transmission_End(NRF_SPI1);
}

/**
 * \brief This function writes the given text directly to the display
 * \param text - the text to be written
 * \param text_size - size of the text to write
 * \param line_number - the line where the text should be put (0 - 95)
 * \param char_index - the index of the character on the display from which the text should be displayed (0 - 11)
 * \param inverted - 1 if white text on black background, 0 otherwise
 * \param dyn_allocated - 1 if the buffer should be freed, 0 - if it lies on stack
 */
__attribute__((optimize("O2")))
void Display_Write_Text(uint8_t* text, uint8_t text_size, uint8_t line_number, uint8_t char_index, bool inverted, bool dyn_alloc_buf)
{
	Display_Write_Buffer(text, text_size, line_number, char_index, inverted);

	if(dyn_alloc_buf == true)
		free(text);

	Display_Write_Consecutive_Lines(line_number, line_number + 8);
}

/**
 * \brief This function writes the given data to the display buffer
 *
 * \param text - the text to be written
 * \param text_size - size of the text to write
 * \param line_number - the line where the text should be put (0 - 95)
 * \param char_index - the index of the character on the display from which the text should be displayed (0 - 11)
 * \param inverted - 1 if white text on black background, 0 otherwise
 */
__attribute__((optimize("O2")))
void Display_Write_Buffer(uint8_t* text, uint8_t text_size, uint8_t line_number, uint8_t char_index, bool inverted)
{
	uint8_t last_char_index = char_index + text_size;
	if(inverted != false)
	{
		for(uint8_t i=0; i< text_size; i++)
		{
			for(uint8_t char_line = 0; char_line < 8; char_line++)
				display_array[(char_line + line_number)*14 + i + 1 + char_index] = font8x8[(text[i] - 32)*8 + char_line];
		}
	}
	else
	{
		for(uint8_t i=0; i< text_size; i++)
		{
			for(uint8_t char_line = 0; char_line < 8; char_line++)
				display_array[(char_line + line_number)*14 + i + 1 + char_index] = font8x8_inverted[(text[i] - 32)*8 + char_line];
		}
	}
}

/**
 * \brief This function writes the current time into the display buffer
 *
 */
__attribute__((optimize("O2")))
void Display_Write_Time()
{
	uint32_t timestamp = RTC_Get_Timestamp();

	uint8_t seconds = timestamp % 60;
	uint8_t mins = (timestamp % 3600) / 60;
	uint8_t hour = (timestamp % 86400) / 3600;

	uint8_t text[8] = {'0', '0', ':', '0', '0', ':', '0', '0'};
	//uint8_t* text = malloc(8);
	if(hour < 10)
		sprintf(text+1, "%d", hour);
	else
		sprintf(text, "%d", hour);
	if(mins < 10)
		sprintf(text+4, "%d", mins);
	else
		sprintf(text+3, "%d", mins);
	if(seconds < 10)
		sprintf(text+7, "%d", seconds);
	else
		sprintf(text+6, "%d", seconds);

	text[2] = ':';
	text[5] = ':';
	Display_Write_Buffer(text, 8, DISPLAY_CLOCK_START_LINE, 2, true);
}

/**
 * \brief This function writes the entire display_array buffer to the display
 */
inline void Display_Flush_Buffer()
{
	Display_Write_Consecutive_Lines(0, 95);
}

/**
 * \brief This function writes the latitude to the latitude area in the display_array buffer
 */
void Display_Write_Latitude()
{
	uint8_t text[12] = {'X', 'X', 'X', '*', 'X', 'X', '.', 'X', 'X', 'X', 'X', '\''};
	uint8_t lat_indi = 'X';
	if(gga_message.fix_indi != 0 && gga_message.fix_indi != '0' && gps_is_powered_on)
	{
		memcpy(text, gga_message.latitude.deg, 3);
		memcpy(text + 4, gga_message.latitude.min_int, 2);
		memcpy(text + 7, gga_message.latitude.min_fract, 4);
		lat_indi = gga_message.latitude_indi;
	}

	Display_Write_Buffer(&"LATITUDE:", 9, DISPLAY_LATITUDE_DESC_START_LINE, 0, true);
	Display_Write_Buffer(text, sizeof(text), DISPLAY_LATITUDE_START_LINE, 0, true);
	Display_Write_Buffer(&lat_indi, 1, DISPLAY_LATITUDE_START_LINE+8, 11, true);


}

/**
 * \brief This function writes the longtitude to the longtitude area in the display_array buffer
 */
void Display_Write_Longtitude()
{
	uint8_t text[12] = {'X', 'X', 'X', '*', 'X', 'X', '.', 'X', 'X', 'X', 'X', '\''};
	uint8_t long_indi = 'X';
	if(gga_message.fix_indi != 0 && gga_message.fix_indi != '0' && gps_is_powered_on)
	{
		memcpy(text, gga_message.longtitude.deg, 3);
		memcpy(text + 4, gga_message.longtitude.min_int, 2);
		memcpy(text + 7, gga_message.longtitude.min_fract, 4);
		long_indi = gga_message.longtitude_indi;
	}

	Display_Write_Buffer(&"LONGTITUDE:", 11, DISPLAY_LONGTITUDE_DESC_START_LINE, 0, true);
	Display_Write_Buffer(text, sizeof(text), DISPLAY_LONGTITUDE_START_LINE, 0, true);
	Display_Write_Buffer(&long_indi, 1, DISPLAY_LONGTITUDE_START_LINE+8, 11, true);
}

/**
 * \brief This function writes the indicator to the display whether the device is in BLE connection state or not
 */
void Display_Update_BLE_Conn(uint16_t ble_conn_status)
{
	uint8_t data = ' ';
	if(ble_conn_status != BLE_CONN_HANDLE_INVALID)
	{
		data = 'B';
	}
	else
	{
		data = ' ';
	}
	Display_Write_Buffer(&data, 1, 1, 0, true);
}

/**
 * \brief This function writes the indicator to the display whether the GPS module is powered on or not
 */
void Display_Update_GPS_Power_On()
{
	static uint8_t cnt = 0;
	uint8_t data[] = "   ";

	if(gps_is_powered_on)
	{
		memcpy(data, &"GPS", 3);

		if(gga_message.fix_indi == 0 || gga_message.fix_indi == '0')
		{
			if(++cnt % 2)
			{
				memcpy(data, &"   ", 3);
			}
		}
	}
	Display_Write_Buffer(data, 3, 1,  4, true);
}

/**
 * \brief This function writes the indicator to the display whether the devices is sampling or not
 */
void Display_Update_Sampling_Status(bool sampling_started)
{
    uint8_t text[] = "OFF";
    if(sampling_started)
    {
    	memcpy(text, &" ON", 3);
    	Display_Write_Buffer(text, 3, DISPLAY_SAMPLING_STATUS_LINE, 9, true);
    }
    else
    {
    	Display_Write_Buffer(text, 3, DISPLAY_SAMPLING_STATUS_LINE, 9, true);
    }
}

/**
 * \brief This function writes the indicator of battery level
 */
void Display_Update_Battery_Level()
{
	static uint8_t previous_level = 100;

	if(previous_level != battery_level)
	{
		uint8_t data[4] = {' ', ' ', ' ', '%'};
		if(battery_level == 100)
			sprintf(data, "%d", battery_level);
		else
		if(battery_level > 9)
			sprintf(data + 1, "%d", battery_level);
		else
			sprintf(data + 2, "%d", battery_level);
		data[3] = '%';
		Display_Write_Buffer(data, sizeof(data), 1, 8, true);
	}
}


void Display_Test()
{
	Display_Clear();
		uint8_t byte = 0x0F;
		//Display_
		for(uint8_t i=0; i<96; i++)
		{
			display_array[i*14] = i+1;
			for(uint8_t j = 1; j<13; j++)
				display_array[ i*14+ j] = byte;
			display_array[i*14 + 13] = 0;
		}

		Display_Write_Consecutive_Lines(0, 95);
		SPI_Wait_For_Transmission_End(NRF_SPI1);
		RTC_Wait(RTC_S_TO_TICKS(3));
		Display_Clear();
		uint8_t* ptr = malloc(11);
		memcpy(ptr, &"HELLO WORLD", 11);
		Display_Write_Text(ptr, 11, 8, 0, false, true);
		SPI_Wait_For_Transmission_End(NRF_SPI1);
		RTC_Wait(RTC_S_TO_TICKS(3));

		/*	for(uint8_t i=0; i<96;i++)
			{
				Display_Write_Line(i);
				RTC_Wait(RTC_MS_TO_TICKS(1));
			}*/

}
