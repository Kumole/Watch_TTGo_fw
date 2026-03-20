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
const int MAX_SESSIONS = 5;

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
    Serial.println("Deleting all sessions...");
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/id_%d.txt", i); LittleFS.remove(path);
        sprintf(path, "/steps_%d.txt", i); LittleFS.remove(path);
        sprintf(path, "/dist_%d.txt", i); LittleFS.remove(path);
    }
    
    // Force reset variables
    storedSessionCount = 0; 
    currentSessionIdx = 0;
    sessionStored = false;
    sessionSent = false;

    updateSessionCount();
    Serial.print("Verification - Stored sessions: ");
    Serial.println(storedSessionCount);
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
    updateSessionCount(); 
    
    // Clear hardware interrupt state before entering the loop
    watch->power->readIRQ();
    watch->power->clearIRQ();
    irqButton = false;

    // Draw your original Start Screen
    watch->tft->fillScreen(TFT_BLACK);
    watch->tft->setTextFont(4);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    watch->tft->drawString("Hiking Watch", 45, 25, 4);
    watch->tft->drawString("Press button", 50, 80);
    watch->tft->drawString("to start session", 40, 110);

    bool exitSync = false;

    while (!exitSync) 
    {
        /* Bluetooth sync */
        if (SerialBT.available())
        {
            char incomingChar = SerialBT.read();
            // Use sessionStored to allow sync only if there is data
            if (incomingChar == 'c' && sessionStored && !sessionSent)
            {
                sendSessionBT();
                sessionSent = true;
            }

            if (incomingChar == 'r')
            {
                Serial.println("Got an R - Sync Complete");
                deleteSession(); 
                
                // IMPORTANT: Reset hardware flags so button works after sync
                watch->power->readIRQ();
                watch->power->clearIRQ();
                irqButton = false; 

                exitSync = true; // Break while loop to refresh state
                break;
            }
        }

        /* Button Handling */
        if (irqButton) {
            irqButton = false;
            watch->power->readIRQ(); // Check hardware register
            
            // Only process if it's a short press
            if (watch->power->isPEKShortPressIRQ()) {
                updateSessionCount(); 

                if (storedSessionCount >= MAX_SESSIONS) {
                    // Draw Warning without permanently changing UI
                    watch->tft->fillScreen(TFT_RED);
                    watch->tft->setTextColor(TFT_WHITE);
                    watch->tft->drawString("MEMORY FULL!", 45, 80, 4);
                    watch->tft->drawString("Sync with RPi", 45, 110);
                    delay(3000); 
                    
                    exitSync = true; // Redraw the normal black screen
                } else {
                    state = 2; // Allow starting hike
                    exitSync = true;
                }
            }
            watch->power->clearIRQ(); // Important to release the interrupt line
        }
        delay(10); 
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