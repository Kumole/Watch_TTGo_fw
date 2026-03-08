#include "config.h"
#include "stepCounter.h"

// Check if Bluetooth configs are enabled
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Bluetooth Serial object
BluetoothSerial SerialBT;

// Watch objects
TTGOClass *watch;
TFT_eSPI *tft;
BMA *sensor;

uint32_t sessionId = 30;

volatile uint8_t state;
volatile bool irqBMA = false;
volatile bool irqButton = false;

bool sessionStored = false;
bool sessionSent = false;

int currentSessionIdx = 0;
int storedSessionCount = 0;
const int MAX_SESSIONS = 10;

// Timer variables
unsigned long last = 0;
unsigned long updateTimeout = 0;

void initHikeWatch()
{
    // LittleFS
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
        Serial.println("LittleFS Mount Failed");
        return;
    }

    // Stepcounter
    // Configure IMU
    // Enable BMA423 step count feature
    // Reset steps
    // Turn on step interrupt
    // Initialize step counter
    initStepCounter();

    // Side button
    pinMode(AXP202_INT, INPUT_PULLUP);
    attachInterrupt(AXP202_INT, [] {
        irqButton = true;
    }, FALLING);

    //!Clear IRQ unprocessed first
    watch->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);
    watch->power->clearIRQ();

    return;
}

void sendDataBT(fs::FS &fs, const char * path)
{
    /* Sends data via SerialBT */
    fs::File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return;
    }
    Serial.println("- read from file:");
    while(file.available()){
        SerialBT.write(file.read());
    }
    file.close();
}

void updateSessionCount() {
    storedSessionCount = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/id_%d.txt", i);
        if (LittleFS.exists(path)) {
            storedSessionCount++;
        }
    }
}

void sendSessionBT() {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/id_%d.txt", i);
        
        if (LittleFS.exists(path)) {
            sendDataBT(LittleFS, path); // ID
            SerialBT.write(';');
            
            sprintf(path, "/steps_%d.txt", i);
            sendDataBT(LittleFS, path);
            SerialBT.write(';');
            
            sprintf(path, "/dist_%d.txt", i);
            sendDataBT(LittleFS, path);
            SerialBT.write(';');
            
            SerialBT.write('\n'); // One line per session for the RPi to parse
        }
    }
}


void saveSessionData(int idx, uint16_t id, uint32_t steps, float distance) {
    char path[20];
    
    sprintf(path, "/id_%d.txt", idx);
    writeFile(LittleFS, path, String(id).c_str());
    
    sprintf(path, "/steps_%d.txt", idx);
    writeFile(LittleFS, path, String(steps).c_str());
    
    sprintf(path, "/dist_%d.txt", idx);
    writeFile(LittleFS, path, String(distance).c_str());
}

void deleteSession() {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/id_%d.txt", i); LittleFS.remove(path);
        sprintf(path, "/steps_%d.txt", i); LittleFS.remove(path);
        sprintf(path, "/dist_%d.txt", i); LittleFS.remove(path);
    }
    storedSessionCount = 0; // Reset counter
    Serial.println("Memory cleared. Ready for new sessions.");
}

void setup()
{
    Serial.begin(115200);
    watch = TTGOClass::getWatch();
    watch->begin();
    watch->openBL();

    //Receive objects for easy writing
    tft = watch->tft;
    sensor = watch->bma;
    
    initHikeWatch();
    updateSessionCount();
    state = 1;

    SerialBT.begin("Hiking Watch");
}

void loop()
{
    switch (state)
    {
    case 1:
    {
        /* Initial stage */
        //Basic interface
        watch->tft->fillScreen(TFT_BLACK);
        watch->tft->setTextFont(4);
        watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
        watch->tft->drawString("Hiking Watch",  45, 25, 4);
        watch->tft->drawString("Press button", 50, 80);
        watch->tft->drawString("to start session", 40, 110);

        bool exitSync = false;

        //Bluetooth discovery
        while (1)
        {
            /* Bluetooth sync */
            if (SerialBT.available())
            {
                char incomingChar = SerialBT.read();
                if (incomingChar == 'c' and sessionStored and not sessionSent)
                {
                    sendSessionBT();
                    sessionSent = true;
                }
                

                if (sessionSent && sessionStored) {
                    // Update timeout before blocking while
                    updateTimeout = 0;
                    last = millis();
                    while(1)
                    {
                        updateTimeout = millis();

                        if (SerialBT.available())
                            incomingChar = SerialBT.read();
                        if (incomingChar == 'r')
                        {
                            Serial.println("Got an R");
                            // Delete session
                            deleteSession();
                            sessionStored = false;
                            sessionSent = false;
                            incomingChar = 'q';
                            exitSync = true;
                            break;
                        }
                        else if ((millis() - updateTimeout > 2000))
                        {
                            Serial.println("Waiting for timeout to expire");
                            updateTimeout = millis();
                            sessionSent = false;
                            exitSync = true;
                            break;
                        }
                    }
                }
            }
            if (exitSync)
            {
                delay(1000);
                watch->tft->fillRect(0, 0, 240, 240, TFT_BLACK);
                watch->tft->drawString("Hiking Watch",  45, 25, 4);
                watch->tft->drawString("Press button", 50, 80);
                watch->tft->drawString("to start session", 40, 110);
                exitSync = false;
            }

            /*      IRQ     */
            if (irqButton) {
            irqButton = false;
            watch->power->readIRQ();
            
            // CHECK THE LIMIT BEFORE STARTING
            updateSessionCount(); 

            if (storedSessionCount >= MAX_SESSIONS) {
                // WARNING UI
                watch->tft->fillScreen(TFT_RED);
                watch->tft->setTextColor(TFT_WHITE);
                watch->tft->drawString("MEMORY FULL!", 45, 80, 4);
                watch->tft->drawString("Sync with RPi", 45, 110);
                watch->tft->drawString("to clear space", 35, 140);
                delay(3000); // Show warning for 3 seconds
                
                // Redraw original screen
                state = 1; 
                break; // Exit while to refresh UI
            } else {
                state = 2; // Allow starting the hike
                break;
            }
            watch->power->clearIRQ();
            }
        }
        break;
    }
    case 2:
    {
        /* Hiking session initalisation */
        
        state = 3;
        break;
    }
    case 3:
    {
        /* Hiking session ongoing */
        watch->tft->fillRect(0, 0, 240, 240, TFT_BLACK);
        watch->tft->drawString("Starting hike", 45, 100);
        delay(1000);
        watch->tft->fillRect(0, 0, 240, 240, TFT_BLACK);

        watch->tft->setCursor(45, 70);
        watch->tft->print("Steps: 0");

        watch->tft->setCursor(45, 100);
        watch->tft->print("Dist: 0 km");

        uint32_t stepCount = 0;
        
        // Ensure IRQ is clean before starting the loop
        watch->power->readIRQ();
        watch->power->clearIRQ();
        irqButton = false; 

        while (state == 3) {
            // Handle Step Interrupt
            if (irqBMA) {
                irqBMA = false;
                if (sensor->readInterrupt() && sensor->isStepCounter()) {
                    stepCount = sensor->getCounter();
                    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
                    watch->tft->setCursor(120, 70); // Update just the number
                    watch->tft->print(stepCount);
                    watch->tft->print("   ");
                }
            }

            // Handle Button Press to END session
            if (irqButton) {
                irqButton = false;
                watch->power->readIRQ();
                
                // Only act if it was a short press
                if (watch->power->isPEKShortPressIRQ()) {
                    // 1. Save using the current index
                    saveSessionData(currentSessionIdx, sessionId, stepCount, 0.0);
                    
                    // 2. Increment index for NEXT time (FIFO)
                    currentSessionIdx++;
                    if (currentSessionIdx >= MAX_SESSIONS) currentSessionIdx = 0;
                    
                    // 3. Update the total count for Case 1 guard
                    updateSessionCount();
                    
                    sessionStored = true;
                    state = 4; // Move to save/exit state
                }
                watch->power->clearIRQ();
            }
            delay(50); // Small delay to prevent CPU hogging
        }
        break; 
    }
    case 4:
    {
        //Save hiking session data
        delay(1000);
        state = 1;  
        break;
    }
    default:
        // Restart watch
        ESP.restart();
        break;
    }
}