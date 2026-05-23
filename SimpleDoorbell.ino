// ESP32C3FN4 SuperMini Board
// =====================================================================================
// Arduino IDE settings:
//   - Board: ESP32C3 Dev Module
//   - ESP CDC On Boot: Enabled
//   - CPU Frequency: 80MHz (WiFi)
//   - Core Debug Level: None
//   - Erase All Flash Before Sketch Upload: Disabled
//   - Flash frequency: 80Mhz
//   - Flash Mode: QIO
//   - Flash Size: 4MB (32Mb)
//   - JTAG Adapter: Disabled
//   - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
//   - Upload Speed: 921600
//   - Zigbee Mode: Disabled
//   - Programmer: Esptool
// =====================================================================================

#include <HomeSpan.h>


/**************************************************************************************/
/*                                  Pins  definition                                  */

// HomeSpan status LED pin.
#define STATUS_LED_PIN      GPIO_NUM_8
// HomeSpan control button pin.
#define CONTROL_PIN         GPIO_NUM_9

// The doorbell button input pin (radio signal)
#define BELL_BUTTON_PIN     GPIO_NUM_3
// The doorbell signal pin (wired to the sound chip)
#define BELL_SIGNAL_PIN     GPIO_NUM_10

/**************************************************************************************/


/**************************************************************************************/
/*                            Doorbell hardware  constants                            */

// The doorbell button signal duration in milliseconds.
constexpr uint32_t BELL_BUTTON_SIGNAL_DURATION = 500;
// The doorbell play signal duration in milliseconds.
constexpr uint32_t BELL_SIGNAL_DURATION = 250;

/**************************************************************************************/


/**************************************************************************************/
/*                                  Global variables                                  */

// The doorbell state. True if the doorbell is enabled and should play a sound.
// False if the doorbell is disabled and should not play any sound. By default
// the doorbell sound is enabled.
bool SoundEnabled = true;
// Indicates when the ring button was pressed.
volatile bool IsRinging = false;

// The doorbell event object.
SpanCharacteristic* DoorbellEvent = nullptr;

/**************************************************************************************/


/**************************************************************************************/
/*                                  Doorbell service                                  */

struct Doorbell : Service::Doorbell
{
    Doorbell() : Service::Doorbell()
    {
        DoorbellEvent = new Characteristic::ProgrammableSwitchEvent();
    }
};

/**************************************************************************************/


/**************************************************************************************/
/*                             Virtual  Doorbell switches                             */

struct DoorbellSwitch : Service::Switch
{
    SpanCharacteristic* Switch;
    
    DoorbellSwitch() : Service::Switch()
    {
        // Default is false (bell is turned off) and we store current value in NVS.
        Switch = new Characteristic::On(false, true);
        // Get current states.
        SoundEnabled = Switch->getVal();
    }
    
    bool update()
    {
        SoundEnabled = Switch->getNewVal();

        return true;
    }
};

/**************************************************************************************/


/**************************************************************************************/
/*                              Doorbell signal interrup                              */

void IRAM_ATTR RingInterrupt()
{
    // The previous level state. True if the previous level was HIGH. False - if LOW.
    static bool WasHigh = false;
    // Last time when level changed from LOW to HIGH.
    static uint32_t LastMillis = 0;

    // Exit immediately if the ringing flag was already set.
    if (IsRinging)
        return;

    // Read the ring signal pin.
    bool NowHigh = (digitalRead(BELL_BUTTON_PIN) == HIGH);
    // If current level is HIGH...
    if (NowHigh)
    {
        // ...and if it was HIGH (what should nebver happen, but who knows)...
        if (WasHigh)
            // ...exit from interrupt.
            return;
        
        // We are here only if level changed from LOW to HIGH.
        // Set previous state to the current one (HIGH).
        WasHigh = true;
        // Remember time when level changed to high.
        LastMillis = millis();
        // Exit from ISR as we need to wait when it downs from HIGH to LOW.
        return;
    }

    // We are here only if the current level is LOW.
    // If it was not HIGH (what should never happen, but show knows)...
    if (!WasHigh)
        // ...exit from the interrupt.
    
    // Ok, the level changed from HIGH to LOW. Reset the previous state to
    // the current one (LOW).
    WasHigh = false;
    // Now calculate the pulse duration and if it looks like bell signal
    // duration set the ringing flag.
    uint32_t CurrentMillis = millis(); // We need this to be able to use unsigned values.
    // Set ringing flag if pulse duration is correct.
    IsRinging = ((CurrentMillis - LastMillis >= BELL_BUTTON_SIGNAL_DURATION));
}

/**************************************************************************************/


/**************************************************************************************/
/*                                 Arduino  functions                                 */

// Arduino initialization routine.
void setup()
{
    // Initialize debug UART.
    Serial.begin(115200);
    
    // Initialize pins door bell pins.
    pinMode(BELL_SIGNAL_PIN, OUTPUT);
    pinMode(BELL_BUTTON_PIN, INPUT_PULLDOWN);
    digitalWrite(BELL_SIGNAL_PIN, LOW);

    // Initialize HomeSpan pins
    pinMode(CONTROL_PIN, INPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, HIGH);

    // Initialize HomeSpan.
    homeSpan.setControlPin(CONTROL_PIN);
    homeSpan.setStatusPin(STATUS_LED_PIN);
    homeSpan.setPairingCode("51234594");
    homeSpan.begin(Category::Bridges, "DroneTales Simple Doorbell Bridge");

    // Build device's serial number.
    char Sn[24];
    snprintf(Sn, 24, "DRONETALES-%llX", ESP.getEfuseMac());

    // Configure the Bridge accessory. We do not need to add name as it is taken
    // from the begin() method.
    new SpanAccessory();
	new Service::AccessoryInformation();
	new Characteristic::Identify();
    new Characteristic::Manufacturer("DroneTales");
    new Characteristic::SerialNumber(Sn);
    new Characteristic::Model("DroneTales Simple Doorbell");
    new Characteristic::FirmwareRevision("1.0.3.0");
    
    // Configure doorbell.
    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Manufacturer("DroneTales");
    new Characteristic::SerialNumber(Sn);
    new Characteristic::Model("DroneTales Simple Doorbell");
    new Characteristic::FirmwareRevision("1.0.3.0");
    new Characteristic::Name("Simple Doorbell");
    new Doorbell();
    
    // Configure doorbell switch.
    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Manufacturer("DroneTales");
    new Characteristic::SerialNumber(Sn);
    new Characteristic::Model("DroneTales Simple Doorbell");
    new Characteristic::FirmwareRevision("1.0.3.0");
    new Characteristic::Name("Simple Doorbell Sound Switch");
    new DoorbellSwitch();

    // Attached the interrupt to the ring signal pin.
    attachInterrupt(BELL_BUTTON_PIN, RingInterrupt, CHANGE);
}

// Arduino main loop.
void loop()
{
    // We need to copy the ring indicator. We need it because the IsRinging may change
    // right in the middle of the routine.
    bool RingDetected = IsRinging;

    // If ring was detected (interrupt happened) after those statements then it
    // will be processed on next iteration because the IsRinging flag will be set
    // to True.
    
    // If ring was detected and notification is enabled...
    if (RingDetected)
        // Notify HomeKit about doorbell ring.
        DoorbellEvent->setVal(SpanButton::SINGLE);

    // Now process the HomeSpan messages.
    homeSpan.poll();

    // If ring was not detected then we can exit. If the ringing flag was changed
    // we find it on next iteration.
    if (!RingDetected)
        return;

    // We are here only if ring was detected. So check the sound enabling flag
    // and if set then play the sound.
    if (SoundEnabled)
    {
        digitalWrite(BELL_SIGNAL_PIN, HIGH);
        delay(BELL_SIGNAL_DURATION);
        digitalWrite(BELL_SIGNAL_PIN, LOW);
    }

    // Here we can reset the ringing flag. Doing it here prevents from
    // multiple ring detection and makes it more stable.
    IsRinging = false;
}

/**************************************************************************************/
