#include "websrv.h"
#include "web_ui.h"
#include "settings.h"
#include "proglink.h"
#include "netmgr.h"
#include "snmpagent.h"
#include "scan.h"
#include "imu.h"
#include <WebServer.h>
#include <stdarg.h>

static WebServer server(80);
static volatile uint32_t s_rebootAt = 0;

// ---------------------------------------------------------------------------
//  Montagem de JSON em buffer fixo (evita churn de String a cada 250 ms)
// ---------------------------------------------------------------------------
static char   JB[2800];
static size_t JP;

static void jreset() { JP = 0; JB[0] = 0; }

static void jf(const char *fmt, ...)
{
    if (JP + 1 >= sizeof(JB)) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(JB + JP, sizeof(JB) - JP, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t room = sizeof(JB) - JP - 1;
    JP += ((size_t)n > room) ? room : (size_t)n;
}

// escapa uma string para dentro de aspas JSON
static void jesc(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (; *s && o + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
        else if (c == '\n')        { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c < 0x20)         { o += snprintf(out + o, cap - o, "\\u%04x", c); }
        else                       { out[o++] = c; }
    }
    out[o] = 0;
}

static int32_t ageMs(uint32_t stamp) { return stamp ? (int32_t)(millis() - stamp) : -1; }

static void sendJson()
{
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", JB);
}

// ---------------------------------------------------------------------------
//  Autenticacao opcional
// ---------------------------------------------------------------------------
static bool guard()
{
    if (!g_cfg.authEnabled) return true;
    if (server.authenticate(g_cfg.authUser, g_cfg.authPass)) return true;
    server.requestAuthentication();
    return false;
}

// ---------------------------------------------------------------------------
//  Handlers
// ---------------------------------------------------------------------------
static void hIndex()
{
    if (!guard()) return;
    server.sendHeader("Content-Encoding", "identity");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void screenJson(char *l1e, char *l2e, char *hex, ScreenSnap *scr)
{
    char l1[80], l2[80];
    progLineToUtf8(scr->raw,      l1, sizeof(l1));
    progLineToUtf8(scr->raw + 16, l2, sizeof(l2));
    jesc(l1, l1e, 100);
    jesc(l2, l2e, 100);
    progRawToHex(scr->raw, 32, hex);
}

static void hScreen()
{
    if (!guard()) return;
    ScreenSnap scr; progGetScreen(&scr);
    char l1e[100], l2e[100], hex[70];
    screenJson(l1e, l2e, hex, &scr);
    jreset();
    jf("{\"l1\":\"%s\",\"l2\":\"%s\",\"raw\":\"%s\",\"id\":%u,\"seq\":%u,"
       "\"age_ms\":%d,\"rx_age_ms\":%d}",
       l1e, l2e, hex, scr.id, scr.seq, ageMs(scr.updatedMs), ageMs(scr.receivedMs));
    sendJson();
}

static void hScreenTxt()
{
    if (!guard()) return;
    ScreenSnap scr; progGetScreen(&scr);
    char l1[80], l2[80], out[170];
    progLineToUtf8(scr.raw,      l1, sizeof(l1));
    progLineToUtf8(scr.raw + 16, l2, sizeof(l2));
    snprintf(out, sizeof(out), "%s\n%s\n", l1, l2);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; charset=utf-8", out);
}

static void valuesJson()
{
    ProgValues v = progGetValues();
    jf("\"fec\":%d,\"abr\":%d,\"bot\":%d,\"fot\":%d,\"rf\":%d,\"status_age\":%d,"
       "\"cycles\":%u,\"cycles_age\":%d,\"rpm\":%u,\"rpm_age\":%d,"
       "\"enc\":%d,\"enc_age\":%d",
       v.fec, v.abr, v.bot, v.fot, v.rf, ageMs(v.statusMs),
       v.cycles, v.cyclesOk ? ageMs(v.cyclesMs) : -1,
       v.rpm,    v.rpmOk    ? ageMs(v.rpmMs)    : -1,
       v.encoder, v.encoderOk ? ageMs(v.encoderMs) : -1);
}

static void hValues()
{
    if (!guard()) return;
    jreset(); jf("{"); valuesJson(); jf("}");
    sendJson();
}

// Acelerometro/giroscopio. Unidades: g, graus/s, graus, graus Celsius.
static void imuJson()
{
    ImuData im = imuGet();
    char model[50];
    jesc(im.model, model, sizeof(model));
    jf("\"present\":%s,\"model\":\"%s\",\"addr\":\"0x%02X\",\"range_g\":%u,"
       "\"range_dps\":%u,\"rate_hz\":%u,",
       im.present ? "true" : "false", model, im.addr, im.rangeG, im.rangeDps,
       (unsigned)IMU_RATE_HZ);
    jf("\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,"
       "\"temp\":%.1f,\"pitch\":%.2f,\"roll\":%.2f,",
       im.ax, im.ay, im.az, im.gx, im.gy, im.gz, im.tempC, im.pitch, im.roll);
    jf("\"lin\":%.3f,\"vib_rms\":%.3f,\"vib_peak\":%.3f,\"moving\":%s,"
       "\"shocks\":%u,\"shock_g\":%.2f,\"shock_age\":%d,\"shock_thr\":%.2f,",
       im.lin, im.vibRms, im.vibPeak, im.moving ? "true" : "false",
       im.shocks, im.shockG, ageMs(im.shockMs), g_imuCfg.shockMg / 1000.0f);
    jf("\"calibrated\":%s,\"calibrating\":%s,\"cal_age\":%d,"
       "\"samples\":%u,\"errors\":%u,\"age_ms\":%d",
       im.calibrated ? "true" : "false", im.calibrating ? "true" : "false",
       ageMs(im.calMs), im.samples, im.errors, ageMs(im.updatedMs));
}

static void hImu()
{
    if (!guard()) return;
    jreset(); jf("{"); imuJson(); jf("}");
    sendJson();
}

// Mede de novo o bias do giroscopio. Assincrono: a task espera uma janela de
// 2 s com o sensor parado; acompanhe por "calibrating" em /api/imu.
static void hImuCal()
{
    if (!guard()) return;
    if (!imuGet().present) {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"sensor ausente\"}");
        return;
    }
    imuCalibrate();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "{\"ok\":true,\"calibrating\":true}");
}

static void hState()
{
    if (!guard()) return;
    ScreenSnap scr; progGetScreen(&scr);
    ProgStats  st = progGetStats();
    SnmpStats  ss = snmpGetStats();
    uint8_t ka, kb; progGetKeyState(&ka, &kb);

    char l1e[100], l2e[100], hex[70];
    screenJson(l1e, l2e, hex, &scr);

    jreset();
    jf("{\"fw\":\"%s\",\"up\":%u,\"heap\":%u,", ESPPROG_VERSION, millis(), ESP.getFreeHeap());

    jf("\"eth\":{\"link\":%s,\"dhcp\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\","
       "\"dns\":\"%s\",\"mac\":\"%s\",\"speed\":%u,\"fd\":%s},",
       netLinkUp() ? "true" : "false", g_cfg.dhcp ? "true" : "false",
       netLocalIp().toString().c_str(), netSubnet().toString().c_str(),
       netGateway().toString().c_str(), netDns().toString().c_str(),
       netMac().c_str(), netLinkUp() ? netLinkSpeed() : 0,
       netFullDuplex() ? "true" : "false");

    jf("\"prog\":{\"link\":%s,\"tx\":%s,\"polls\":%u,\"frames\":%u,\"screens\":%u,"
       "\"beeps\":%u,\"lights\":%u,\"unknown\":%u,\"bad\":%u,\"badchk\":%u,\"resp\":%u,"
       "\"period_ms\":%u,\"age_ms\":%d,\"keya\":\"%02X\",\"keyb\":\"%02X\","
       "\"scanning\":%s},",
       progLinkUp() ? "true" : "false", g_cfg.txEnabled ? "true" : "false",
       st.polls, st.frames, st.screens, st.beeps, st.lights, st.unknownCmd,
       st.badFrames, st.badChk, st.responses, st.pollPeriodUs / 1000,
       ageMs(st.lastFrameMs), ka, kb, scanRunning() ? "true" : "false");

    jf("\"screen\":{\"l1\":\"%s\",\"l2\":\"%s\",\"raw\":\"%s\",\"id\":%u,\"seq\":%u,"
       "\"age_ms\":%d},",
       l1e, l2e, hex, scr.id, scr.seq, ageMs(scr.updatedMs));

    jf("\"val\":{"); valuesJson(); jf("},");

    jf("\"imu\":{"); imuJson(); jf("},");

    jf("\"snmp\":{\"en\":%s,\"port\":%u,\"rx\":%u,\"tx\":%u,\"bad\":%u,"
       "\"auth_fail\":%u,\"sets\":%u}}",
       g_cfg.snmpEnabled ? "true" : "false", g_cfg.snmpPort,
       ss.rxPackets, ss.txPackets, ss.badPackets, ss.authFailures, ss.sets);

    sendJson();
}

// --- teclas ----------------------------------------------------------------
static void hKey()
{
    if (!guard()) return;
    String k      = server.hasArg("k")      ? server.arg("k")      : "";
    String action = server.hasArg("action") ? server.arg("action") : "tap";

    ProgKey a = PK_NONE, b = PK_NONE;
    if (k.length()) {
        int comma = k.indexOf(',');
        if (comma < 0) {
            a = progKeyFromName(k.c_str());
        } else {
            String s1 = k.substring(0, comma), s2 = k.substring(comma + 1);
            s1.trim(); s2.trim();
            a = progKeyFromName(s1.c_str());
            b = progKeyFromName(s2.c_str());
        }
    }

    if (action == "up") {
        progKeyRelease();
    } else if (action == "down") {
        if (a == PK_NONE && b == PK_NONE) {
            server.send(400, "application/json",
                        "{\"ok\":false,\"error\":\"tecla invalida\"}");
            return;
        }
        progKeyHold(a, b, true);
    } else {
        if (a == PK_NONE && b == PK_NONE) {
            server.send(400, "application/json",
                        "{\"ok\":false,\"error\":\"tecla invalida\"}");
            return;
        }
        progKeyTap(a, b, true);
    }

    uint8_t ka, kb; progGetKeyState(&ka, &kb);
    jreset();
    jf("{\"ok\":true,\"action\":\"%s\",\"a\":\"%s\",\"b\":\"%s\","
       "\"bytes\":\"%02X %02X\"}",
       action.c_str(), progKeyName(a), progKeyName(b), ka, kb);
    sendJson();
}

// --- configuracao ----------------------------------------------------------
static void hGetConfig()
{
    if (!guard()) return;
    char c1[130], c2[130], c3[130], c4[70], c5[70], c6[70];
    jesc(g_cfg.sysContact,  c1, sizeof(c1));
    jesc(g_cfg.sysLocation, c2, sizeof(c2));
    jesc(g_cfg.scanSeq,     c3, sizeof(c3));
    jesc(g_cfg.hostname,    c4, sizeof(c4));
    jesc(g_cfg.roCommunity, c5, sizeof(c5));
    jesc(g_cfg.rwCommunity, c6, sizeof(c6));

    jreset();
    jf("{\"entRoot\":\"1.3.6.1.4.1.%u\",", (unsigned)ESPPROG_ENTERPRISE);
    jf("\"dhcp\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\",\"dns1\":\"%s\","
       "\"dns2\":\"%s\",\"hostname\":\"%s\",",
       g_cfg.dhcp ? "true" : "false",
       IPAddress(g_cfg.ip).toString().c_str(), IPAddress(g_cfg.mask).toString().c_str(),
       IPAddress(g_cfg.gw).toString().c_str(), IPAddress(g_cfg.dns1).toString().c_str(),
       IPAddress(g_cfg.dns2).toString().c_str(), c4);
    jf("\"snmpEnabled\":%s,\"snmpPort\":%u,\"roCommunity\":\"%s\",\"rwCommunity\":\"%s\","
       "\"sysContact\":\"%s\",\"sysLocation\":\"%s\",",
       g_cfg.snmpEnabled ? "true" : "false", g_cfg.snmpPort, c5, c6, c1, c2);
    jf("\"txEnabled\":%s,\"turnaroundUs\":%u,\"tailGapUs\":%u,\"keyHoldPolls\":%u,"
       "\"beepUnitMs\":%u,\"buzzer\":%s,\"led\":%s,",
       g_cfg.txEnabled ? "true" : "false", g_cfg.turnaroundUs, g_cfg.tailGapUs,
       g_cfg.keyHoldPolls, g_cfg.beepUnitMs,
       g_cfg.buzzer ? "true" : "false", g_cfg.led ? "true" : "false");
    jf("\"scanEnabled\":%s,\"scanIntervalSec\":%u,\"scanIdleSec\":%u,\"scanSeq\":\"%s\",",
       g_cfg.scanEnabled ? "true" : "false", g_cfg.scanIntervalSec, g_cfg.scanIdleSec, c3);
    jf("\"imuAccelG\":%u,\"imuGyroDps\":%u,\"imuShockMg\":%u,",
       g_imuCfg.accelG, g_imuCfg.gyroDps, g_imuCfg.shockMg);
    jf("\"authEnabled\":%s,\"authUser\":\"%s\",\"authPass\":\"\"}",
       g_cfg.authEnabled ? "true" : "false", g_cfg.authUser);
    sendJson();
}

static bool argBool(const char *n, bool cur)
{
    if (!server.hasArg(n)) return cur;
    String v = server.arg(n);
    return !(v == "0" || v == "false" || v == "off" || v == "");
}
static uint32_t argU(const char *n, uint32_t cur, uint32_t lo, uint32_t hi)
{
    if (!server.hasArg(n)) return cur;
    long v = server.arg(n).toInt();
    if (v < (long)lo) v = lo;
    if (v > (long)hi) v = hi;
    return (uint32_t)v;
}
static void argStr(const char *n, char *dst, size_t cap)
{
    if (!server.hasArg(n)) return;
    strlcpy(dst, server.arg(n).c_str(), cap);
}
static uint32_t argIp(const char *n, uint32_t cur)
{
    if (!server.hasArg(n)) return cur;
    IPAddress a;
    if (!a.fromString(server.arg(n))) return cur;
    return (uint32_t)a;
}

static void hPostConfig()
{
    if (!guard()) return;

    g_cfg.dhcp = argBool("dhcp", g_cfg.dhcp);
    g_cfg.ip   = argIp("ip",   g_cfg.ip);
    g_cfg.mask = argIp("mask", g_cfg.mask);
    g_cfg.gw   = argIp("gw",   g_cfg.gw);
    g_cfg.dns1 = argIp("dns1", g_cfg.dns1);
    g_cfg.dns2 = argIp("dns2", g_cfg.dns2);
    argStr("hostname", g_cfg.hostname, sizeof(g_cfg.hostname));
    if (!g_cfg.hostname[0]) strlcpy(g_cfg.hostname, DEF_HOSTNAME, sizeof(g_cfg.hostname));

    bool snmpTouched = server.hasArg("snmpEnabled") || server.hasArg("snmpPort") ||
                       server.hasArg("roCommunity") || server.hasArg("rwCommunity");
    g_cfg.snmpEnabled = argBool("snmpEnabled", g_cfg.snmpEnabled);
    g_cfg.snmpPort    = (uint16_t)argU("snmpPort", g_cfg.snmpPort, 1, 65535);
    argStr("roCommunity", g_cfg.roCommunity, sizeof(g_cfg.roCommunity));
    argStr("rwCommunity", g_cfg.rwCommunity, sizeof(g_cfg.rwCommunity));
    argStr("sysContact",  g_cfg.sysContact,  sizeof(g_cfg.sysContact));
    argStr("sysLocation", g_cfg.sysLocation, sizeof(g_cfg.sysLocation));

    const bool txAntes = g_cfg.txEnabled;
    g_cfg.txEnabled    = argBool("txEnabled", g_cfg.txEnabled);
    g_cfg.turnaroundUs = (uint16_t)argU("turnaroundUs", g_cfg.turnaroundUs, 500, 20000);
    g_cfg.tailGapUs    = (uint16_t)argU("tailGapUs",    g_cfg.tailGapUs,    0,   20000);
    g_cfg.keyHoldPolls = (uint8_t) argU("keyHoldPolls", g_cfg.keyHoldPolls, 1,   50);
    g_cfg.beepUnitMs   = (uint16_t)argU("beepUnitMs",   g_cfg.beepUnitMs,   1,   100);
    g_cfg.buzzer       = argBool("buzzer", g_cfg.buzzer);
    g_cfg.led          = argBool("led",    g_cfg.led);

    g_cfg.scanEnabled     = argBool("scanEnabled", g_cfg.scanEnabled);
    g_cfg.scanIntervalSec = (uint16_t)argU("scanIntervalSec", g_cfg.scanIntervalSec, 5, 3600);
    g_cfg.scanIdleSec     = (uint16_t)argU("scanIdleSec",     g_cfg.scanIdleSec,     1, 3600);
    argStr("scanSeq", g_cfg.scanSeq, sizeof(g_cfg.scanSeq));

    g_cfg.authEnabled = argBool("authEnabled", g_cfg.authEnabled);
    argStr("authUser", g_cfg.authUser, sizeof(g_cfg.authUser));
    if (server.hasArg("authPass") && server.arg("authPass").length())
        strlcpy(g_cfg.authPass, server.arg("authPass").c_str(), sizeof(g_cfg.authPass));
    if (!g_cfg.authUser[0]) strlcpy(g_cfg.authUser, "admin", sizeof(g_cfg.authUser));

    // faixas fora do que o chip aceita sao ignoradas, como um campo omitido
    const ImuSettings imuAntes = g_imuCfg;
    uint32_t ag = argU("imuAccelG",  g_imuCfg.accelG,  0, 65535);
    if (ag == 2 || ag == 4 || ag == 8 || ag == 16) g_imuCfg.accelG = (uint8_t)ag;
    uint32_t gd = argU("imuGyroDps", g_imuCfg.gyroDps, 0, 65535);
    if (gd == 250 || gd == 500 || gd == 1000 || gd == 2000) g_imuCfg.gyroDps = (uint16_t)gd;
    g_imuCfg.shockMg = (uint16_t)argU("imuShockMg", g_imuCfg.shockMg, 100, 16000);

    settingsSave();
    if (txAntes != g_cfg.txEnabled) progApplyTxMode();
    if (snmpTouched) snmpRestart();
    // o limiar de pancada e lido a cada amostra; so a faixa exige reprogramar o chip
    if (imuAntes.accelG != g_imuCfg.accelG || imuAntes.gyroDps != g_imuCfg.gyroDps)
        imuApplyConfig();

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void hReboot()
{
    if (!guard()) return;
    server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
    s_rebootAt = millis() + 400;
}

static void hFactory()
{
    if (!guard()) return;
    settingsFactoryDefaults();
    settingsSave();
    server.send(200, "application/json", "{\"ok\":true,\"factory\":true}");
    s_rebootAt = millis() + 400;
}

static void hScan()
{
    if (!guard()) return;
    scanTrigger();
    server.send(200, "application/json", "{\"ok\":true}");
}

// Diagnostico eletrico da linha de RX do barramento PROG. Responde a pergunta
// que os contadores nao respondem: a linha esta parada, ou tem atividade que a
// UART nao consegue decodificar?
static void hRxDiag()
{
    if (!guard()) return;
    uint32_t ms = (uint32_t)argU("ms", 200, 10, 1000);
    // pull=3 (pull-down interno) e o teste que importa: se a linha continuar
    // alta contra ele, existe pull-up EXTERNO -- ou seja, o VCC_PROG chega ao R4.
    int pull = (int)argU("pull", 0, 0, 3);
    RxDiag d = progDiagRx(ms, pull);

    const char *veredito;
    if (d.transitions == 0)
        veredito = d.high ? "LINHA PARADA EM NIVEL ALTO (idle normal, mas sem dados)"
                          : "LINHA PRESA EM NIVEL BAIXO (curto, ou conversor invertido)";
    else
        veredito = "HA ATIVIDADE NA LINHA";

    uint32_t baud = d.minPulseUs ? (1000000UL / d.minPulseUs) : 0;

    jreset();
    const char *pullTxt = pull == 1 ? "sem pull" : pull == 2 ? "pull-up interno"
                        : pull == 3 ? "pull-down interno" : "padrao (pull-up da UART)";
    jf("{\"pull\":\"%s\",\"janela_us\":%u,\"amostras\":%u,\"alto_pct\":%u,"
       "\"transicoes\":%u,\"menor_pulso_us\":%u,\"maior_pulso_us\":%u,"
       "\"baud_estimado\":%u,\"veredito\":\"%s\"}",
       pullTxt, d.windowUs, d.samples, d.samples ? (d.high * 100 / d.samples) : 0,
       d.transitions, d.minPulseUs, d.maxPulseUs, baud, veredito);
    sendJson();
}

// Devolve os tempos entre bordas da linha de RX, em microssegundos. Serve para
// medir o baud real e decodificar os bytes manualmente.
static void hEdges()
{
    if (!guard()) return;
    uint32_t to = (uint32_t)argU("timeout", 3000, 100, 10000);
    static EdgeCap cap;
    progCaptureEdges(&cap, to);

    jreset();
    jf("{\"esperou_us\":%u,\"nivel_inicial\":%u,\"bordas\":%u,\"dt_us\":[",
       cap.waitUs, cap.firstLevel, cap.n);
    for (uint16_t i = 0; i < cap.n; i++)
        jf("%s%u", i ? "," : "", cap.dt[i]);
    jf("]}");
    sendJson();
}

// Envia bytes crus na linha do PROG: /api/txraw?hex=7E080100...
// Serve para testar se a placa responde a alguem que fale primeiro.
static void hTxRaw()
{
    if (!guard()) return;
    String hex = server.hasArg("hex") ? server.arg("hex") : "";
    hex.replace(" ", "");
    if (hex.length() < 2 || (hex.length() % 2)) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"hex invalido\"}");
        return;
    }
    static uint8_t buf[64];
    size_t n = 0;
    for (size_t i = 0; i + 1 < (size_t)hex.length() && n < sizeof(buf); i += 2) {
        char t[3] = { hex[i], hex[i + 1], 0 };
        buf[n++] = (uint8_t)strtoul(t, nullptr, 16);
    }
    size_t sent = progTxRaw(buf, n);

    jreset();
    jf("{\"ok\":true,\"enviados\":%u,\"hex\":\"", (unsigned)sent);
    for (size_t i = 0; i < n; i++) jf("%02X", buf[i]);
    jf("\"}");
    sendJson();
}

// Ultimos quadros recebidos, em hexadecimal, com o intervalo entre eles.
static void hFrames()
{
    if (!guard()) return;
    static FrameLogEntry log[FRAME_LOG_N];
    uint8_t n = progGetFrameLog(log, FRAME_LOG_N);

    jreset();
    jf("{\"n\":%u,\"quadros\":[", n);
    for (uint8_t i = 0; i < n; i++) {
        jf("%s{\"idade_ms\":%d,\"delta_ms\":%d,\"cmd\":\"%02X\",\"hex\":\"",
           i ? "," : "", (int)(millis() - log[i].ms),
           i ? (int)(log[i].ms - log[i - 1].ms) : 0,
           log[i].len > 3 ? log[i].data[3] : 0);
        for (uint8_t k = 0; k < log[i].len; k++) jf("%02X", log[i].data[k]);
        jf("\"}");
    }
    jf("]}");
    sendJson();
}

// Segura o TX em nivel baixo para medicao: /api/txlow?ms=8000
static void hTxLow()
{
    if (!guard()) return;
    uint32_t ms = (uint32_t)argU("ms", 8000, 500, 20000);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json",
                "{\"ok\":true,\"aviso\":\"IO17 em nivel baixo; meca o D3 agora\"}");
    progTxHoldLow(ms);
}

// Envia bytes de tecla crus: /api/keyraw?a=A8&b=B4&hold=400
// Serve para descobrir o valor de "+ e - simultaneos", que nao esta documentado.
static void hKeyRaw()
{
    if (!guard()) return;
    if (!server.hasArg("a")) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"informe a=XX (hex)\"}");
        return;
    }
    uint8_t a = (uint8_t)strtoul(server.arg("a").c_str(), nullptr, 16);
    uint8_t b = server.hasArg("b")
              ? (uint8_t)strtoul(server.arg("b").c_str(), nullptr, 16) : KEY_IDLE;
    uint32_t hold = (uint32_t)argU("hold", 0, 0, 5000);

    progKeyRawTap(a, b);
    if (hold) delay(hold);              // mantem por mais tempo, se pedido

    jreset();
    jf("{\"ok\":true,\"a\":\"%02X\",\"b\":\"%02X\",\"hold_ms\":%u}", a, b, hold);
    sendJson();
}

static void hNotFound()
{
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"nao encontrado\"}");
}

// ---------------------------------------------------------------------------
void webBegin()
{
    server.on("/",                HTTP_GET,  hIndex);
    server.on("/api/state",       HTTP_GET,  hState);
    server.on("/api/screen",      HTTP_GET,  hScreen);
    server.on("/api/screen.txt",  HTTP_GET,  hScreenTxt);
    server.on("/api/values",      HTTP_GET,  hValues);
    server.on("/api/imu",         HTTP_GET,  hImu);
    server.on("/api/imu/calibrate", HTTP_POST, hImuCal);
    server.on("/api/key",         HTTP_GET,  hKey);
    server.on("/api/key",         HTTP_POST, hKey);
    server.on("/api/config",      HTTP_GET,  hGetConfig);
    server.on("/api/config",      HTTP_POST, hPostConfig);
    server.on("/api/reboot",      HTTP_POST, hReboot);
    server.on("/api/factory",     HTTP_POST, hFactory);
    server.on("/api/scan",        HTTP_POST, hScan);
    server.on("/api/rxdiag",      HTTP_GET,  hRxDiag);
    server.on("/api/edges",       HTTP_GET,  hEdges);
    server.on("/api/txraw",       HTTP_GET,  hTxRaw);
    server.on("/api/frames",      HTTP_GET,  hFrames);
    server.on("/api/txlow",       HTTP_GET,  hTxLow);
    server.on("/api/keyraw",      HTTP_GET,  hKeyRaw);
    server.onNotFound(hNotFound);
    server.begin();
    Serial.println("[web] http em :80");
}

void webLoop()
{
    server.handleClient();
    if (s_rebootAt && (int32_t)(millis() - s_rebootAt) > 0) {
        Serial.println("[sys] reiniciando...");
        delay(50);
        ESP.restart();
    }
}
