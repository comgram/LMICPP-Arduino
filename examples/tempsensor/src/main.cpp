
#include <Arduino.h>
#include <SPI.h>

#include <lmic.h>
#include <keyhandler.h>
#include <hal/hal_io.h>
#include <hal/print_debug.h>

#include <sleepandwatchdog.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#define DEVICE_BALISE2
#include "lorakeys.h"

void do_send();
void begin_read();
void powersave(OsDeltaTime maxTime);

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
constexpr OsDeltaTime TX_INTERVAL = OsDeltaTime::from_sec(180);

constexpr unsigned int BAUDRATE = 19200;
constexpr uint8_t button_pin = 3;
// Pin mapping
constexpr lmic_pinmap lmic_pins = {
    .nss = 10,
    .prepare_antenna_tx = nullptr,
    .rst = 14,
    .dio = {9, 8},
};
OsScheduler OSS;
RadioSx1276 radio{lmic_pins};
LmicEu868 LMIC{radio, OSS};

OsJob sendjob{OSS};

bool new_click = false;
bool send_now = false;

// Data wire is plugged into this port
constexpr uint8_t const one_wire_bus = 4;

// Setup a oneWire instance
OneWire oneWire(one_wire_bus);

// Temperature sensors manager
DallasTemperature temps_sensors(&oneWire);

void onEvent(EventType ev)
{
    rst_wdt();
    switch (ev)
    {
    case EventType::JOINING:
        PRINT_DEBUG(2, F("EV_JOINING"));
        //        LMIC.setDrJoin(0);
        break;
    case EventType::JOINED:
        PRINT_DEBUG(2, F("EV_JOINED"));
        LMIC.setDutyRate(12);
        break;
    case EventType::JOIN_FAILED:
        PRINT_DEBUG(2, F("EV_JOIN_FAILED"));
        break;
    case EventType::TXCOMPLETE:
        PRINT_DEBUG(2, F("EV_TXCOMPLETE (includes waiting for RX windows)"));
        send_now = false;
        // we have transmit
        // Schedule next transmission
        sendjob.setTimedCallback(os_getTime() + TX_INTERVAL, begin_read);

        break;
    case EventType::RESET:
        PRINT_DEBUG(2, F("EV_RESET"));
        break;
    case EventType::LINK_DEAD:
        PRINT_DEBUG(2, F("EV_LINK_DEAD"));
        break;
    case EventType::LINK_ALIVE:
        PRINT_DEBUG(2, F("EV_LINK_ALIVE"));
        break;
    default:
        PRINT_DEBUG(2, F("Unknown event"));
        break;
    }
}

void begin_read()
{
    temps_sensors.requestTemperatures();
    sendjob.setTimedCallback(
        os_getTime() + OsDeltaTime::from_ms(temps_sensors.millisToWaitForConversion(temps_sensors.getResolution())),
        do_send);
}

void do_send()
{

    // Check if there is not a current TX/RX job running
    if (LMIC.getOpMode().test(OpState::TXRXPEND))
    {
        PRINT_DEBUG(1, F("OpState::TXRXPEND, not sending"));
        // should not happen so reschedule anymway
        sendjob.setTimedCallback(os_getTime() + TX_INTERVAL, do_send);
    }
    else
    {
        // TODO check buffer is small enought for LMIC.
        uint8_t buffsize = 1 + 2 * temps_sensors.getDeviceCount();
        uint8_t rawbuff[buffsize];

        // battery
        rawbuff[0] = ((uint32_t)analogRead(A1)) * 255 / 683;

        auto temp_buff = reinterpret_cast<uint16_t *>(rawbuff + 1);
        for (int i = 0; i < temps_sensors.getDeviceCount(); i++)
        {

            DeviceAddress deviceAddress;
            if (!temps_sensors.getAddress(deviceAddress, i))
            {
                temp_buff[i] = DEVICE_DISCONNECTED_C;
            }
            else
            {
                temp_buff[i] = temps_sensors.getTemp(deviceAddress);
            }
        }

        // Prepare upstream data transmission at the next possible time.
        LMIC.setTxData2(4, rawbuff, buffsize, false);
        PRINT_DEBUG(1, F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

// lmic_pins.dio[0]  = 9 => PCINT1
// lmic_pins.dio[1]  = 8 => PCINT0
// PCI2 PCINT[23:16]
// PCI1 PCINT[14:8]
// PCI0 PCINT[7:0]

ISR(PCINT0_vect)
{
    // one of pins D8 to D13 has changed
    // store time, will be check in OSS.runloopOnce()
    LMIC.store_trigger();
}

void pciSetup(byte pin)
{
    *digitalPinToPCMSK(pin) |= bit(digitalPinToPCMSKbit(pin)); // enable pin
    PCIFR |= bit(digitalPinToPCICRbit(pin));                   // clear any outstanding interrupt
    PCICR |= bit(digitalPinToPCICRbit(pin));                   // enable interrupt for the group
}

void testDuration(int32_t ms)
{
    const auto delta = OsDeltaTime::from_ms(ms);
    PRINT_DEBUG(1, F("Test sleep time for %i ms."), ms);
    const OsTime start = os_getTime();
    PRINT_DEBUG(1, F("Start Test sleep time."));
    powersave(delta);
    const OsTime end = os_getTime();
    PRINT_DEBUG(1, F("End Test sleep time."));
    PRINT_DEBUG(1, F("Test Time should be : %d ms"), (end - start).to_ms());
}

void buttonInterupt()
{
    // Do nothing if send is already scheduled.
    if (send_now)
    {
        return;
    }
    if (digitalRead(button_pin) == 0)
    {
        new_click = true;
    }
}

void setup()
{
    if (debugLevel > 0)
    {
        Serial.begin(BAUDRATE);
    }
    pciSetup(lmic_pins.dio[0]);
    pciSetup(lmic_pins.dio[1]);

    pinMode(button_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(button_pin), &buttonInterupt, FALLING);

    SPI.begin();
    // LMIC init
    os_init();
    LMIC.init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC.reset();

    LMIC.setEventCallBack(onEvent);

    SetupLmicKey<appEui, devEui, appKey>::setup(LMIC);

    // set clock error to allow good connection.
    LMIC.setClockError(MAX_CLOCK_ERROR * 3 / 100);
    // LIMIT power consumtion
    LMIC.setAntennaPowerAdjustment(-14);

    // Only work with special boot loader.
    configure_wdt();

    // test duration and in case of reboot loop  prevent flood
    testDuration(1000);
    testDuration(8000);
    testDuration(30000);

    // setup onwire sensor
    temps_sensors.begin();
    temps_sensors.setWaitForConversion(false);

    // Start job (sending automatically starts OTAA too)
    sendjob.setCallbackRunnable(begin_read);
}

const int64_t sleepAdj = 1080;

void powersave(OsDeltaTime maxTime)
{
    OsDeltaTime duration_selected;
    Sleep period_selected;
    // these value are base on test
    if (maxTime > OsDeltaTime::from_ms(8700))
    {
        duration_selected = OsDeltaTime::from_ms(8000 * sleepAdj / 1000);
        period_selected = Sleep::P8S;
    }
    else if (maxTime > OsDeltaTime::from_ms(4600))
    {
        duration_selected = OsDeltaTime::from_ms(4000 * sleepAdj / 1000);
        period_selected = Sleep::P4S;
    }
    else if (maxTime > OsDeltaTime::from_ms(2600))
    {
        duration_selected = OsDeltaTime::from_ms(2000 * sleepAdj / 1000);
        period_selected = Sleep::P2S;
    }
    else if (maxTime > OsDeltaTime::from_ms(1500))
    {
        duration_selected = OsDeltaTime::from_ms(1000 * sleepAdj / 1000);
        period_selected = Sleep::P1S;
    }
    else if (maxTime > OsDeltaTime::from_ms(800))
    {
        duration_selected = OsDeltaTime::from_ms(500 * sleepAdj / 1000);
        period_selected = Sleep::P500MS;
    }
    else if (maxTime > OsDeltaTime::from_ms(500))
    {
        duration_selected = OsDeltaTime::from_ms(250 * sleepAdj / 1000);
        period_selected = Sleep::P250MS;
    }
    else
    {
        return;
    }

    PRINT_DEBUG(1, F("Sleep (ostick) :%lix%i"), duration_selected.to_ms(), maxTime / duration_selected);
    if (debugLevel > 0)
    {
        Serial.flush();
    }

    for (uint16_t nbsleep = maxTime / duration_selected; nbsleep > 0 && !new_click; nbsleep--)
    {
        powerDown(period_selected);
        hal_add_time_in_sleep(duration_selected);

        buttonInterupt();
    }
    PRINT_DEBUG(1, F("Wakeup"));
}

void loop()
{
    rst_wdt();
    OsDeltaTime to_wait = OSS.runloopOnce();
    if (to_wait > OsDeltaTime(0))
    {
        // Go to sleep if we have nothing to do.
        powersave(to_wait);
    }

    if (new_click)
    {
        send_now = true;
        new_click = false;
        sendjob.setCallbackRunnable(begin_read);
    }
}