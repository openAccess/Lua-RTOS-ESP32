/*
 * Lua RTOS, ADC internal driver
 *
 * Copyright (C) 2015 - 2017
 * IBEROXARXA SERVICIOS INTEGRALES, S.L.
 * 
 * Author: Jaume Olivé (jolive@iberoxarxa.com / jolive@whitecatboard.org)
 * 
 * All rights reserved.  
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#include "sdkconfig.h"

#include "esp_adc_cal.h"
#include "driver/adc.h"

#include <stdint.h>
#include <string.h>

#include <sys/driver.h>
#include <sys/syslog.h>

#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/adc_internal.h>

esp_adc_cal_characteristics_t characteristics;

/*
 * Helper functions
 */

// Get the pins used by an ADC channel
static void adc_pins(int8_t channel, uint8_t *pin) {
	switch (channel) {
		case 0: *pin = GPIO36; break;
		case 3: *pin = GPIO39; break;
		case 4: *pin = GPIO32; break;
		case 5: *pin = GPIO33; break;
		case 6: *pin = GPIO34; break;
		case 7: *pin = GPIO35; break;
	}
}

// Lock resources needed by ADC
static driver_error_t *adc_lock_resources(int8_t channel, void *resources) {
	adc_resources_t tmp_adc_resources;

	if (!resources) {
		resources = &tmp_adc_resources;
	}

	adc_resources_t *adc_resources = (adc_resources_t *)resources;
    driver_unit_lock_error_t *lock_error = NULL;

    adc_pins(channel, &adc_resources->pin);

    // Lock this pins
    if ((lock_error = driver_lock(ADC_DRIVER, channel, GPIO_DRIVER, adc_resources->pin, DRIVER_ALL_FLAGS, NULL))) {
    	// Revoked lock on pin
    	return driver_lock_error(ADC_DRIVER, lock_error);
    }

    return NULL;
}

/*
 * Operation functions
 *
 */

driver_error_t * adc_internal_pin_to_channel(uint8_t pin, uint8_t *chan) {
	switch (pin) {
		case GPIO36: *chan = 0; break;
		case GPIO39: *chan = 3; break;
		case GPIO32: *chan = 4; break;
		case GPIO33: *chan = 5; break;
		case GPIO34: *chan = 6; break;
		case GPIO35: *chan = 7; break;
		default:
			return driver_error(ADC_DRIVER, ADC_ERR_INVALID_PIN, NULL);
	}

	return NULL;
}

driver_error_t *adc_internal_setup(adc_chann_t *chan) {
	driver_error_t *error;
	adc_resources_t resources = {0};
	adc_atten_t atten;
	char attens[6];

	uint8_t unit = chan->unit;
	uint8_t channel = chan->channel;

	// Apply default max value
	if (chan->max == 0) {
		chan->max = 3900;
	}

	// Apply default resolution if needed
	if (chan->resolution == 0) {
		chan->resolution = 12;
	}

	// Sanity checks
	if ((chan->max < 0) || (chan->max > 3900)) {
		return driver_error(ADC_DRIVER, ADC_ERR_INVALID_MAX, NULL);
	}

	if ((chan->resolution != 9) && (chan->resolution != 10) && (chan->resolution != 11) && (chan->resolution != 12)) {
		return driver_error(ADC_DRIVER, ADC_ERR_INVALID_RESOLUTION, NULL);
	}

	if (chan->vref != 0) {
		return driver_error(ADC_DRIVER, ADC_ERR_VREF_SET_NOT_ALLOWED, NULL);
	}

	// Setup

	// Lock the resources needed
	if ((error = adc_lock_resources(channel, &resources))) {
		return error;
	}

	// Computes the required attenuation
	if (chan->max <= 1100) {
		atten = ADC_ATTEN_0db;
		strcpy(attens, "0db");
	} else if (chan->max <= 1500) {
		atten = ADC_ATTEN_2_5db;
		strcpy(attens, "2.5db");
	} else if (chan->max <= 2200) {
		atten = ADC_ATTEN_6db;
		strcpy(attens, "6db");
	} else {
		atten = ADC_ATTEN_11db;
		strcpy(attens, "11db");
	}

	adc1_config_channel_atten(channel, atten);

	// Configure all channels with the given resolution
	adc1_config_width(chan->resolution - 9);

	// Get characteristics
	esp_adc_cal_get_characteristics(CONFIG_ADC_INTERNAL_VREF, atten, chan->resolution - 9, &characteristics);

	if (!chan->setup) {
		syslog(
				LOG_INFO,
				"adc%d: at pin %s%d, attenuation %s, %d bits of resolution", unit, gpio_portname(resources.pin),
				gpio_name(resources.pin), attens, chan->resolution
		);
	}

	return NULL;
}

driver_error_t *adc_internal_read(adc_chann_t *chan, int *raw, double *mvolts) {
	uint8_t channel = chan->channel;

	int traw = adc1_get_raw(channel);
	double tmvolts = esp_adc_cal_raw_to_voltage(traw, &characteristics);

	if (raw) {
		*raw = traw;
	}

	if (mvolts) {
		*mvolts = tmvolts;
	}

	return NULL;
}
