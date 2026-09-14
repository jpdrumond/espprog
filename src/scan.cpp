#include "scan.h"
#include "settings.h"
#include "proglink.h"

#define MAX_STEPS 24
#define KEY_SETTLE_MS 250     // espera implicita depois de cada tecla

struct Step { char type; uint16_t val; };   // 'K' = tecla, 'W' = espera em ms

static Step    s_steps[MAX_STEPS];
static int     s_nSteps  = 0;
static int     s_cur     = -1;              // -1 = parado
static uint32_t s_nextMs = 0;
static uint32_t s_lastRunMs = 0;

static int parseSeq(const char *seq)
{
    int n = 0;
    const char *p = seq;
    while (*p && n < MAX_STEPS) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        char tok[16]; int t = 0;
        while (*p && *p != ',' && *p != ' ' && t < (int)sizeof(tok) - 1) tok[t++] = *p++;
        tok[t] = 0;
        if (!t) continue;

        if (tok[0] == 'W' || tok[0] == 'w') {
            long ms = atol(tok + 1);
            if (ms < 0) ms = 0;
            if (ms > 20000) ms = 20000;
            s_steps[n++] = { 'W', (uint16_t)ms };
            continue;
        }
        ProgKey k = progKeyFromName(tok);
        if (k != PK_NONE) s_steps[n++] = { 'K', (uint16_t)k };
    }
    return n;
}

static void start()
{
    s_nSteps = parseSeq(g_cfg.scanSeq);
    if (!s_nSteps) { s_cur = -1; return; }
    s_cur       = 0;
    s_nextMs    = millis();
    s_lastRunMs = millis();
}

void scanBegin()
{
    s_cur = -1;
    s_lastRunMs = millis();
}

void scanTrigger() { start(); }

bool scanRunning() { return s_cur >= 0; }

void scanService()
{
    // --- varredura em andamento ---------------------------------------------
    if (s_cur >= 0) {
        if ((int32_t)(millis() - s_nextMs) < 0) return;
        if (s_cur >= s_nSteps) { s_cur = -1; s_lastRunMs = millis(); return; }

        Step &st = s_steps[s_cur++];
        if (st.type == 'K') {
            progKeyTap((ProgKey)st.val, PK_NONE, false);   // fromUser = false
            s_nextMs = millis() + KEY_SETTLE_MS;
        } else {
            s_nextMs = millis() + st.val;
        }
        return;
    }

    // --- decide se comeca uma nova ------------------------------------------
    if (!g_cfg.scanEnabled) return;
    if (!progLinkUp()) return;
    if ((millis() - progLastUserKeyMs()) < (uint32_t)g_cfg.scanIdleSec * 1000UL) return;
    if ((millis() - s_lastRunMs) < (uint32_t)g_cfg.scanIntervalSec * 1000UL) return;
    start();
}
