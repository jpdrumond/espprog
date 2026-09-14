#pragma once
#include <Arduino.h>

struct SnmpStats {
    uint32_t rxPackets;
    uint32_t txPackets;
    uint32_t badPackets;
    uint32_t authFailures;
    uint32_t sets;
};

void       snmpBegin();
void       snmpPoll();
void       snmpRestart();      // reabre o socket apos mudanca de configuracao
SnmpStats  snmpGetStats();
