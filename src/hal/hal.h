/*******************************************************************************
 * Copyright (c) 2015 Matthijs Kooijman
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * This the HAL to run LMIC on top of the Arduino environment.
 *******************************************************************************/
#ifndef _hal_hal_h_
#define _hal_hal_h_

#include <stdbool.h>
#include <stdint.h>

#include "../lmic/osticks.h"

/*
 * initialize hardware (IO, SPI, TIMER, IRQ).
 */
void hal_init(void);

/*
 * disable all CPU interrupts for the current scope.
 * might be invoked nested.
 */
class DisableIRQsGard {
  private:
    uint8_t sreg_save;
public:
  DisableIRQsGard();
  ~DisableIRQsGard();
};

/*
 * return system time.
 */
OsTime hal_ticks();

#ifndef ARDUINO_ARCH_ESP32
void hal_add_time_in_sleep(OsDeltaTime nb_tick);
#endif

/*
 * busy-wait until specified timestamp is reached.
 */
void hal_waitUntil(OsTime time);

/*
 * wait this interval.
 */
void hal_wait(OsDeltaTime time);

/*
 * check and rewind timer for target time.
 *   - return 1 if target time is close
 *   - otherwise rewind timer for target time or full period and return 0
 */
bool hal_checkTimer(OsTime targettime);

/*
 * perform fatal failure action.
 *   - called by assertions
 *   - action could be HALT or reboot
 */
void hal_failed(const char *file, uint16_t line);

void hal_store_trigger();

#endif // _hal_hal_h_
