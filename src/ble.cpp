#include "ble.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

static const char *BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char *BLE_COMMAND_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

static BLEServer *g_bleServer = nullptr;
static BLECharacteristic *g_bleCommandCharacteristic = nullptr;
static bool g_bleClientConnected = false;
static bool g_blePreviousClientConnected = false;
static String g_pendingBleCommand;

class HubBleServerCallbacks : public BLEServerCallbacks {
	void onConnect(BLEServer *server) override
	{
		(void)server;
		g_bleClientConnected = true;
	}

	void onDisconnect(BLEServer *server) override
	{
		(void)server;
		g_bleClientConnected = false;
	}
};

class HubBleCommandCallbacks : public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *characteristic) override
	{
		std::string value = characteristic->getValue();
		if (value.empty()) {
			return;
		}

		g_pendingBleCommand = String(value.c_str());
		g_pendingBleCommand.trim();
	}
};

void bleInit(const char *deviceName)
{
	BLEDevice::init(deviceName);
	BLEDevice::setMTU(247);

	g_bleServer = BLEDevice::createServer();
	g_bleServer->setCallbacks(new HubBleServerCallbacks());

	BLEService *service = g_bleServer->createService(BLE_SERVICE_UUID);
	g_bleCommandCharacteristic = service->createCharacteristic(
		BLE_COMMAND_CHAR_UUID,
		BLECharacteristic::PROPERTY_READ |
			BLECharacteristic::PROPERTY_WRITE |
			BLECharacteristic::PROPERTY_NOTIFY
	);

	g_bleCommandCharacteristic->addDescriptor(new BLE2902());
	g_bleCommandCharacteristic->setCallbacks(new HubBleCommandCallbacks());
	g_bleCommandCharacteristic->setValue("READY");

	service->start();
	BLEAdvertising *advertising = BLEDevice::getAdvertising();
	advertising->addServiceUUID(BLE_SERVICE_UUID);
	advertising->start();
}

void bleProcess()
{
	if (!g_bleClientConnected && g_blePreviousClientConnected) {
		delay(300);
		g_bleServer->startAdvertising();
		g_blePreviousClientConnected = g_bleClientConnected;
	}

	if (g_bleClientConnected && !g_blePreviousClientConnected) {
		g_blePreviousClientConnected = g_bleClientConnected;
	}
}

bool bleIsClientConnected()
{
	return g_bleClientConnected;
}

String bleTakePendingCommand()
{
	String command = g_pendingBleCommand;
	g_pendingBleCommand = "";
	return command;
}

void bleSendCommand(const String &command, const String &payload)
{
	if (!g_bleClientConnected || g_bleCommandCharacteristic == nullptr) {
		return;
	}

	String message = command;
	if (payload.length() > 0) {
		message += "|";
		message += payload;
	}

	g_bleCommandCharacteristic->setValue(message.c_str());
	g_bleCommandCharacteristic->notify();
	delay(40);
}