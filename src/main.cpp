// ---------------------------------------------------------------------------
//  espprog - emulador do programador de mao da placa TRIFLEX CONNECT v2.36.33,
//            acessivel por Ethernet (web, REST e SNMP v2c).
//
//  Alvo: WT32-ETH01 (ESP32-D0WD + LAN8720).
//  Ver README.md para pinagem, protocolo e procedimento de bancada.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "proglink.h"
#include "netmgr.h"
#include "websrv.h"
#include "snmpagent.h"
#include "scan.h"
#include "imu.h"

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n=== espprog %s ===\n", ESPPROG_VERSION);

    settingsLoad();
    Serial.printf("[cfg] rede=%s ip=%s host=%s tx=%s snmp=%s\n",
                  g_cfg.dhcp ? "DHCP" : "estatico",
                  IPAddress(g_cfg.ip).toString().c_str(),
                  g_cfg.hostname,
                  g_cfg.txEnabled ? "on" : "OFF (modo escuta)",
                  g_cfg.snmpEnabled ? "on" : "off");

    progBegin();     // sobe as tasks do barramento antes da rede
    netBegin();
    webBegin();
    snmpBegin();
    scanBegin();
    imuBegin();      // depois do netBegin: as sondas de PHY tambem mexem no IO32/IO33

    Serial.println("[sys] pronto");
}

// Linha de estado a cada 5 s na UART0. Existe para bancada: com ela, o console
// serial responde sozinho "esta vivo?", "a rede subiu?", "a placa esta falando?"
// sem precisar pegar o boot no ar.
static void heartbeat()
{
    static uint32_t last = 0;
    if (millis() - last < 5000) return;
    last = millis();

    ProgStats st = progGetStats();
    ImuData   im = imuGet();
    Serial.printf("[hb] up=%lus phy=%s eth=%s ip=%s prog=%s polls=%u telas=%u "
                  "ruins=%u/%u imu=%s heap=%u\n",
                  (unsigned long)(millis() / 1000),
                  netPhyOk() ? "ok" : "FALHOU",
                  netLinkUp() ? "up" : "down",
                  netLocalIp().toString().c_str(),
                  progLinkUp() ? "ativo" : "mudo",
                  st.polls, st.screens, st.badFrames, st.badChk,
                  im.present ? "ok" : "ausente",
                  ESP.getFreeHeap());
}

void loop()
{
    webLoop();
    snmpPoll();
    scanService();
    netRetryProbe();
    progEngageService();
    heartbeat();
    delay(1);        // devolve a CPU; o barramento roda em task propria
}
