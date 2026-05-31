/*
====================================================
 INMOOV I2 OS v2.0
 ESP32 MAIN CONTROLLER

 MODES

 0 = Constructor
 1 = Voice
 2 = Auto Grip
 3 = EMG

====================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <SPIFFS.h>

#define ARDUINO_UNO_I2C_ADDR 8

// ==========================================
// WIFI
// ==========================================

const char* AP_SSID = "InMoov_Hand_AP";
const char* AP_PASS = "12345678password";

IPAddress apIP(10,10,10,1);
IPAddress netMask(255,255,255,0);

WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// ==========================================
// HC-SR04
// ==========================================

#define TRIG_PIN 25
#define ECHO_PIN 26

// ==========================================
// MODES
// ==========================================

enum SystemMode
{
    MODE_CONSTRUCTOR = 0,
    MODE_VOICE       = 1,
    MODE_AUTOGRIP    = 2,
    MODE_EMG         = 3
};

volatile uint8_t currentMode = MODE_CONSTRUCTOR;

// ==========================================
// SECURITY
// ==========================================

String adminPin = "47781842";

bool isLoggedIn = false;

// ==========================================
// ROBOT STATE
// ==========================================

int currentFingers[5] =
{
    0,
    0,
    0,
    0,
    0
};

bool robotBusy = false;

unsigned long busyStart = 0;
unsigned long busyDuration = 0;

// ==========================================
// LOGGING
// ==========================================

void logEvent(String msg)
{
    Serial.print("[LOG] ");
    Serial.println(msg);
}

// ==========================================
// I2C
// ==========================================

void sendFinger(uint8_t finger, uint8_t angle)
{
    Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);

    Wire.write(finger);
    Wire.write(angle);

    Wire.endTransmission();

    Serial.printf(
        "[I2C] Finger=%d Angle=%d\n",
        finger,
        angle
    );
}

void moveFinger(uint8_t finger, uint8_t angle)
{
    if(finger > 4) return;

    currentFingers[finger] = angle;

    sendFinger(finger + 1, angle);
}

void moveAllFingers(uint8_t angle)
{
    for(int i=0;i<5;i++)
    {
        currentFingers[i] = angle;
    }

    sendFinger(0, angle);
}

// ==========================================
// HC-SR04
// ==========================================

float getDistanceCM()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(3);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG_PIN, LOW);

    long duration =
        pulseIn(
            ECHO_PIN,
            HIGH,
            30000
        );

    if(duration == 0)
        return -1;

    return duration * 0.0343 / 2.0;
}

// ==========================================
// SPIFFS
// ==========================================

void serveIndex()
{
    File file =
        SPIFFS.open(
            "/index.html",
            "r"
        );

    if(!file)
    {
        server.send(
            500,
            "text/plain",
            "index.html missing"
        );
        return;
    }

    server.streamFile(
        file,
        "text/html"
    );

    file.close();
}

// ==========================================
// LOGIN
// ==========================================

void handleLogin()
{
    if(!server.hasArg("pin"))
    {
        server.send(
            400,
            "text/plain",
            "NO PIN"
        );
        return;
    }

    String pin =
        server.arg("pin");

    if(pin == adminPin)
    {
        isLoggedIn = true;

        server.send(
            200,
            "text/plain",
            "OK"
        );

        logEvent("Admin login");
    }
    else
    {
        server.send(
            401,
            "text/plain",
            "DENIED"
        );
    }
}

// ==========================================
// MODE
// ==========================================

void handleSetMode()
{
    if(!isLoggedIn)
    {
        server.send(
            403,
            "text/plain",
            "LOGIN REQUIRED"
        );
        return;
    }

    if(!server.hasArg("m"))
    {
        server.send(
            400,
            "text/plain",
            "NO MODE"
        );
        return;
    }

    currentMode =
        server.arg("m").toInt();

    logEvent(
        "Mode -> " +
        String(currentMode)
    );

    server.send(
        200,
        "text/plain",
        "OK"
    );
}

void handleGetMode()
{
    String json =
        "{\"mode\":" +
        String(currentMode) +
        "}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ==========================================
// STATUS
// ==========================================

void handleStatus()
{
    float dist =
        getDistanceCM();

    String json = "{";

    json += "\"mode\":";
    json += String(currentMode);
    json += ",";

    json += "\"busy\":";
    json += robotBusy ? "true":"false";
    json += ",";

    json += "\"distance\":";
    json += String(dist,1);
    json += ",";

    json += "\"fingers\":[";

    for(int i=0;i<5;i++)
    {
        json += currentFingers[i];

        if(i<4)
            json += ",";
    }

    json += "]";

    json += "}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ==========================================
// EXECUTE
// ==========================================

void handleExecute()
{
    if(robotBusy)
    {
        if(
            millis() - busyStart
            <
            busyDuration
        )
        {
            server.send(
                429,
                "text/plain",
                "BUSY"
            );
            return;
        }

        robotBusy = false;
    }

    if(!server.hasArg("f"))
    {
        server.send(
            400,
            "text/plain",
            "NO F"
        );
        return;
    }

    if(!server.hasArg("a"))
    {
        server.send(
            400,
            "text/plain",
            "NO A"
        );
        return;
    }

    int finger =
        server.arg("f").toInt();

    int angle =
        server.arg("a").toInt();

    if(angle < 0 || angle > 180)
    {
        server.send(
            400,
            "text/plain",
            "BAD ANGLE"
        );
        return;
    }

    if(finger < 0 || finger > 5)
    {
        server.send(
            400,
            "text/plain",
            "BAD FINGER"
        );
        return;
    }

    robotBusy = true;
    busyStart = millis();
    busyDuration = 300;

    if(finger == 5)
    {
        moveAllFingers(angle);
    }
    else
    {
        moveFinger(
            finger,
            angle
        );
    }

    server.send(
        200,
        "text/plain",
        "OK"
    );
}

// ==========================================
// VOICE COMMANDS
// ==========================================

void handleVoice()
{
    if(currentMode != MODE_VOICE)
    {
        server.send(
            403,
            "text/plain",
            "VOICE MODE OFF"
        );
        return;
    }

    String cmd =
        server.arg("cmd");

    cmd.toLowerCase();

    if(cmd == "fist")
    {
        moveAllFingers(180);
    }

    else if(cmd == "open")
    {
        moveAllFingers(0);
    }

    else if(cmd == "ok")
    {
        moveFinger(0,180);
        moveFinger(1,180);
    }

    server.send(
        200,
        "text/plain",
        "OK"
    );
}

// ==========================================
// AUTOGRIP
// ==========================================

void processAutoGrip()
{
    if(currentMode != MODE_AUTOGRIP)
        return;

    static bool closed = false;

    float d =
        getDistanceCM();

    if(d < 0)
        return;

    if(d < 15 && !closed)
    {
        moveAllFingers(180);

        closed = true;

        logEvent(
            "Object detected"
        );
    }

    if(d > 20 && closed)
    {
        moveAllFingers(0);

        closed = false;

        logEvent(
            "Object removed"
        );
    }
}

// ==========================================
// NOT FOUND
// ==========================================

void handleNotFound()
{
    server.sendHeader(
        "Location",
        "http://10.10.10.1",
        true
    );

    server.send(
        302,
        "text/plain",
        ""
    );
}

// ==========================================
// SETUP
// ==========================================

void setup()
{
    Serial.begin(115200);

    Serial.println();
    Serial.println();
    Serial.println("INMOOV OS");

    Wire.begin(
        21,
        22
    );

    pinMode(
        TRIG_PIN,
        OUTPUT
    );

    pinMode(
        ECHO_PIN,
        INPUT
    );

    if(!SPIFFS.begin(true))
    {
        Serial.println(
            "SPIFFS ERROR"
        );
    }

    WiFi.mode(WIFI_AP);

    WiFi.softAPConfig(
        apIP,
        apIP,
        netMask
    );

    WiFi.softAP(
        AP_SSID,
        AP_PASS
    );

    dnsServer.start(
        DNS_PORT,
        "*",
        apIP
    );

    server.on("/", serveIndex);

    server.on(
        "/login",
        handleLogin
    );

    server.on(
        "/set_mode",
        handleSetMode
    );

    server.on(
        "/get_mode",
        handleGetMode
    );

    server.on(
        "/status",
        handleStatus
    );

    server.on(
        "/execute",
        handleExecute
    );

    server.on(
        "/voice",
        handleVoice
    );

    server.onNotFound(
        handleNotFound
    );

    server.begin();

    Serial.println(
        "READY"
    );
}

// ==========================================
// LOOP
// ==========================================

void loop()
{
    dnsServer.processNextRequest();

    server.handleClient();

    processAutoGrip();
}
