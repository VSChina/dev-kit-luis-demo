#include "Arduino.h"
#include "iot_client.h"
#include "AZ3166WiFi.h"
#include "OLEDDisplay.h"
#include "http_client.h"
#include "mbed_memory_status.h"
#include "json.h"
#include "AudioClass.h"

static boolean hasWifi = false;
static const int recordedDuration = 3;
static char *waveFile = NULL;
static int wavFileSize;
static bool translated = false;
static const uint32_t delayTimes = 1000;
static AudioClass Audio;
static const int AUDIO_SIZE = ((32000 * recordedDuration) + 44);
static const char *DeviceConnectionString = "HostName=devkit-luis-iot-hub.azure-devices.net;DeviceId=devkit;SharedAccessKey=ubTyTDivvtuwJ/dEMGA1FdKVN8wbGRRW9UV6vo+b2HQ=";

enum STATUS
{
    Idle,
    Recorded,
    WavReady,
    Uploading,
    Uploaded
};
static STATUS status;

static void InitWiFi()
{
    if (WiFi.begin() == WL_CONNECTED)
    {
        hasWifi = true;
    }
    else
    {
        Screen.print(1, "No Wi-Fi");
    }
}

static void EnterIdleState()
{
    status = Idle;
    Screen.clean();
    Screen.print(0, "Hold B to talk");
}

void setup()
{
    Screen.clean();
    Screen.print(0, "DevKit Translator");
    Screen.print(2, "Initializing...");
    Screen.print(3, " > WiFi");

    hasWifi = false;
    InitWiFi();
    EnterIdleState();
    iot_client_set_connection_string(DeviceConnectionString);
}

void log_time()
{
    time_t t = time(NULL);
    Serial.printf("Time is now (UTC): %s\r\n", ctime(&t));
}

void freeWavFile()
{
    if (waveFile != NULL)
    {
        free(waveFile);
        waveFile = NULL;
    }
}

void loop()
{
    if (!hasWifi)
    {
        return;
    }

    uint32_t curr = millis();
    switch (status)
    {
    case Idle:
        if (digitalRead(USER_BUTTON_B) == LOW)
        {
            waveFile = (char *)malloc(AUDIO_SIZE + 1);
            if (waveFile == NULL)
            {
                Serial.println("No enough Memory! ");
                EnterIdleState();
                return;
            }
            memset(waveFile, 0, AUDIO_SIZE + 1);
            Audio.format(8000, 16);
            Audio.startRecord(waveFile, AUDIO_SIZE, recordedDuration);
            status = Recorded;
            Screen.clean();
            Screen.print(0, "Release B to send\r\nMax duraion: 3 sec");
        }
        break;
    case Recorded:
        if (digitalRead(USER_BUTTON_B) == HIGH)
        {
            Audio.getWav(&wavFileSize);
            if (wavFileSize > 0)
            {
                wavFileSize = Audio.convertToMono(waveFile, wavFileSize, 16);
                if (wavFileSize <= 0)
                {
                    Serial.println("ConvertToMono failed! ");
                    EnterIdleState();
                    freeWavFile();
                }
                else
                {
                    status = WavReady;
                    Screen.clean();
                    Screen.print(0, "Processing...          ");
                    Screen.print(1, "Uploading #1", true);
                }
            }
            else
            {
                Serial.println("No Data Recorded! ");
                freeWavFile();
                EnterIdleState();
            }
        }
        break;
    case WavReady:
        if (wavFileSize > 0 && waveFile != NULL)
        {
            Serial.print("begin uploading: ");
            log_time();
            if (0 == iot_client_blob_upload_step1("test.wav"))
            {
                status = Uploading;
                Screen.clean();
                Screen.print(0, "Processing...");
                Screen.print(1, "Uploading audio...");
            }
            else
            {
                Serial.println("Upload step 1 failed");
                freeWavFile();
                EnterIdleState();
            }
        }
        else
        {
            freeWavFile();
            EnterIdleState();
        }
        break;
    case Uploading:
        if (iot_client_blob_upload_step2(waveFile, wavFileSize) == 0)
        {
            status = Uploaded;
            Serial.print("uploaded: ");
            log_time();
            Screen.clean();
            Screen.print(0, "Processing...          ");
            Screen.print(1, "Receiving...", true);
        }
        else
        {
            freeWavFile();
            EnterIdleState();
        }
        break;
    case Uploaded:
    Serial.print("waiting for c2d message: ");
    log_time();
        char *etag = (char *)malloc(40);
        while (!translated)
        {
            const char *p = iot_client_get_c2d_message(etag);
            while (p != NULL)
            {
                complete_c2d_message((char *)etag);
                if (strlen(p) == 0)
                {
                    free((void *)p);
                    break;
                }
                Screen.clean();
                Screen.print(1, "Translation: ");
                Screen.print(2, p, true);
                Serial.print("got c2d message: ");
                log_time();
                translated = true;
                free((void *)p);
                p = iot_client_get_c2d_message(etag);
            }
        }
        free(etag);
        freeWavFile();
        translated = false;
        status = Idle;
        Screen.print(0, "Hold B to talk");
        break;
    }

    curr = millis() - curr;
    if (curr < delayTimes)
    {
        delay(delayTimes - curr);
    }
}

