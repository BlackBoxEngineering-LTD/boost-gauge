#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite frame = TFT_eSprite(&tft);

const int SENSOR_PIN = 4;
const float VREF = 3.3f;
const int ADC_MAX = 4095;
const int SAMPLE_AVG = 16;

const int CX = 120;
const int CY = 120;
const int R_OUT = 104;
const int ARC_THICK = 14;
const int R_NEEDLE = 90;
const int R_LABEL = 78;

const float GAUGE_DEG_TO_RAD = 0.01745329252f;
const float BAR_MIN = -1.0f;
const float BAR_MAX = 1.1f;

float CAL_BAR_OFFSET = 0.0f;
float CAL_SCALE = 4.0f;

float smoothPsi = 0.0f;

bool SIMULATE = true;
float simTime = 0.0f;

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 30;
float lastPsi = 0.0f;

struct TickPoint { int x0, y0, x1, y1; };
TickPoint ticks[32];
uint16_t gradientColors[91];
bool graphicsInitialized = false;

inline void pointOnCircle(float deg, int r, int &x, int &y)
{
    float rad = deg * GAUGE_DEG_TO_RAD;
    x = CX + int(cosf(rad) * r);
    y = CY - int(sinf(rad) * r);
}

inline float psiToAngle(float psi)
{
    if (psi <= 0.0f)
    {
        float frac = (psi + 15.0f) / 15.0f;
        return 270.0f + frac * -90.0f;
    }
    else
    {
        float frac = psi / 16.0f;
        return 180.0f + frac * -180.0f;
    }
}

inline float voltageToBar(float v) { return ((v - 0.5f) * 0.5f) - 1.0f; }
inline float barToPsi(float bar) { return bar * 14.5038f; }

void drawArcBandGradient(TFT_eSprite &spr, int rOuter, int thickness, int degStart, int degEnd, bool boostArc)
{
    int dir = (degEnd >= degStart) ? 1 : -1;
    int count = abs(degEnd - degStart);
    if (count == 0) return;
    
    for (int i = 0; i <= count; ++i)
    {
        int a = degStart + dir * i;
        float frac = (float)i / (float)count;
        uint16_t col = boostArc
                           ? gradientColors[int(frac * 90)]
                           : spr.color565(0, 0, 50 + int(frac * 205));
        int x0, y0, x1, y1;
        pointOnCircle(a, rOuter, x0, y0);
        pointOnCircle(a, rOuter - thickness, x1, y1);
        spr.drawLine(x0, y0, x1, y1, col);
    }
}

void initializeGraphics()
{
    for (int p = -15; p <= 16; p++)
    {
        float a = psiToAngle((float)p);
        int idx = p + 15;
        if (p % 5 == 0)
        {
            pointOnCircle(a, R_OUT + 2, ticks[idx].x0, ticks[idx].y0);
            pointOnCircle(a, R_OUT - ARC_THICK - 6, ticks[idx].x1, ticks[idx].y1);
        }
        else
        {
            pointOnCircle(a, R_OUT, ticks[idx].x0, ticks[idx].y0);
            pointOnCircle(a, R_OUT - ARC_THICK - 3, ticks[idx].x1, ticks[idx].y1);
        }
    }
    
    for (int i = 0; i <= 90; i++)
    {
        float frac = (float)i / 90.0f;
        gradientColors[i] = tft.color565(50 + int(frac * 205), 0, 0);
    }
    
    graphicsInitialized = true;
}

void autoZeroAtStartup()
{
    const int samples = 512;
    long sum = 0;
    for (int i = 0; i < samples; ++i)
    {
        sum += analogRead(SENSOR_PIN);
        delay(2);
    }
    float adcAvg = (float)sum / (float)samples;
    float volts = (adcAvg * VREF) / (float)ADC_MAX;
    float barAtRest = voltageToBar(volts);
    CAL_BAR_OFFSET = -barAtRest;
    Serial.printf("Auto-zero: ADCavg=%.1f V=%.3f bar=%.4f offset=%.4f\n",
                  adcAvg, volts, barAtRest, CAL_BAR_OFFSET);
}

struct GaugeLabel
{
    const char *txt;
    float value;
};

void composeFrame(float psi, float bar, float volts)
{
    frame.fillSprite(TFT_BLACK);

    drawArcBandGradient(frame, R_OUT, ARC_THICK, 180, 270, false);
    drawArcBandGradient(frame, R_OUT, ARC_THICK, 180, 0, true);

    frame.setTextDatum(MC_DATUM);
    frame.setTextColor(TFT_WHITE, TFT_BLACK);
    frame.setTextFont(2);

    GaugeLabel labs[] = {
        {"-15", -15}, {"-10", -10}, {"-5", -5}, {"0", 0}, {"+5", 5}, {"+10", 10}, {"+15", 15}, {"+16", 16}};

    for (auto &L : labs)
    {
        float a = psiToAngle(L.value);
        int lx, ly;
        pointOnCircle(a, R_LABEL, lx, ly);
        frame.drawString(L.txt, lx, ly);
    }

    for (int p = -15; p <= 16; p++)
    {
        int idx = p + 15;
        frame.drawLine(ticks[idx].x0, ticks[idx].y0, ticks[idx].x1, ticks[idx].y1, TFT_WHITE);
    }

    float angle = psiToAngle(psi);
    int nx = CX + int(cosf(angle * GAUGE_DEG_TO_RAD) * R_NEEDLE);
    int ny = CY - int(sinf(angle * GAUGE_DEG_TO_RAD) * R_NEEDLE);
    frame.drawLine(CX, CY, nx, ny, TFT_WHITE);
    frame.fillCircle(CX, CY, 4, TFT_WHITE);

    int bx = 180, by = 170;
    frame.setTextColor(TFT_WHITE, TFT_BLACK);
    frame.setTextDatum(MC_DATUM);

    frame.setTextFont(4);
    if (psi >= 15.5f) {
        frame.setTextColor(TFT_RED, TFT_BLACK);
        frame.drawString("MAX BOOST", bx - 15, by - 20);
        frame.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        frame.drawString(psi < 0 ? "VACUUM" : "BOOST", bx, by - 20);
    }

    frame.setTextFont(2);
    char buf[32];
    frame.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "PSI %5.1f", psi);
    frame.drawString(buf, bx - 38, by - 5);
    snprintf(buf, sizeof(buf), "BAR %5.2f", bar);
    frame.drawString(buf, bx - 38, by + 12);
    frame.setTextDatum(MC_DATUM);
    
    frame.setTextFont(1);
    snprintf(buf, sizeof(buf), "V:%.2f %s", volts, SIMULATE ? "SIM" : "REAL");
    frame.drawString(buf, CX, CY - 30);

    frame.pushSprite(0, 0);
}

void setup()
{
    Serial.begin(115200);
    analogReadResolution(12);

    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    frame.setColorDepth(16);
    frame.createSprite(240, 240);

    initializeGraphics();
    autoZeroAtStartup();
}

void loop()
{
    unsigned long currentTime = millis();
    float changeMagnitude = abs(smoothPsi - lastPsi);
    unsigned long interval = changeMagnitude > 0.5f ? 16 : UPDATE_INTERVAL;
    
    if (currentTime - lastUpdate < interval) return;
    
    float bar, volts;
    
    if (SIMULATE) {
        simTime += 0.02f;
        float simPsi = 16.0f * sinf(simTime);
        bar = simPsi / 14.5038f;
        volts = 1.65f;
    } else {
        long sum = 0;
        for (int i = 0; i < SAMPLE_AVG; ++i)
            sum += analogRead(SENSOR_PIN);
        float adcAvg = (float)sum / SAMPLE_AVG;
        volts = (adcAvg * VREF) / ADC_MAX;
        
        if (volts < 0.1f || volts > 4.9f) {
            Serial.println("Sensor error detected!");
            return;
        }
        
        bar = (voltageToBar(volts) + CAL_BAR_OFFSET) * CAL_SCALE;
    }
    
    if (bar < BAR_MIN) bar = BAR_MIN;
    if (bar > BAR_MAX) bar = BAR_MAX;

    float psi = barToPsi(bar);
    lastPsi = smoothPsi;
    smoothPsi = 0.90f * smoothPsi + 0.10f * psi;

    composeFrame(smoothPsi, bar, volts);
    lastUpdate = currentTime;
}