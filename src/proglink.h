#pragma once
#include <Arduino.h>
#include "config.h"

// Snapshot do LCD 2x16 (bytes crus, como vieram do comando 'S')
struct ScreenSnap {
    uint8_t  raw[32];
    uint32_t seq;          // incrementa a cada mudanca de conteudo
    uint32_t updatedMs;    // millis() da ultima mudanca
    uint32_t receivedMs;   // millis() do ultimo 'S' recebido (mesmo sem mudanca)
    uint8_t  id;           // ScreenId
};

// Valores extraidos das telas conhecidas. age* = millis() da ultima leitura.
struct ProgValues {
    int8_t   fec, abr, bot, fot, rf;   // -1 = nunca visto
    uint32_t statusMs;

    uint32_t cycles;   bool cyclesOk;   uint32_t cyclesMs;
    int16_t  encoder;  bool encoderOk;  uint32_t encoderMs;
    uint16_t rpm;      bool rpmOk;      uint32_t rpmMs;
};

struct ProgStats {
    uint32_t frames, polls, screens, beeps, lights, unknownCmd;
    uint32_t badFrames, badChk;
    uint32_t lastFrameMs;
    uint32_t pollPeriodUs;   // media movel do intervalo entre polls
    uint32_t responses;
};

void progBegin();

// Leitura (thread-safe) --------------------------------------------------
void       progGetScreen(ScreenSnap *out);
ProgValues progGetValues();
ProgStats  progGetStats();
bool       progLinkUp();

// Teclas -----------------------------------------------------------------
// tap: segura por keyHoldPolls polls e solta sozinha.
// hold/release: segura ate release (ou watchdog), respeitando o minimo de polls.
void progKeyTap(ProgKey a, ProgKey b, bool fromUser = true);
void progKeyHold(ProgKey a, ProgKey b, bool fromUser = true);
void progKeyRelease();
void progGetKeyState(uint8_t *a, uint8_t *b);
uint32_t progLastUserKeyMs();

// Utilidades -------------------------------------------------------------
const char *progKeyName(ProgKey k);
ProgKey     progKeyFromName(const char *s);
// Converte os 32 bytes crus em UTF-8 (2 linhas de 16). out precisa de >= 80 bytes.
void        progLineToUtf8(const uint8_t *raw16, char *out, size_t outSz);
void        progRawToHex(const uint8_t *raw, size_t n, char *out);

// Diagnostico eletrico da linha de RX. Amostra o pino cru, sem tocar na
// configuracao da UART -- serve para distinguir "linha parada" de "linha com
// atividade que a UART nao decodifica".
struct RxDiag {
    uint32_t samples;       // amostras tomadas
    uint32_t high;          // quantas em nivel alto
    uint32_t transitions;   // bordas detectadas
    uint32_t minPulseUs;    // menor pulso -> 1 bit time (104 us @ 9600)
    uint32_t maxPulseUs;
    uint32_t windowUs;      // duracao real da janela
};
// pull: 0 = nao mexe, 1 = sem pull, 2 = pull-up, 3 = pull-down.
// Com pull-down interno, a linha so fica alta se houver pull-up EXTERNO.
RxDiag progDiagRx(uint32_t ms, int pull = 0);

// Captura de bordas da linha de RX. Espera atividade, entao registra o tempo
// entre transicoes sucessivas. Com isso da para medir o baud real e decodificar
// os bytes na mao -- min/max de pulso nao distinguem dado de ruido.
#define EDGE_CAP_MAX 200
struct EdgeCap {
    uint16_t n;
    uint8_t  firstLevel;
    uint32_t waitUs;            // quanto esperou ate a primeira borda
    uint16_t dt[EDGE_CAP_MAX];  // us entre bordas (saturado em 65535)
};
void progCaptureEdges(EdgeCap *out, uint32_t timeoutMs);

// Transmite bytes crus no barramento PROG. Existe para bancada: o levantamento
// assume que a placa inicia a conversa, mas isso nunca foi verificado com o
// conector vazio. Talvez ela so responda depois que o programador se anuncia.
// Retorna quantos bytes enviou. Ignora o modo escuta de proposito.
size_t progTxRaw(const uint8_t *data, size_t n);

// Aplica o modo de transmissao ao pino de TX. Em modo escuta o IO17 vai para
// ALTA IMPEDANCIA -- deixar de enviar quadros nao basta: como saida de UART ele
// repousa em 3,3 V e, quando a placa (ou outro programador) puxa o D3 para
// baixo, o diodo de corpo do MOSFET liga o transistor e o nosso driver passa a
// brigar com quem esta falando. Chamar sempre que txEnabled mudar.
void progApplyTxMode();

// Log circular dos ultimos quadros validos recebidos, em bytes crus. Contadores
// agregados nao mostram O QUE chega -- aqui da para ver o CMD, o tamanho e o
// intervalo real entre quadros.
#define FRAME_LOG_N 16
struct FrameLogEntry {
    uint32_t ms;      // millis() em que chegou
    uint8_t  len;
    uint8_t  data[40];
};
uint8_t progGetFrameLog(FrameLogEntry *out, uint8_t max);

// Segura o pino de TX em nivel baixo por N ms, para medir o D3 com multimetro.
// E o teste espelhado do que fizemos no RX: se o Q3 conduzir, o D3 sai de ~9 V
// e vai para perto de 0. Sem isso, nossa resposta nunca chega na placa.
void progTxHoldLow(uint32_t ms);

// --- reengate automatico ---------------------------------------------------
// Se a cancela reiniciar com o ESP ligado, ela volta a mandar so o splash de
// segundo em segundo e nunca comeca a pollar. Foi observado em bancada que um
// reinicio do ESP reengata a sessao -- a hipotese e que ela reconhece "as
// respostas pararam e voltaram" como um programador novo se conectando.
// Isto reproduz esse efeito sem derrubar web e SNMP, escalando so se preciso.
uint32_t progLastPollMs();
void     progPauseResponses(uint32_t ms);
void     progEngageService();      // chamar no loop()
uint8_t  progEngageTries();

// Define os bytes de tecla diretamente, sem passar pelo mapa de teclas logicas.
// Existe porque "+ e - juntos" nao foi capturado no levantamento: na escada
// resistiva original os dois no mesmo canal produzem um TERCEIRO valor, que
// precisa ser descoberto na bancada. Ver README secao de teclas.
void progKeyRawTap(uint8_t a, uint8_t b);
