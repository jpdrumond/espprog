#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Constantes do protocolo PROG (ver README secao 3)
// ---------------------------------------------------------------------------
#define PROG_BAUD            9600
#define PROG_SOF             0x7E
#define PROG_ADDR            0x01
#define PROG_TAIL_BOARD      0x51   // TAIL quando a placa fala
#define PROG_TAIL_PROG       0x81   // TAIL quando o programador fala
#define PROG_CHK_SEED        0x81

#define PROG_CMD_POLL        0x3F   // '?'
#define PROG_CMD_SCREEN      0x53   // 'S'
#define PROG_CMD_BEEP        0x42   // 'B'
#define PROG_CMD_LIGHT       0x4C   // 'L'

#define PROG_FRAME_MIN       8
#define PROG_FRAME_MAX       64
#define PROG_SCREEN_LEN      38     // 0x26 -> 32 bytes de dados

// Codigos de tecla (dois canais independentes)
#define KEY_IDLE             0xB4
#define KEY_A_MINUS          0x54
#define KEY_A_PLUS           0xA8
#define KEY_B_LEFT           0x54
#define KEY_B_RIGHT          0xA8
// "+ e - simultaneos" nao estava no levantamento original: foi CAPTURADO do
// programador real espionando a linha D3. Na escada resistiva os dois caminhos
// em paralelo derrubam mais a leitura (72) do que qualquer tecla sozinha.
#define KEY_A_PLUSMINUS      0x48

// Identificadores logicos de tecla usados na API/web/SNMP
enum ProgKey : uint8_t {
    PK_NONE  = 0,
    PK_MINUS = 1,
    PK_PLUS  = 2,
    PK_LEFT  = 3,
    PK_RIGHT = 4,
    PK_PLUSMINUS = 5,   // + e - ao mesmo tempo
};

// Telas reconhecidas pelo parser
enum ScreenId : uint8_t {
    SCR_UNKNOWN = 0,
    SCR_STATUS  = 1,   // FEC/ABR/BOT/FOT/RF
    SCR_CYCLES  = 2,   // Ciclos / Enc / rpm
    SCR_LANG    = 3,   // Idioma
    SCR_SPLASH  = 4,   // TRIFLEX CONNECT
};

// ---------------------------------------------------------------------------
//  Temporizacao (valores padrao; ajustaveis em runtime pela aba Avancado)
// ---------------------------------------------------------------------------
#define DEF_TURNAROUND_US    3000   // fim do pedido -> 1o bit da resposta (2800..3300)
#define DEF_TAILGAP_US       2100   // fim do 0x81 -> byte 0x00
#define DEF_KEY_HOLD_POLLS   3      // quantos polls a tecla fica ativa
#define INTERBYTE_TIMEOUT_US 5000   // silencio que aborta um quadro pela metade
#define LINK_TIMEOUT_MS      2000   // sem quadro por este tempo -> link caido
#define KEY_WATCHDOG_MS      3000   // solta tecla presa se o cliente sumir

// ---------------------------------------------------------------------------
//  Rede / servicos
// ---------------------------------------------------------------------------
#define DEF_HOSTNAME         "espprog"
#define DEF_SNMP_PORT        161
#define DEF_SNMP_RO          "public"
#define DEF_SNMP_RW          ""      // vazio = SET desabilitado
