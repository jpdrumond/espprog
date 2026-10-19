#pragma once
#include <Arduino.h>

// Acelerometro + giroscopio MPU-6050 (modulo GY-521) no I2C. A leitura roda em
// task propria; daqui so sai uma copia consistente do ultimo estado.
struct ImuData {
    bool     present;        // sensor respondendo no I2C
    uint8_t  addr;           // 0x68 (AD0 em GND) ou 0x69 (AD0 em 3V3)
    uint8_t  whoami;         // registrador WHO_AM_I, identifica o chip
    char     model[32];
    uint8_t  rangeG;         // faixa aplicada: 2, 4, 8 ou 16 g
    uint16_t rangeDps;       // faixa aplicada: 250, 500, 1000 ou 2000 graus/s

    float    ax, ay, az;     // aceleracao em g (inclui a gravidade)
    float    gx, gy, gz;     // rotacao em graus/s, ja sem o bias
    float    tempC;          // temperatura do chip -- nao e a do ambiente
    float    pitch, roll;    // inclinacao em graus, pela direcao da gravidade
    float    lin;            // aceleracao sem a gravidade, |a - g|, em g
    float    vibRms;         // RMS de lin na ultima janela de 1 s
    float    vibPeak;        // pico de lin na ultima janela de 1 s
    bool     moving;         // giroscopio acima do limiar de movimento

    bool     calibrated;     // bias do giroscopio medido com o sensor parado
    bool     calibrating;    // esperando uma janela parada para calibrar
    uint32_t calMs;          // millis() da ultima calibracao (0 = nunca)

    uint32_t shocks;         // pancadas detectadas desde o boot
    uint32_t shockMs;        // millis() da ultima pancada (0 = nunca)
    float    shockG;         // pico da ultima pancada, em g

    uint32_t samples;        // leituras boas
    uint32_t errors;         // leituras com erro de I2C
    uint32_t updatedMs;      // millis() da ultima leitura boa (0 = nunca)
};

#define IMU_RATE_HZ 100

void    imuBegin();
ImuData imuGet();
void    imuCalibrate();      // pede nova calibracao do giroscopio (sensor parado)
void    imuApplyConfig();    // reaplica faixas e limiar depois de mudar g_imuCfg
