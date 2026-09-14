#pragma once
#include <Arduino.h>
#include <IPAddress.h>

void        netBegin();
bool        netLinkUp();
bool        netPhyOk();      // false = ETH.begin() falhou (PHY mudo no MDIO)
void        netRetryProbe(); // sonda MDIO periodica enquanto o PHY nao responder
bool        netHasIp();
IPAddress   netLocalIp();
IPAddress   netSubnet();
IPAddress   netGateway();
IPAddress   netDns();
String      netMac();
uint16_t    netLinkSpeed();
bool        netFullDuplex();
