#pragma once
#include <Arduino.h>

// Varredura automatica de menus: aperta teclas sozinha para trazer as telas de
// ciclos / rpm / encoder e voltar, mantendo os valores do SNMP atualizados
// mesmo sem ninguem mexendo na interface.
void scanBegin();
void scanService();     // chamar no loop()
void scanTrigger();     // dispara agora, mesmo com a varredura desabilitada
bool scanRunning();
