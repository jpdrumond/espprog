#include "proglink.h"
#include "settings.h"
#include <string.h>
#include "driver/gpio.h"

static HardwareSerial ProgSerial(2);

// ---------------------------------------------------------------------------
//  Estado compartilhado
// ---------------------------------------------------------------------------
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static ScreenSnap g_scr;
static ProgValues g_val;
static ProgStats  g_st;

static volatile uint8_t  keyA = KEY_IDLE, keyB = KEY_IDLE;
static volatile uint16_t keyPollsLeft  = 0;        // 0 = solta, 0xFFFF = segurando
static volatile uint16_t keyMinPolls   = 0;
static volatile uint16_t keyServed     = 0;
static volatile uint32_t keyDeadlineMs = 0;
static volatile uint32_t g_lastUserKeyMs = 0;

struct BeepMsg { uint8_t on, off, rep; };
static QueueHandle_t beepQ = nullptr;
static volatile uint32_t g_ledOffMs = 0;
static volatile uint32_t g_lastPollMs = 0;
static volatile uint32_t g_pauseUntilMs = 0;

static FrameLogEntry g_flog[FRAME_LOG_N];
static volatile uint8_t g_flogHead = 0;
static volatile uint8_t g_flogCount = 0;

// ---------------------------------------------------------------------------
//  Tabela de caracteres do LCD -> UTF-8
//
//  A placa usa um charset proprio (nao e o HD44780 A00 puro): a unica
//  correspondencia confirmada em captura e 0xEA = e-circunflexo, visto em
//  "Portugues-Brasil". Preencha as linhas abaixo conforme identificar os demais
//  acentos na bancada -- a aba "Tela" da interface web tem um botao HEX que
//  mostra o codigo de cada posicao, e /api/screen devolve raw_hex sempre.
//  Bytes >= 0x80 sem mapeamento aparecem como MIDDLE DOT.
// ---------------------------------------------------------------------------
struct CharMap { uint8_t code; const char *utf8; };
static const CharMap kCharMap[] = {
    { 0xEA, "ê" },   // e-circunflexo  (CONFIRMADO em captura)
 // { 0xE1, "á" },   // a-agudo        (a confirmar)
 // { 0xE7, "ç" },   // c-cedilha      (a confirmar)
 // { 0xE3, "ã" },   // a-til          (a confirmar)
 // { 0xF5, "õ" },   // o-til          (a confirmar)
 // { 0xE9, "é" },   // e-agudo        (a confirmar)
 // { 0xED, "í" },   // i-agudo        (a confirmar)
 // { 0xF3, "ó" },   // o-agudo        (a confirmar)
 // { 0xFA, "ú" },   // u-agudo        (a confirmar)
    { 0xDF, "°" },   // grau           (padrao HD44780)
    { 0x7E, "→" },   // seta direita   (padrao HD44780)
    { 0x7F, "←" },   // seta esquerda  (padrao HD44780)
};
static const char *UNMAPPED = "·";   // MIDDLE DOT

void progLineToUtf8(const uint8_t *raw16, char *out, size_t outSz)
{
    size_t o = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t c = raw16[i];
        const char *rep = nullptr;
        for (size_t k = 0; k < sizeof(kCharMap) / sizeof(kCharMap[0]); k++)
            if (kCharMap[k].code == c) { rep = kCharMap[k].utf8; break; }
        if (!rep && c >= 0x20 && c < 0x7E) {
            if (o + 1 >= outSz) break;
            out[o++] = (char)c;
            continue;
        }
        if (!rep) rep = UNMAPPED;
        size_t rl = strlen(rep);
        if (o + rl >= outSz) break;
        memcpy(out + o, rep, rl);
        o += rl;
    }
    out[o] = 0;
}

void progRawToHex(const uint8_t *raw, size_t n, char *out)
{
    static const char *H = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = H[raw[i] >> 4];
        out[i * 2 + 1] = H[raw[i] & 0x0F];
    }
    out[n * 2] = 0;
}

// ---------------------------------------------------------------------------
//  Parser das telas conhecidas
// ---------------------------------------------------------------------------
static int findTok(const uint8_t *b, int n, const char *t)
{
    int tl = (int)strlen(t);
    for (int i = 0; i + tl <= n; i++)
        if (memcmp(b + i, t, tl) == 0) return i;
    return -1;
}

static int8_t readFlag(const uint8_t *b, int n, const char *lbl)
{
    int p = findTok(b, n, lbl);
    if (p < 0) return -1;
    p += (int)strlen(lbl);
    if (p < n && b[p] >= '0' && b[p] <= '9') return (int8_t)(b[p] - '0');
    return -1;
}

// Chamado com o spinlock ja tomado.
static void parseScreen(const uint8_t *b)
{
    const int n = 32;
    uint32_t now = millis();

    // --- tela de status: "FEC:0 ABR:0" / "BOT:0 FOT:0 RF:0" -----------------
    int8_t fec = readFlag(b, n, "FEC:"), abr = readFlag(b, n, "ABR:");
    int8_t bot = readFlag(b, n, "BOT:"), fot = readFlag(b, n, "FOT:");
    int8_t rf  = readFlag(b, n, "RF:");
    if (fec >= 0 || abr >= 0 || bot >= 0 || fot >= 0 || rf >= 0) {
        if (fec >= 0) g_val.fec = fec;
        if (abr >= 0) g_val.abr = abr;
        if (bot >= 0) g_val.bot = bot;
        if (fot >= 0) g_val.fot = fot;
        if (rf  >= 0) g_val.rf  = rf;
        g_val.statusMs = now;
    }

    // --- "Ciclos: 00382400" -------------------------------------------------
    int p = findTok(b, n, "Ciclos");
    if (p >= 0) {
        p += 6;
        while (p < n && (b[p] < '0' || b[p] > '9')) p++;
        uint32_t v = 0; int d = 0;
        while (p < n && b[p] >= '0' && b[p] <= '9' && d < 10) { v = v * 10 + (b[p] - '0'); p++; d++; }
        if (d) { g_val.cycles = v; g_val.cyclesOk = true; g_val.cyclesMs = now; }
    }

    // --- "Enc+0000" ---------------------------------------------------------
    p = findTok(b, n, "Enc");
    if (p >= 0) {
        p += 3;
        int sign = 1;
        if (p < n && (b[p] == '+' || b[p] == '-')) { if (b[p] == '-') sign = -1; p++; }
        int32_t v = 0; int d = 0;
        while (p < n && b[p] >= '0' && b[p] <= '9' && d < 6) { v = v * 10 + (b[p] - '0'); p++; d++; }
        if (d) { g_val.encoder = (int16_t)(sign * v); g_val.encoderOk = true; g_val.encoderMs = now; }
    }

    // --- "222rpm" (digitos imediatamente antes de "rpm") --------------------
    p = findTok(b, n, "rpm");
    if (p > 0) {
        int e = p - 1, s = e;
        while (s >= 0 && b[s] >= '0' && b[s] <= '9') s--;
        s++;
        if (s <= e) {
            uint32_t v = 0;
            for (int i = s; i <= e; i++) v = v * 10 + (b[i] - '0');
            g_val.rpm = (uint16_t)v; g_val.rpmOk = true; g_val.rpmMs = now;
        }
    }

    // --- classificacao da tela ---------------------------------------------
    if      (findTok(b, n, "TRIFLEX") >= 0) g_scr.id = SCR_SPLASH;
    else if (findTok(b, n, "Ciclos")  >= 0) g_scr.id = SCR_CYCLES;
    else if (findTok(b, n, "Idioma")  >= 0) g_scr.id = SCR_LANG;
    else if (findTok(b, n, "FEC:")    >= 0) g_scr.id = SCR_STATUS;
    else                                    g_scr.id = SCR_UNKNOWN;
}

// ---------------------------------------------------------------------------
//  Teclas
// ---------------------------------------------------------------------------
// Resolve as duas teclas logicas nos dois bytes de canal. Precisa olhar o par
// junto, e nao uma de cada vez: "+" e "-" vivem no MESMO canal e, pressionados
// juntos, produzem um terceiro valor (0x48) em vez de um sobrescrever o outro.
static void keysToBytes(ProgKey k1, ProgKey k2, uint8_t *a, uint8_t *b)
{
    *a = KEY_IDLE;
    *b = KEY_IDLE;

    const bool mais  = (k1 == PK_PLUS  || k2 == PK_PLUS  ||
                        k1 == PK_PLUSMINUS || k2 == PK_PLUSMINUS);
    const bool menos = (k1 == PK_MINUS || k2 == PK_MINUS ||
                        k1 == PK_PLUSMINUS || k2 == PK_PLUSMINUS);

    if (mais && menos) *a = KEY_A_PLUSMINUS;
    else if (mais)     *a = KEY_A_PLUS;
    else if (menos)    *a = KEY_A_MINUS;

    if      (k1 == PK_LEFT  || k2 == PK_LEFT)  *b = KEY_B_LEFT;
    else if (k1 == PK_RIGHT || k2 == PK_RIGHT) *b = KEY_B_RIGHT;
}

void progKeyTap(ProgKey a, ProgKey b, bool fromUser)
{
    uint8_t ba, bb;
    keysToBytes(a, b, &ba, &bb);
    uint16_t hold = g_cfg.keyHoldPolls ? g_cfg.keyHoldPolls : DEF_KEY_HOLD_POLLS;
    portENTER_CRITICAL(&mux);
    keyA = ba; keyB = bb;
    keyPollsLeft  = hold;
    keyMinPolls   = hold;
    keyServed     = 0;
    keyDeadlineMs = millis() + 1000;
    if (fromUser) g_lastUserKeyMs = millis();
    portEXIT_CRITICAL(&mux);
}

void progKeyHold(ProgKey a, ProgKey b, bool fromUser)
{
    uint8_t ba, bb;
    keysToBytes(a, b, &ba, &bb);
    uint16_t hold = g_cfg.keyHoldPolls ? g_cfg.keyHoldPolls : DEF_KEY_HOLD_POLLS;
    portENTER_CRITICAL(&mux);
    bool same = (keyA == ba && keyB == bb && keyPollsLeft == 0xFFFF);
    keyA = ba; keyB = bb;
    if (!same) keyServed = 0;
    keyPollsLeft  = 0xFFFF;
    keyMinPolls   = hold;
    keyDeadlineMs = millis() + KEY_WATCHDOG_MS;
    if (fromUser) g_lastUserKeyMs = millis();
    portEXIT_CRITICAL(&mux);
}

void progKeyRelease()
{
    portENTER_CRITICAL(&mux);
    if (keyPollsLeft == 0xFFFF) {
        // garante o minimo de polls mesmo num clique muito curto
        keyPollsLeft = (keyServed >= keyMinPolls) ? 0 : (uint16_t)(keyMinPolls - keyServed);
        if (keyPollsLeft == 0) { keyA = KEY_IDLE; keyB = KEY_IDLE; }
        else keyDeadlineMs = millis() + 1000;
    }
    portEXIT_CRITICAL(&mux);
}

void progGetKeyState(uint8_t *a, uint8_t *b)
{
    portENTER_CRITICAL(&mux);
    *a = keyA; *b = keyB;
    portEXIT_CRITICAL(&mux);
}

uint32_t progLastUserKeyMs() { return g_lastUserKeyMs; }

// chamado depois de cada resposta enviada
static void keyAfterResponse()
{
    portENTER_CRITICAL(&mux);
    if (keyPollsLeft) {
        if (keyServed < 0xFFFE) keyServed++;
        if (keyPollsLeft != 0xFFFF) {
            if (--keyPollsLeft == 0) { keyA = KEY_IDLE; keyB = KEY_IDLE; }
        }
    }
    portEXIT_CRITICAL(&mux);
}

// watchdog de tecla presa, chamado periodicamente pela task de saidas
static void keyWatchdog()
{
    portENTER_CRITICAL(&mux);
    if (keyPollsLeft && (int32_t)(millis() - keyDeadlineMs) > 0) {
        keyPollsLeft = 0; keyA = KEY_IDLE; keyB = KEY_IDLE;
    }
    portEXIT_CRITICAL(&mux);
}

const char *progKeyName(ProgKey k)
{
    switch (k) {
        case PK_MINUS: return "minus";
        case PK_PLUS:  return "plus";
        case PK_LEFT:  return "left";
        case PK_RIGHT: return "right";
        case PK_PLUSMINUS: return "plusminus";
        default:       return "none";
    }
}

ProgKey progKeyFromName(const char *s)
{
    if (!s || !*s) return PK_NONE;
    if (!strcasecmp(s, "minus") || !strcmp(s, "-") || !strcasecmp(s, "menos")) return PK_MINUS;
    if (!strcasecmp(s, "plus")  || !strcmp(s, "+") || !strcasecmp(s, "mais"))  return PK_PLUS;
    if (!strcasecmp(s, "left")  || !strcmp(s, "<") || !strcasecmp(s, "esq"))   return PK_LEFT;
    if (!strcasecmp(s, "right") || !strcmp(s, ">") || !strcasecmp(s, "dir"))   return PK_RIGHT;
    if (!strcasecmp(s, "plusminus") || !strcmp(s, "+-") || !strcasecmp(s, "maismenos"))
        return PK_PLUSMINUS;
    return PK_NONE;
}

// ---------------------------------------------------------------------------
//  Leitura do estado (para web / SNMP)
// ---------------------------------------------------------------------------
void progGetScreen(ScreenSnap *out)
{
    portENTER_CRITICAL(&mux);
    memcpy(out, &g_scr, sizeof(ScreenSnap));
    portEXIT_CRITICAL(&mux);
}

ProgValues progGetValues()
{
    ProgValues v;
    portENTER_CRITICAL(&mux);
    v = g_val;
    portEXIT_CRITICAL(&mux);
    return v;
}

ProgStats progGetStats()
{
    ProgStats s;
    portENTER_CRITICAL(&mux);
    s = g_st;
    portEXIT_CRITICAL(&mux);
    return s;
}

bool progLinkUp()
{
    uint32_t last;
    portENTER_CRITICAL(&mux);
    last = g_st.lastFrameMs;
    portEXIT_CRITICAL(&mux);
    return last && (millis() - last) < LINK_TIMEOUT_MS;
}

// ---------------------------------------------------------------------------
//  Motor do protocolo
// ---------------------------------------------------------------------------
static uint8_t frameChk(const uint8_t *f, int len)
{
    uint8_t x = PROG_CHK_SEED;
    for (int i = 1; i <= len - 3; i++) x ^= f[i];
    return x;
}

static inline void waitUntilUs(uint32_t deadline)
{
    int32_t rem = (int32_t)(deadline - micros());
    if (rem <= 0) return;
    if (rem > 1500) vTaskDelay(pdMS_TO_TICKS((rem - 1200) / 1000));
    while ((int32_t)(deadline - micros()) > 0) { /* spin curto e deterministico */ }
}

static void handleFrame(const uint8_t *f, int len, uint32_t endUs)
{
    // Aceita os dois sentidos do barramento: TAIL 0x51 quando a placa fala e
    // 0x81 quando um programador fala. Escutar o lado do programador e o que
    // permite capturar os bytes de tecla -- inclusive combinacoes que o
    // levantamento original nao registrou, como "+ e - simultaneos".
    const bool daPlaca = (f[len - 1] == PROG_TAIL_BOARD);
    const bool doProg  = (f[len - 1] == PROG_TAIL_PROG);
    bool badShape = (f[2] != PROG_ADDR || !(daPlaca || doProg));
    if (badShape || frameChk(f, len) != f[len - 2]) {
        portENTER_CRITICAL(&mux);
        if (badShape) g_st.badFrames++; else g_st.badChk++;
        portEXIT_CRITICAL(&mux);
        return;                       // quadro invalido: nao responder
    }

    const uint8_t cmd = f[3];
    const uint32_t now = millis();

    portENTER_CRITICAL(&mux);
    {
        FrameLogEntry &e = g_flog[g_flogHead];
        e.ms  = now;
        e.len = (uint8_t)((len > 40) ? 40 : len);
        memcpy(e.data, f, e.len);
        g_flogHead = (uint8_t)((g_flogHead + 1) % FRAME_LOG_N);
        if (g_flogCount < FRAME_LOG_N) g_flogCount++;
    }
    portEXIT_CRITICAL(&mux);
    static uint32_t lastPollUs = 0;

    if (doProg) {
        // Quadro de OUTRO programador: interessa apenas para espionagem. Fica
        // registrado no log, mas nao conta como trafego da placa e, claro, nao
        // se responde a ele.
        portENTER_CRITICAL(&mux);
        g_st.frames++;
        g_st.lastFrameMs = now;
        portEXIT_CRITICAL(&mux);
        return;
    }

    portENTER_CRITICAL(&mux);
    g_st.frames++;
    g_st.lastFrameMs = now;

    switch (cmd) {
    case PROG_CMD_POLL:
        g_st.polls++;
        g_lastPollMs = now;
        if (lastPollUs) {
            uint32_t dt = endUs - lastPollUs;
            if (dt < 500000)
                g_st.pollPeriodUs = g_st.pollPeriodUs ? (g_st.pollPeriodUs * 7 + dt) / 8 : dt;
        }
        lastPollUs = endUs;
        break;

    case PROG_CMD_SCREEN:
        if (len == PROG_SCREEN_LEN) {
            g_st.screens++;
            g_scr.receivedMs = now;
            if (memcmp(g_scr.raw, f + 4, 32) != 0) {
                memcpy(g_scr.raw, f + 4, 32);
                g_scr.seq++;
                g_scr.updatedMs = now;
                parseScreen(g_scr.raw);
            }
        }
        break;

    case PROG_CMD_BEEP:
        if (len >= 9) g_st.beeps++;
        break;

    case PROG_CMD_LIGHT:
        g_st.lights++;
        break;

    default:
        g_st.unknownCmd++;
        break;
    }
    portEXIT_CRITICAL(&mux);

    if (cmd == PROG_CMD_BEEP && len >= 9 && g_cfg.buzzer && beepQ) {
        BeepMsg m = { f[4], f[5], f[6] };
        xQueueSend(beepQ, &m, 0);
    }

    if (g_cfg.led) { digitalWrite(PIN_LED, HIGH); g_ledOffMs = now + 25; }

    if (!g_cfg.txEnabled) return;      // modo escuta: nunca transmite
    if (g_pauseUntilMs && (int32_t)(millis() - g_pauseUntilMs) < 0)
        return;                        // pausa deliberada para reengatar

    uint8_t a, b;
    progGetKeyState(&a, &b);
    uint8_t r[8] = { PROG_SOF, 0x08, PROG_ADDR, cmd, a, b, 0x00, PROG_TAIL_PROG };
    r[6] = frameChk(r, 8);

    waitUntilUs(endUs + g_cfg.turnaroundUs);
    ProgSerial.write(r, 8);
    ProgSerial.flush();                // TX-only: nao descarta o buffer de RX
    delayMicroseconds(g_cfg.tailGapUs);
    ProgSerial.write((uint8_t)0x00);
    ProgSerial.flush();

    portENTER_CRITICAL(&mux); g_st.responses++; portEXIT_CRITICAL(&mux);
    keyAfterResponse();
}

static void progTask(void *)
{
    uint8_t f[PROG_FRAME_MAX];
    int idx = 0, need = 0;
    uint32_t lastByteUs = 0;

    for (;;) {
        if (idx > 0 && (micros() - lastByteUs) > INTERBYTE_TIMEOUT_US) {
            idx = 0; need = 0;
            portENTER_CRITICAL(&mux); g_st.badFrames++; portEXIT_CRITICAL(&mux);
        }

        if (!ProgSerial.available()) {
            if (idx > 0) delayMicroseconds(100);   // dentro de quadro: amostragem fina
            else         vTaskDelay(1);            // ocioso: devolve a CPU
            continue;
        }

        int c = ProgSerial.read();
        lastByteUs = micros();

        if (idx == 0) {
            if (c != PROG_SOF) continue;           // ruido / diafonia
            f[0] = (uint8_t)c; idx = 1;
        } else if (idx == 1) {
            if (c < PROG_FRAME_MIN || c > PROG_FRAME_MAX) {
                idx = 0;
                portENTER_CRITICAL(&mux); g_st.badFrames++; portEXIT_CRITICAL(&mux);
                continue;
            }
            f[1] = (uint8_t)c; need = c; idx = 2;
        } else {
            f[idx++] = (uint8_t)c;
            if (idx >= need) {
                handleFrame(f, need, lastByteUs);
                idx = 0; need = 0;
            }
        }
    }
}

// task de saidas: buzzer, LED de atividade e watchdog de tecla
static void outputTask(void *)
{
    BeepMsg m;
    for (;;) {
        if (beepQ && xQueueReceive(beepQ, &m, pdMS_TO_TICKS(10)) == pdTRUE) {
            uint8_t rep = m.rep ? m.rep : 1;
            for (uint8_t i = 0; i < rep && i < 10; i++) {
                digitalWrite(PIN_BUZZER, HIGH);
                vTaskDelay(pdMS_TO_TICKS((uint32_t)m.on * g_cfg.beepUnitMs));
                digitalWrite(PIN_BUZZER, LOW);
                if (i + 1 < rep) vTaskDelay(pdMS_TO_TICKS((uint32_t)m.off * g_cfg.beepUnitMs));
            }
        }
        if (g_ledOffMs && (int32_t)(millis() - g_ledOffMs) > 0) {
            digitalWrite(PIN_LED, LOW);
            g_ledOffMs = 0;
        }
        keyWatchdog();
    }
}

void progBegin()
{
    memset(&g_scr, 0, sizeof(g_scr));
    memset(&g_val, 0, sizeof(g_val));
    memset(&g_st,  0, sizeof(g_st));
    memset(g_scr.raw, ' ', sizeof(g_scr.raw));
    g_val.fec = g_val.abr = g_val.bot = g_val.fot = g_val.rf = -1;

    pinMode(PIN_LED, OUTPUT);    digitalWrite(PIN_LED, LOW);
    pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);

    ProgSerial.begin(PROG_BAUD, SERIAL_8N1, PIN_PROG_RX, PIN_PROG_TX);
    ProgSerial.setRxFIFOFull(1);   // cada byte cai no ring buffer na hora
    ProgSerial.setRxTimeout(1);

    beepQ = xQueueCreate(8, sizeof(BeepMsg));

    // Prioridade alta e afinidade no core 1: o loopTask do Arduino (HTTP/SNMP)
    // roda no mesmo core com prioridade 1 e e preemptado sem afetar o turnaround.
    progApplyTxMode();

    xTaskCreatePinnedToCore(progTask,   "proglink", 4096, nullptr, 19, nullptr, 1);
    xTaskCreatePinnedToCore(outputTask, "progout",  2560, nullptr,  4, nullptr, 1);
}

// ---------------------------------------------------------------------------
//  Diagnostico eletrico da linha de RX
//
//  Le o pino cru com digitalRead, que enxerga o pad independente do roteamento
//  da matriz de GPIO -- a UART continua funcionando normalmente durante a
//  amostragem. O menor pulso medido revela o baud: 1 bit a 9600 = 104 us.
// ---------------------------------------------------------------------------
RxDiag progDiagRx(uint32_t ms, int pull)
{
    // O Serial.begin() liga o pull-up INTERNO do ESP no pino de RX. Com ele
    // ativo a linha le alto mesmo que o circuito externo esteja desconectado --
    // e isso mascara a ausencia do pull-up externo. gpio_set_pull_mode muda so
    // a configuracao de pull, sem desfazer o roteamento da UART.
    if (pull == 1) gpio_set_pull_mode((gpio_num_t)PIN_PROG_RX, GPIO_FLOATING);
    else if (pull == 3) gpio_set_pull_mode((gpio_num_t)PIN_PROG_RX, GPIO_PULLDOWN_ONLY);
    else if (pull == 2) gpio_set_pull_mode((gpio_num_t)PIN_PROG_RX, GPIO_PULLUP_ONLY);
    if (pull) delayMicroseconds(500);

    RxDiag d = {};
    d.minPulseUs = 0xFFFFFFFF;

    const uint32_t t0 = micros();
    const uint32_t win = ms * 1000UL;
    int last = digitalRead(PIN_PROG_RX);
    uint32_t lastEdge = t0;

    while ((micros() - t0) < win) {
        int v = digitalRead(PIN_PROG_RX);
        d.samples++;
        if (v) d.high++;
        if (v != last) {
            uint32_t now = micros();
            uint32_t w = now - lastEdge;
            if (d.transitions) {              // ignora o primeiro intervalo
                if (w < d.minPulseUs) d.minPulseUs = w;
                if (w > d.maxPulseUs) d.maxPulseUs = w;
            }
            d.transitions++;
            lastEdge = now;
            last = v;
        }
    }
    d.windowUs = micros() - t0;
    if (d.minPulseUs == 0xFFFFFFFF) d.minPulseUs = 0;
    if (pull) gpio_set_pull_mode((gpio_num_t)PIN_PROG_RX, GPIO_PULLUP_ONLY);
    return d;
}

// ---------------------------------------------------------------------------
//  Captura de bordas
// ---------------------------------------------------------------------------
void progCaptureEdges(EdgeCap *out, uint32_t timeoutMs)
{
    out->n = 0;
    out->waitUs = 0;

    const uint32_t t0 = micros();
    const uint32_t limit = timeoutMs * 1000UL;

    int lvl = digitalRead(PIN_PROG_RX);
    out->firstLevel = (uint8_t)lvl;

    // espera a primeira transicao
    while ((micros() - t0) < limit) {
        int v = digitalRead(PIN_PROG_RX);
        if (v != lvl) { lvl = v; break; }
    }
    uint32_t last = micros();
    out->waitUs = last - t0;
    if ((last - t0) >= limit) return;          // nada aconteceu

    // registra ate encher ou ate 20 ms de silencio
    while (out->n < EDGE_CAP_MAX) {
        uint32_t start = micros();
        int v;
        do {
            v = digitalRead(PIN_PROG_RX);
            if ((micros() - start) > 20000UL) { return; }
        } while (v == lvl);
        uint32_t now = micros();
        uint32_t d = now - last;
        out->dt[out->n++] = (d > 65535) ? 65535 : (uint16_t)d;
        last = now;
        lvl = v;
    }
}

size_t progTxRaw(const uint8_t *data, size_t n)
{
    ProgSerial.write(data, n);
    ProgSerial.flush();
    return n;
}

void progApplyTxMode()
{
    if (g_cfg.txEnabled) {
        ProgSerial.begin(PROG_BAUD, SERIAL_8N1, PIN_PROG_RX, PIN_PROG_TX);
        ProgSerial.setRxFIFOFull(1);
        ProgSerial.setRxTimeout(1);
        Serial.println("[prog] TX habilitado (IO17 como saida de UART)");
    } else {
        pinMode(PIN_PROG_TX, INPUT);   // alta impedancia: escuta de verdade
        Serial.println("[prog] modo escuta: IO17 em ALTA IMPEDANCIA");
    }
}

uint8_t progGetFrameLog(FrameLogEntry *out, uint8_t max)
{
    portENTER_CRITICAL(&mux);
    uint8_t n = g_flogCount < max ? g_flogCount : max;
    uint8_t idx = (uint8_t)((g_flogHead + FRAME_LOG_N - n) % FRAME_LOG_N);
    for (uint8_t i = 0; i < n; i++) {
        out[i] = g_flog[idx];
        idx = (uint8_t)((idx + 1) % FRAME_LOG_N);
    }
    portEXIT_CRITICAL(&mux);
    return n;
}

void progTxHoldLow(uint32_t ms)
{
    pinMode(PIN_PROG_TX, OUTPUT);
    digitalWrite(PIN_PROG_TX, LOW);
    delay(ms);
    progApplyTxMode();            // devolve o pino ao estado configurado
}

uint32_t progLastPollMs() { return g_lastPollMs; }

void progPauseResponses(uint32_t ms) { g_pauseUntilMs = millis() + ms; }

static uint8_t s_engageTry = 0;
uint8_t progEngageTries() { return s_engageTry; }

#define ENGAGE_IDLE_MS   8000    // sem poll por este tempo -> tentar reengatar
#define ENGAGE_GAP_MS   12000    // espera entre tentativas

void progEngageService()
{
    // So faz sentido se a placa esta falando (chegam quadros) mas nao polla.
    if (!g_cfg.txEnabled) return;
    if (!progLinkUp())    return;

    const uint32_t now = millis();
    if (g_lastPollMs && (now - g_lastPollMs) < ENGAGE_IDLE_MS) {
        s_engageTry = 0;               // sessao saudavel
        return;
    }
    if (!g_lastPollMs && now < ENGAGE_IDLE_MS) return;   // ainda no boot

    static uint32_t nextMs = 0;
    if (nextMs && (int32_t)(now - nextMs) < 0) return;
    nextMs = now + ENGAGE_GAP_MS;

    s_engageTry++;
    switch (s_engageTry) {
    case 1:
        Serial.println("[prog] sem polls: pausando respostas 1,5 s para reengatar");
        progPauseResponses(1500);
        break;
    case 2:
        Serial.println("[prog] sem polls: reinicializando a UART do barramento");
        ProgSerial.end();
        delay(300);
        progApplyTxMode();
        break;
    default:
        Serial.println("[prog] sem polls apos varias tentativas: reiniciando o modulo");
        Serial.flush();
        delay(200);
        ESP.restart();
    }
}

void progKeyRawTap(uint8_t a, uint8_t b)
{
    uint16_t hold = g_cfg.keyHoldPolls ? g_cfg.keyHoldPolls : DEF_KEY_HOLD_POLLS;
    portENTER_CRITICAL(&mux);
    keyA = a; keyB = b;
    keyPollsLeft  = hold;
    keyMinPolls   = hold;
    keyServed     = 0;
    keyDeadlineMs = millis() + 1000;
    g_lastUserKeyMs = millis();
    portEXIT_CRITICAL(&mux);
}
