#pragma once
#include <Arduino.h>

struct Settings {
    // ---- rede -------------------------------------------------------------
    bool     dhcp;
    uint32_t ip, mask, gw, dns1, dns2;   // usados apenas quando dhcp == false
    char     hostname[32];

    // ---- SNMP v2c ---------------------------------------------------------
    bool     snmpEnabled;
    uint16_t snmpPort;
    char     roCommunity[33];
    char     rwCommunity[33];            // vazio = SET desabilitado
    char     sysContact[64];
    char     sysLocation[64];

    // ---- barramento PROG --------------------------------------------------
    bool     txEnabled;                  // false = modo escuta (nao transmite)
    uint16_t turnaroundUs;
    uint16_t tailGapUs;
    uint8_t  keyHoldPolls;
    uint16_t beepUnitMs;                 // unidade de tempo do comando 'B'
    bool     buzzer;
    bool     led;
    uint8_t  ethCombo;                   // combinacao de PHY que funcionou (ver netmgr)

    // ---- varredura automatica de menus (para SNMP) ------------------------
    bool     scanEnabled;
    uint16_t scanIntervalSec;            // periodo entre varreduras
    uint16_t scanIdleSec;                // so varre apos este tempo sem tecla humana
    char     scanSeq[64];                // ex: "+,W1500,-,W1500"

    // ---- interface web ----------------------------------------------------
    bool     authEnabled;
    char     authUser[24];
    char     authPass[32];
};

extern Settings g_cfg;

// Acelerometro/giroscopio. Fica num blob PROPRIO na NVS ("imu"), fora de
// Settings: settingsLoad() descarta o blob principal quando o tamanho muda, e
// acrescentar campos la faria quem atualiza o firmware perder a configuracao
// de rede -- o modulo voltaria para 192.168.1.21.
struct ImuSettings {
    uint8_t  accelG;                     // faixa do acelerometro: 2, 4, 8 ou 16 g
    uint16_t gyroDps;                    // faixa do giroscopio: 250, 500, 1000 ou 2000 graus/s
    uint16_t shockMg;                    // aceleracao (sem a gravidade) que conta como pancada
};

extern ImuSettings g_imuCfg;

// Posicoes de referencia da cancela, gravadas pela aba Sensor. Blob proprio
// ("gate") pelo mesmo motivo do "imu". Cada referencia e a direcao da gravidade
// vista pelo sensor naquela posicao: comparar o vetor inteiro funciona em
// qualquer montagem, sem depender de qual eixo ficou ao longo do braco.
#define GATE_REF_CLOSED  0x01
#define GATE_REF_OPEN    0x02
struct GateSettings {
    uint8_t valid;                       // GATE_REF_CLOSED | GATE_REF_OPEN ja gravadas
    float   closed[3];                   // vetor unitario com a cancela fechada
    float   open[3];                     // vetor unitario com a cancela aberta
};

extern GateSettings g_gateCfg;

void settingsLoad();
void settingsSave();
void settingsFactoryDefaults();
