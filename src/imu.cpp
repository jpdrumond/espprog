// ---------------------------------------------------------------------------
//  Acelerometro + giroscopio MPU-6050 (modulo GY-521) no I2C.
//
//  Task propria no CORE 0, longe da task do barramento PROG (core 1): o I2C
//  nunca disputa CPU com o turnaround de 3 ms. Le o sensor a 100 Hz e deriva:
//    - pitch/roll pela direcao da gravidade (filtro complementar em vetor);
//    - aceleracao linear (sem a gravidade), vibracao RMS/pico em janelas de 1 s;
//    - pancadas: aceleracao linear acima do limiar configurado;
//    - movimento: rotacao acima de IMU_MOTION_DPS.
//  Aceita tambem os primos com o mesmo mapa de registradores (MPU-6500,
//  MPU-9250, ICM-206xx), que aparecem em muitos GY-521 genericos.
// ---------------------------------------------------------------------------
#include "imu.h"
#include "settings.h"
#include <Wire.h>
#include <math.h>

#ifndef PIN_IMU_SDA
#define PIN_IMU_SDA 32
#endif
#ifndef PIN_IMU_SCL
#define PIN_IMU_SCL 33
#endif

#define IMU_I2C_HZ        100000   // 100 kHz: folga para fio de ate ~50 cm
#define IMU_PERIOD_MS     (1000 / IMU_RATE_HZ)
#define IMU_PROBE_MS      2000     // sem sensor: procura de novo a cada 2 s
#define IMU_MAX_FAILS     10       // leituras seguidas com erro = sensor perdido
#define IMU_ALPHA         0.98f    // peso do giroscopio (constante de ~0,5 s)
#define IMU_ALPHA_UNCAL   0.90f    // antes de calibrar o bias puxa o vetor: confia mais no acel
#define IMU_ACC_TRUST_G   0.20f    // |a| longe da gravidade: e aceleracao, nao inclinacao
#define IMU_GMAG_MIN      0.85f    // faixa de |a| parado (gravidade + erro de escala do chip),
#define IMU_GMAG_MAX      1.15f    // mais estreita que a janela acima: a referencia nunca trava
#define IMU_MOTION_DPS    3.0f     // rotacao que conta como "em movimento"
#define IMU_SHOCK_HOLD_MS 250      // uma pancada so conta uma vez
#define IMU_CAL_SAMPLES   200      // janela de calibracao: 2 s parado
#define IMU_CAL_GYRO_DPS  5.0f     // variacao maxima por eixo na janela: tremor passa, giro nao
#define IMU_CAL_TILT_DEG  1.0f     // giro maximo da gravidade entre o 1o e o ultimo quarto

// --- registradores (comuns a MPU-6050 / 6500 / 9250 / ICM-206xx) -----------
#define R_SMPLRT_DIV    0x19
#define R_CONFIG        0x1A
#define R_GYRO_CONFIG   0x1B
#define R_ACCEL_CONFIG  0x1C
#define R_ACCEL_CONFIG2 0x1D   // so na familia 6500/ICM: filtro do acelerometro
#define R_ACCEL_XOUT_H  0x3B   // 14 bytes: acel X/Y/Z, temperatura, giro X/Y/Z
#define R_PWR_MGMT_1    0x6B
#define R_PWR_MGMT_2    0x6C
#define R_WHO_AM_I      0x75

static const float RAD2DEG = 57.29578f;

// Os chips da familia diferem so na formula da temperatura e no registrador
// de filtro separado do acelerometro.
struct Chip { uint8_t who; const char *name; float tSens; float tOff; bool accelCfg2; };
static const Chip kChips[] = {
    { 0x68, "MPU-6050",  340.0f,  36.53f, false },
    { 0x70, "MPU-6500",  333.87f, 21.0f,  true  },
    { 0x71, "MPU-9250",  333.87f, 21.0f,  true  },
    { 0x73, "MPU-9255",  333.87f, 21.0f,  true  },
    { 0x11, "ICM-20600", 326.8f,  25.0f,  true  },
    { 0x12, "ICM-20602", 326.8f,  25.0f,  true  },
    { 0x98, "ICM-20689", 326.8f,  25.0f,  true  },
    { 0xAF, "ICM-20608", 326.8f,  25.0f,  true  },
};
// GY-521 falsificado responde com WHO_AM_I fora da tabela (0x72 e o mais
// visto) mas segue o mapa do MPU-6050. Temperatura pode sair deslocada.
static const Chip kGeneric = { 0, "compativel MPU-6050", 340.0f, 36.53f, false };

// ---------------------------------------------------------------------------
//  Estado compartilhado
// ---------------------------------------------------------------------------
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ImuData s_pub;                    // copia publicada para web/SNMP
static ImuData s_d;                      // estado de trabalho, so a task mexe
static volatile bool s_reconfig = false;
static volatile bool s_calReq   = false;

static uint8_t     s_addr = 0;
static const Chip *s_chip = &kGeneric;
static float       s_accLsb  = 16384.0f; // LSB por g
static float       s_gyroLsb = 131.0f;   // LSB por grau/s

static void publish()
{
    portENTER_CRITICAL(&s_mux);
    s_pub = s_d;
    portEXIT_CRITICAL(&s_mux);
}

ImuData imuGet()
{
    ImuData d;
    portENTER_CRITICAL(&s_mux);
    d = s_pub;
    portEXIT_CRITICAL(&s_mux);
    return d;
}

void imuCalibrate()   { s_calReq = true; }
void imuApplyConfig() { s_reconfig = true; }

// ---------------------------------------------------------------------------
//  I2C
// ---------------------------------------------------------------------------
static bool wr(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(s_addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool rd(uint8_t reg, uint8_t *buf, uint8_t n)
{
    Wire.beginTransmission(s_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;      // repeated start
    if (Wire.requestFrom(s_addr, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

static bool probe()
{
    static const uint8_t kAddrs[] = { 0x68, 0x69 };
    for (uint8_t a : kAddrs) {
        s_addr = a;
        uint8_t who;
        if (!rd(R_WHO_AM_I, &who, 1)) continue;
        s_chip = &kGeneric;
        for (const Chip &c : kChips)
            if (c.who == who) { s_chip = &c; break; }
        s_d.addr   = a;
        s_d.whoami = who;
        if (s_chip == &kGeneric)
            snprintf(s_d.model, sizeof(s_d.model), "%s (0x%02X)", kGeneric.name, who);
        else
            strlcpy(s_d.model, s_chip->name, sizeof(s_d.model));
        return true;
    }
    return false;
}

static uint8_t accelSel(uint8_t g)  { return g >= 16 ? 3 : g >= 8 ? 2 : g >= 4 ? 1 : 0; }
static uint8_t gyroSel(uint16_t d)  { return d >= 2000 ? 3 : d >= 1000 ? 2 : d >= 500 ? 1 : 0; }

static bool configure()
{
    const uint8_t as = accelSel(g_imuCfg.accelG);
    const uint8_t gs = gyroSel(g_imuCfg.gyroDps);

    wr(R_PWR_MGMT_1, 0x80);                  // reset; o ACK pode nao vir
    vTaskDelay(pdMS_TO_TICKS(100));
    bool ok = wr(R_PWR_MGMT_1, 0x01)         // acorda, clock do PLL do giroscopio
           && wr(R_PWR_MGMT_2, 0x00)         // todos os eixos ligados
           && wr(R_CONFIG, 0x03)             // filtro ~44 Hz: abaixo de Nyquist a 100 Hz
           && wr(R_SMPLRT_DIV, 1000 / IMU_RATE_HZ - 1)
           && wr(R_GYRO_CONFIG,  (uint8_t)(gs << 3))
           && wr(R_ACCEL_CONFIG, (uint8_t)(as << 3));
    if (ok && s_chip->accelCfg2) ok = wr(R_ACCEL_CONFIG2, 0x03);   // acel ~45 Hz

    s_accLsb    = 16384.0f / (float)(1 << as);
    s_gyroLsb   = 131.0f   / (float)(1 << gs);
    s_d.rangeG   = (uint8_t)(2 << as);
    s_d.rangeDps = (uint16_t)(250 << gs);
    return ok;
}

// ---------------------------------------------------------------------------
//  Processamento
// ---------------------------------------------------------------------------
static inline int16_t be16(const uint8_t *p) { return (int16_t)((p[0] << 8) | p[1]); }

static void normalize(float *v)
{
    float m = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (m < 1e-6f) { v[0] = 0; v[1] = 0; v[2] = 1; return; }
    v[0] /= m; v[1] /= m; v[2] /= m;
}

// Le o sensor ate ele parar de responder. Todo o estado do filtro e local:
// sensor reconectado comeca do zero, inclusive a calibracao.
static void sampleLoop()
{
    float bias[3] = { 0, 0, 0 };     // graus/s
    float grav[3] = { 0, 0, 1 };     // direcao da gravidade no referencial do sensor
    float gMag    = 1.0f;            // |a| medio: gravidade com o erro de escala do chip
    float wAvg    = 0;               // |w| suavizado
    bool  first   = true;

    float    vibSq = 0, vibPk = 0;
    uint16_t vibN  = 0;

    bool     inShock = false;
    uint32_t shockEndMs = 0;

    const uint16_t calQ = IMU_CAL_SAMPLES / 4;
    uint16_t calN = 0;
    float    calSum[3] = { 0 }, calMin[3] = { 0 }, calMax[3] = { 0 };
    float    calA0[3] = { 0 }, calA1[3] = { 0 };   // soma do acel no 1o / ultimo quarto

    s_d.calibrated  = false;
    s_d.calibrating = true;          // tenta calibrar assim que o sensor fica parado

    uint8_t    fails  = 0;
    uint32_t   lastUs = micros();
    TickType_t wake   = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IMU_PERIOD_MS));

        if (s_reconfig) {
            s_reconfig = false;
            if (!configure()) return;
            s_d.calibrating = true;         // chip resetado: mede o bias de novo
            calN = 0;
            wake = xTaskGetTickCount();     // o reset levou 100 ms
        }
        if (s_calReq) {
            s_calReq = false;
            s_d.calibrating = true;
            calN = 0;
        }

        uint8_t b[14];
        if (!rd(R_ACCEL_XOUT_H, b, sizeof(b))) {
            s_d.errors++;
            publish();
            if (++fails >= IMU_MAX_FAILS) return;
            continue;
        }
        fails = 0;

        const uint32_t nowUs = micros();
        float dt = (float)(nowUs - lastUs) * 1e-6f;
        lastUs = nowUs;
        if (dt <= 0 || dt > 0.1f) dt = IMU_PERIOD_MS / 1000.0f;

        float a[3], w[3], wRaw[3];
        for (int i = 0; i < 3; i++) {
            a[i]    = be16(b + 2 * i)     / s_accLsb;
            wRaw[i] = be16(b + 8 + 2 * i) / s_gyroLsb;
        }
        s_d.tempC = be16(b + 6) / s_chip->tSens + s_chip->tOff;
        const float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

        // --- calibracao do giroscopio -----------------------------------------
        // Parado, o giroscopio deveria ler zero; o que ele le e o bias (ate
        // +-20 graus/s no MPU-6050, e varia com a temperatura). O bias e a media
        // da janela, e essa media so engana se o sensor terminou a janela virado
        // para outro lado -- vibracao vai e volta e se cancela. Por isso o teste
        // e a direcao media da gravidade no 1o e no ultimo quarto da janela: pega
        // rotacao lenta e constante sem ser enganado por vibracao.
        if (s_d.calibrating) {
            if (calN == 0)
                for (int i = 0; i < 3; i++) {
                    calSum[i] = 0;
                    calMin[i] = calMax[i] = wRaw[i];
                    calA0[i] = calA1[i] = 0;
                }
            for (int i = 0; i < 3; i++) {
                calSum[i] += wRaw[i];
                calMin[i] = fminf(calMin[i], wRaw[i]);
                calMax[i] = fmaxf(calMax[i], wRaw[i]);
                if (calN < calQ)                   calA0[i] += a[i];
                if (calN >= IMU_CAL_SAMPLES - calQ) calA1[i] += a[i];
            }
            if (++calN >= IMU_CAL_SAMPLES) {
                normalize(calA0);
                normalize(calA1);
                const float dot = calA0[0] * calA1[0] + calA0[1] * calA1[1] + calA0[2] * calA1[2];
                bool still = acosf(fminf(dot, 1.0f)) * RAD2DEG < IMU_CAL_TILT_DEG;
                for (int i = 0; i < 3; i++)
                    if (calMax[i] - calMin[i] > IMU_CAL_GYRO_DPS) still = false;
                if (still) {
                    for (int i = 0; i < 3; i++) bias[i] = calSum[i] / calN;
                    s_d.calibrated  = true;
                    s_d.calibrating = false;
                    s_d.calMs       = millis();
                    Serial.printf("[imu] giroscopio calibrado: bias %.2f %.2f %.2f graus/s\n",
                                  bias[0], bias[1], bias[2]);
                }
                calN = 0;                   // mexeu: tenta de novo na proxima janela
            }
        }
        for (int i = 0; i < 3; i++) w[i] = wRaw[i] - bias[i];

        // --- direcao da gravidade (filtro complementar) -----------------------
        // O giroscopio gira o vetor gravidade junto com o sensor (dg/dt = -w x g)
        // e o acelerometro corrige a deriva devagar -- mas so quando |a| esta
        // perto da gravidade, senao ele esta medindo movimento e nao inclinacao.
        // Trabalhar no vetor, em vez de integrar pitch/roll, evita a
        // descontinuidade dos angulos de Euler no meio da conta.
        if (first) {
            // A 1a leitura pode cair no meio de um tranco: sem o limite, gMag
            // nasceria em 3 g, nenhuma leitura parada caberia na janela de
            // confianca e o acelerometro nunca mais corrigiria o angulo.
            for (int i = 0; i < 3; i++) grav[i] = a[i];
            normalize(grav);
            gMag  = fminf(fmaxf(mag, IMU_GMAG_MIN), IMU_GMAG_MAX);
            first = false;
        } else {
            const float wx = w[0] / RAD2DEG * dt, wy = w[1] / RAD2DEG * dt,
                        wz = w[2] / RAD2DEG * dt;
            const float g0 = grav[0], g1 = grav[1], g2 = grav[2];
            grav[0] = g0 - (wy * g2 - wz * g1);
            grav[1] = g1 - (wz * g0 - wx * g2);
            grav[2] = g2 - (wx * g1 - wy * g0);
            normalize(grav);
            if (fabsf(mag - gMag) < IMU_ACC_TRUST_G && mag > 0.1f) {
                const float k = 1.0f - (s_d.calibrated ? IMU_ALPHA : IMU_ALPHA_UNCAL);
                for (int i = 0; i < 3; i++)
                    grav[i] += k * (a[i] / mag - grav[i]);
                normalize(grav);
                gMag += (mag - gMag) * 0.005f;     // ~2 s
                gMag  = fminf(fmaxf(gMag, IMU_GMAG_MIN), IMU_GMAG_MAX);
            }
        }
        s_d.roll  = atan2f(grav[1], grav[2]) * RAD2DEG;
        s_d.pitch = atan2f(-grav[0], sqrtf(grav[1] * grav[1] + grav[2] * grav[2])) * RAD2DEG;

        // --- aceleracao linear, vibracao e pancadas ---------------------------
        float l[3];
        for (int i = 0; i < 3; i++) l[i] = a[i] - grav[i] * gMag;
        const float lin = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);

        vibSq += lin * lin;
        if (lin > vibPk) vibPk = lin;
        if (++vibN >= IMU_RATE_HZ) {
            s_d.vibRms  = sqrtf(vibSq / vibN);
            s_d.vibPeak = vibPk;
            vibSq = 0; vibPk = 0; vibN = 0;
        }

        const uint32_t now  = millis();
        const float    thrG = g_imuCfg.shockMg / 1000.0f;
        if (lin >= thrG) {
            if (!inShock) {
                inShock = true;
                s_d.shocks++;
                s_d.shockMs = now ? now : 1;
                s_d.shockG  = lin;
            } else if (lin > s_d.shockG) {
                s_d.shockG = lin;
            }
            shockEndMs = now + IMU_SHOCK_HOLD_MS;
        } else if (inShock && (int32_t)(now - shockEndMs) >= 0) {
            inShock = false;
            Serial.printf("[imu] pancada #%u: pico de %.2f g\n",
                          (unsigned)s_d.shocks, s_d.shockG);
        }

        // --- movimento --------------------------------------------------------
        const float wm = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        wAvg += (wm - wAvg) * 0.2f;                // ~50 ms
        // Sem calibrar, o bias sozinho ja passa do limiar: melhor nao afirmar nada.
        s_d.moving = s_d.calibrated && wAvg > IMU_MOTION_DPS;

        s_d.ax = a[0]; s_d.ay = a[1]; s_d.az = a[2];
        s_d.gx = w[0]; s_d.gy = w[1]; s_d.gz = w[2];
        s_d.lin       = lin;
        s_d.samples++;
        s_d.updatedMs = now ? now : 1;
        publish();
    }
}

// ---------------------------------------------------------------------------
static void imuTask(void *)
{
    // Wire.begin() AQUI, e nao no setup(): a interrupcao do I2C e alocada no
    // core de quem inicializa, e assim ela tambem fica fora do core 1.
    Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_HZ);
    Wire.setTimeOut(20);
    bool avisou = false;

    for (;;) {
        s_reconfig = false;                 // configure() abaixo ja le a config atual
        if (!probe() || !configure()) {
            if (!avisou) {
                Serial.printf("[imu] nenhum MPU-6050 em SDA=IO%d SCL=IO%d (0x68/0x69); "
                              "procurando a cada %d s\n",
                              PIN_IMU_SDA, PIN_IMU_SCL, IMU_PROBE_MS / 1000);
                avisou = true;
            }
            s_d.present = false;
            s_d.moving  = false;
            s_d.calibrating = false;
            publish();
            vTaskDelay(pdMS_TO_TICKS(IMU_PROBE_MS));
            continue;
        }

        s_d.present = true;
        publish();
        Serial.printf("[imu] %s em 0x%02X (WHO_AM_I=0x%02X), +-%u g, +-%u graus/s, %u Hz\n",
                      s_d.model, s_d.addr, s_d.whoami, s_d.rangeG, s_d.rangeDps,
                      IMU_RATE_HZ);

        sampleLoop();                       // so volta quando o sensor some

        Serial.println("[imu] sensor parou de responder");
        s_d.present = false;
        s_d.moving  = false;
        s_d.calibrating = false;
        publish();
        // reinicia o periferico: um escravo resetado no meio de uma leitura
        // pode deixar o barramento travado
        Wire.end();
        Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_HZ);
        Wire.setTimeOut(20);
        avisou = true;
        vTaskDelay(pdMS_TO_TICKS(IMU_PROBE_MS));
    }
}

void imuBegin()
{
    memset(&s_d, 0, sizeof(s_d));
    strlcpy(s_d.model, "-", sizeof(s_d.model));
    s_pub = s_d;
    // Prioridade baixa no core 0: abaixo da pilha TCP/IP e do driver Ethernet.
    xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, 2, nullptr, 0);
}
