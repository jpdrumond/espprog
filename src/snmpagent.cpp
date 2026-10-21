// ---------------------------------------------------------------------------
//  Agente SNMP v1 / v2c minimo, sem dependencias externas.
//  Suporta GetRequest, GetNextRequest, GetBulkRequest e SetRequest (apenas
//  espprogControlPressKey, e so quando existe community de escrita).
// ---------------------------------------------------------------------------
#include "snmpagent.h"
#include "settings.h"
#include "proglink.h"
#include "netmgr.h"
#include "imu.h"
#include <WiFiUdp.h>
#include <math.h>

#ifndef ESPPROG_ENTERPRISE
#define ESPPROG_ENTERPRISE 63050
#endif
#define ENT ESPPROG_ENTERPRISE

// --- tags BER --------------------------------------------------------------
#define T_INT        0x02
#define T_STR        0x04
#define T_NULL       0x05
#define T_OID        0x06
#define T_SEQ        0x30
#define T_IPADDR     0x40
#define T_COUNTER32  0x41
#define T_GAUGE32    0x42
#define T_TIMETICKS  0x43
#define T_NOSUCHOBJ  0x80
#define T_NOSUCHINST 0x81
#define T_ENDOFMIB   0x82

#define PDU_GET      0xA0
#define PDU_GETNEXT  0xA1
#define PDU_RESPONSE 0xA2
#define PDU_SET      0xA3
#define PDU_GETBULK  0xA5

// --- error-status ----------------------------------------------------------
#define ERR_NONE        0
#define ERR_TOOBIG      1
#define ERR_NOSUCHNAME  2
#define ERR_BADVALUE    3
#define ERR_GENERR      5
#define ERR_NOACCESS    6
#define ERR_WRONGTYPE   7
#define ERR_NOTWRITABLE 17

#define MAX_OID_LEN  24
#define MAX_VBS      20
#define BUF_SZ       1400

static WiFiUDP udp;
static SnmpStats g_ss;
static bool s_running = false;

// ---------------------------------------------------------------------------
//  Arvore de OIDs -- OBRIGATORIAMENTE em ordem lexicografica crescente
// ---------------------------------------------------------------------------
enum {
    // system group (RFC 1213)
    ID_sysDescr = 0, ID_sysObjectID, ID_sysUpTime, ID_sysContact,
    ID_sysName, ID_sysLocation, ID_sysServices,
    // espprog.1 - sistema
    ID_fwVersion, ID_uptimeSec, ID_macAddr, ID_ipAddr, ID_ethLink,
    ID_ethSpeed, ID_freeHeap,
    // espprog.2 - barramento PROG
    ID_progLink, ID_frameAge, ID_pollPeriod, ID_frames, ID_polls, ID_screens,
    ID_beeps, ID_badFrames, ID_badChk, ID_responses, ID_txEnabled,
    // espprog.3 - tela
    ID_line1, ID_line2, ID_rawHex, ID_screenId, ID_screenAge, ID_screenSeq,
    // espprog.4 - status (FEC/ABR/BOT/FOT/RF)
    ID_fec, ID_abr, ID_bot, ID_fot, ID_rf, ID_statusAge,
    // espprog.5 - contadores da placa
    ID_cycles, ID_cyclesAge, ID_rpm, ID_rpmAge, ID_encoder, ID_encoderAge,
    // espprog.6 - controle
    ID_pressKey,
    // espprog.7 - acelerometro/giroscopio (valores fracionarios x1000)
    ID_imuPresent, ID_imuModel, ID_accelX, ID_accelY, ID_accelZ,
    ID_gyroX, ID_gyroY, ID_gyroZ, ID_imuTemp, ID_pitch, ID_roll,
    ID_vibRms, ID_vibPeak, ID_moving, ID_shocks, ID_shockAge, ID_shockPeak,
    ID_imuAge, ID_imuSamples, ID_imuErrors, ID_imuCalibrated, ID_gateState,
};

struct OidEntry {
    const uint32_t *oid;
    uint8_t         len;
    uint8_t         id;
    bool            writable;
};

#define SYS(n)   {1,3,6,1,2,1,1,n,0}
#define EP(a,b)  {1,3,6,1,4,1,ENT,a,b,0}

static const uint32_t o_sysDescr[]    = SYS(1);
static const uint32_t o_sysObjectID[] = SYS(2);
static const uint32_t o_sysUpTime[]   = SYS(3);
static const uint32_t o_sysContact[]  = SYS(4);
static const uint32_t o_sysName[]     = SYS(5);
static const uint32_t o_sysLocation[] = SYS(6);
static const uint32_t o_sysServices[] = SYS(7);

static const uint32_t o_fwVersion[]  = EP(1,1);
static const uint32_t o_uptimeSec[]  = EP(1,2);
static const uint32_t o_macAddr[]    = EP(1,3);
static const uint32_t o_ipAddr[]     = EP(1,4);
static const uint32_t o_ethLink[]    = EP(1,5);
static const uint32_t o_ethSpeed[]   = EP(1,6);
static const uint32_t o_freeHeap[]   = EP(1,7);

static const uint32_t o_progLink[]   = EP(2,1);
static const uint32_t o_frameAge[]   = EP(2,2);
static const uint32_t o_pollPeriod[] = EP(2,3);
static const uint32_t o_frames[]     = EP(2,4);
static const uint32_t o_polls[]      = EP(2,5);
static const uint32_t o_screens[]    = EP(2,6);
static const uint32_t o_beeps[]      = EP(2,7);
static const uint32_t o_badFrames[]  = EP(2,8);
static const uint32_t o_badChk[]     = EP(2,9);
static const uint32_t o_responses[]  = EP(2,10);
static const uint32_t o_txEnabled[]  = EP(2,11);

static const uint32_t o_line1[]      = EP(3,1);
static const uint32_t o_line2[]      = EP(3,2);
static const uint32_t o_rawHex[]     = EP(3,3);
static const uint32_t o_screenId[]   = EP(3,4);
static const uint32_t o_screenAge[]  = EP(3,5);
static const uint32_t o_screenSeq[]  = EP(3,6);

static const uint32_t o_fec[]        = EP(4,1);
static const uint32_t o_abr[]        = EP(4,2);
static const uint32_t o_bot[]        = EP(4,3);
static const uint32_t o_fot[]        = EP(4,4);
static const uint32_t o_rf[]         = EP(4,5);
static const uint32_t o_statusAge[]  = EP(4,6);

static const uint32_t o_cycles[]     = EP(5,1);
static const uint32_t o_cyclesAge[]  = EP(5,2);
static const uint32_t o_rpm[]        = EP(5,3);
static const uint32_t o_rpmAge[]     = EP(5,4);
static const uint32_t o_encoder[]    = EP(5,5);
static const uint32_t o_encoderAge[] = EP(5,6);

static const uint32_t o_pressKey[]   = EP(6,1);

static const uint32_t o_imuPresent[] = EP(7,1);
static const uint32_t o_imuModel[]   = EP(7,2);
static const uint32_t o_accelX[]     = EP(7,3);
static const uint32_t o_accelY[]     = EP(7,4);
static const uint32_t o_accelZ[]     = EP(7,5);
static const uint32_t o_gyroX[]      = EP(7,6);
static const uint32_t o_gyroY[]      = EP(7,7);
static const uint32_t o_gyroZ[]      = EP(7,8);
static const uint32_t o_imuTemp[]    = EP(7,9);
static const uint32_t o_pitch[]      = EP(7,10);
static const uint32_t o_roll[]       = EP(7,11);
static const uint32_t o_vibRms[]     = EP(7,12);
static const uint32_t o_vibPeak[]    = EP(7,13);
static const uint32_t o_moving[]     = EP(7,14);
static const uint32_t o_shocks[]     = EP(7,15);
static const uint32_t o_shockAge[]   = EP(7,16);
static const uint32_t o_shockPeak[]  = EP(7,17);
static const uint32_t o_imuAge[]     = EP(7,18);
static const uint32_t o_imuSamples[] = EP(7,19);
static const uint32_t o_imuErrors[]  = EP(7,20);
static const uint32_t o_imuCal[]     = EP(7,21);
static const uint32_t o_gateState[]  = EP(7,22);

// raiz usada em sysObjectID
static const uint32_t o_entRoot[]    = {1,3,6,1,4,1,ENT};

#define E(sym, id, w) { sym, (uint8_t)(sizeof(sym)/sizeof(uint32_t)), id, w }

static const OidEntry kTable[] = {
    E(o_sysDescr,    ID_sysDescr,    false),
    E(o_sysObjectID, ID_sysObjectID, false),
    E(o_sysUpTime,   ID_sysUpTime,   false),
    E(o_sysContact,  ID_sysContact,  false),
    E(o_sysName,     ID_sysName,     false),
    E(o_sysLocation, ID_sysLocation, false),
    E(o_sysServices, ID_sysServices, false),

    E(o_fwVersion,   ID_fwVersion,   false),
    E(o_uptimeSec,   ID_uptimeSec,   false),
    E(o_macAddr,     ID_macAddr,     false),
    E(o_ipAddr,      ID_ipAddr,      false),
    E(o_ethLink,     ID_ethLink,     false),
    E(o_ethSpeed,    ID_ethSpeed,    false),
    E(o_freeHeap,    ID_freeHeap,    false),

    E(o_progLink,    ID_progLink,    false),
    E(o_frameAge,    ID_frameAge,    false),
    E(o_pollPeriod,  ID_pollPeriod,  false),
    E(o_frames,      ID_frames,      false),
    E(o_polls,       ID_polls,       false),
    E(o_screens,     ID_screens,     false),
    E(o_beeps,       ID_beeps,       false),
    E(o_badFrames,   ID_badFrames,   false),
    E(o_badChk,      ID_badChk,      false),
    E(o_responses,   ID_responses,   false),
    E(o_txEnabled,   ID_txEnabled,   false),

    E(o_line1,       ID_line1,       false),
    E(o_line2,       ID_line2,       false),
    E(o_rawHex,      ID_rawHex,      false),
    E(o_screenId,    ID_screenId,    false),
    E(o_screenAge,   ID_screenAge,   false),
    E(o_screenSeq,   ID_screenSeq,   false),

    E(o_fec,         ID_fec,         false),
    E(o_abr,         ID_abr,         false),
    E(o_bot,         ID_bot,         false),
    E(o_fot,         ID_fot,         false),
    E(o_rf,          ID_rf,          false),
    E(o_statusAge,   ID_statusAge,   false),

    E(o_cycles,      ID_cycles,      false),
    E(o_cyclesAge,   ID_cyclesAge,   false),
    E(o_rpm,         ID_rpm,         false),
    E(o_rpmAge,      ID_rpmAge,      false),
    E(o_encoder,     ID_encoder,     false),
    E(o_encoderAge,  ID_encoderAge,  false),

    E(o_pressKey,    ID_pressKey,    true),

    E(o_imuPresent,  ID_imuPresent,  false),
    E(o_imuModel,    ID_imuModel,    false),
    E(o_accelX,      ID_accelX,      false),
    E(o_accelY,      ID_accelY,      false),
    E(o_accelZ,      ID_accelZ,      false),
    E(o_gyroX,       ID_gyroX,       false),
    E(o_gyroY,       ID_gyroY,       false),
    E(o_gyroZ,       ID_gyroZ,       false),
    E(o_imuTemp,     ID_imuTemp,     false),
    E(o_pitch,       ID_pitch,       false),
    E(o_roll,        ID_roll,        false),
    E(o_vibRms,      ID_vibRms,      false),
    E(o_vibPeak,     ID_vibPeak,     false),
    E(o_moving,      ID_moving,      false),
    E(o_shocks,      ID_shocks,      false),
    E(o_shockAge,    ID_shockAge,    false),
    E(o_shockPeak,   ID_shockPeak,   false),
    E(o_imuAge,      ID_imuAge,      false),
    E(o_imuSamples,  ID_imuSamples,  false),
    E(o_imuErrors,   ID_imuErrors,   false),
    E(o_imuCal,      ID_imuCalibrated, false),
    E(o_gateState,   ID_gateState,   false),
};
static const int kTableLen = sizeof(kTable) / sizeof(kTable[0]);

// ---------------------------------------------------------------------------
//  Valores
// ---------------------------------------------------------------------------
struct SnmpVal {
    uint8_t         tag;
    int32_t         ival;
    uint32_t        uval;
    char            sval[100];
    size_t          slen;
    const uint32_t *oid;
    uint8_t         oidLen;
};

static uint32_t ageOf(uint32_t stampMs)
{
    if (!stampMs) return 0xFFFFFFFFu;          // nunca lido
    return millis() - stampMs;
}

static void setStr(SnmpVal &v, const char *s)
{
    v.tag = T_STR;
    v.slen = strlcpy(v.sval, s ? s : "", sizeof(v.sval));
    if (v.slen >= sizeof(v.sval)) v.slen = sizeof(v.sval) - 1;
}
static void setInt(SnmpVal &v, int32_t i)          { v.tag = T_INT;  v.ival = i; }
static void setU(SnmpVal &v, uint8_t tag, uint32_t u) { v.tag = tag; v.uval = u; }

// SNMP nao tem ponto flutuante: grandezas do sensor saem em milesimos
// (mg, milligraus/s, milligraus, milligraus Celsius).
static int32_t  milli(float f)  { return (int32_t)lroundf(f * 1000.0f); }
static uint32_t milliU(float f) { return f > 0 ? (uint32_t)lroundf(f * 1000.0f) : 0; }

static void getValue(uint8_t id, SnmpVal &v)
{
    memset(&v, 0, sizeof(v));
    ScreenSnap scr; progGetScreen(&scr);
    ProgValues pv = progGetValues();
    ProgStats  st = progGetStats();
    ImuData    im = {};
    if (id >= ID_imuPresent) im = imuGet();

    switch (id) {
    case ID_sysDescr: {
        char b[100];
        snprintf(b, sizeof(b), "espprog %s - emulador PROG TRIFLEX CONNECT (WT32-ETH01)",
                 ESPPROG_VERSION);
        setStr(v, b); break;
    }
    case ID_sysObjectID:
        v.tag = T_OID; v.oid = o_entRoot;
        v.oidLen = sizeof(o_entRoot) / sizeof(uint32_t); break;
    case ID_sysUpTime:   setU(v, T_TIMETICKS, millis() / 10); break;
    case ID_sysContact:  setStr(v, g_cfg.sysContact); break;
    case ID_sysName:     setStr(v, g_cfg.hostname); break;
    case ID_sysLocation: setStr(v, g_cfg.sysLocation); break;
    case ID_sysServices: setInt(v, 72); break;

    case ID_fwVersion:   setStr(v, ESPPROG_VERSION); break;
    case ID_uptimeSec:   setU(v, T_GAUGE32, millis() / 1000); break;
    case ID_macAddr:     setStr(v, netMac().c_str()); break;
    case ID_ipAddr:      v.tag = T_IPADDR; v.uval = (uint32_t)netLocalIp(); break;
    case ID_ethLink:     setInt(v, netLinkUp() ? 1 : 0); break;
    case ID_ethSpeed:    setU(v, T_GAUGE32, netLinkUp() ? netLinkSpeed() : 0); break;
    case ID_freeHeap:    setU(v, T_GAUGE32, ESP.getFreeHeap()); break;

    case ID_progLink:    setInt(v, progLinkUp() ? 1 : 0); break;
    case ID_frameAge:    setU(v, T_GAUGE32, ageOf(st.lastFrameMs)); break;
    case ID_pollPeriod:  setU(v, T_GAUGE32, st.pollPeriodUs / 1000); break;
    case ID_frames:      setU(v, T_COUNTER32, st.frames); break;
    case ID_polls:       setU(v, T_COUNTER32, st.polls); break;
    case ID_screens:     setU(v, T_COUNTER32, st.screens); break;
    case ID_beeps:       setU(v, T_COUNTER32, st.beeps); break;
    case ID_badFrames:   setU(v, T_COUNTER32, st.badFrames); break;
    case ID_badChk:      setU(v, T_COUNTER32, st.badChk); break;
    case ID_responses:   setU(v, T_COUNTER32, st.responses); break;
    case ID_txEnabled:   setInt(v, g_cfg.txEnabled ? 1 : 0); break;

    case ID_line1: { char b[80]; progLineToUtf8(scr.raw,      b, sizeof(b)); setStr(v, b); break; }
    case ID_line2: { char b[80]; progLineToUtf8(scr.raw + 16, b, sizeof(b)); setStr(v, b); break; }
    case ID_rawHex: { char b[70]; progRawToHex(scr.raw, 32, b); setStr(v, b); break; }
    case ID_screenId:    setInt(v, scr.id); break;
    case ID_screenAge:   setU(v, T_GAUGE32, ageOf(scr.updatedMs)); break;
    case ID_screenSeq:   setU(v, T_COUNTER32, scr.seq); break;

    case ID_fec:         setInt(v, pv.fec); break;
    case ID_abr:         setInt(v, pv.abr); break;
    case ID_bot:         setInt(v, pv.bot); break;
    case ID_fot:         setInt(v, pv.fot); break;
    case ID_rf:          setInt(v, pv.rf);  break;
    case ID_statusAge:   setU(v, T_GAUGE32, ageOf(pv.statusMs)); break;

    case ID_cycles:      setU(v, T_GAUGE32, pv.cyclesOk ? pv.cycles : 0); break;
    case ID_cyclesAge:   setU(v, T_GAUGE32, ageOf(pv.cyclesMs)); break;
    case ID_rpm:         setU(v, T_GAUGE32, pv.rpmOk ? pv.rpm : 0); break;
    case ID_rpmAge:      setU(v, T_GAUGE32, ageOf(pv.rpmMs)); break;
    case ID_encoder:     setInt(v, pv.encoderOk ? pv.encoder : 0); break;
    case ID_encoderAge:  setU(v, T_GAUGE32, ageOf(pv.encoderMs)); break;

    case ID_pressKey:    setInt(v, 0); break;

    case ID_imuPresent:  setInt(v, im.present ? 1 : 0); break;
    case ID_imuModel:    setStr(v, im.model); break;
    case ID_accelX:      setInt(v, milli(im.ax)); break;
    case ID_accelY:      setInt(v, milli(im.ay)); break;
    case ID_accelZ:      setInt(v, milli(im.az)); break;
    case ID_gyroX:       setInt(v, milli(im.gx)); break;
    case ID_gyroY:       setInt(v, milli(im.gy)); break;
    case ID_gyroZ:       setInt(v, milli(im.gz)); break;
    case ID_imuTemp:     setInt(v, milli(im.tempC)); break;
    case ID_pitch:       setInt(v, milli(im.pitch)); break;
    case ID_roll:        setInt(v, milli(im.roll)); break;
    case ID_vibRms:      setU(v, T_GAUGE32, milliU(im.vibRms)); break;
    case ID_vibPeak:     setU(v, T_GAUGE32, milliU(im.vibPeak)); break;
    case ID_moving:      setInt(v, im.moving ? 1 : 0); break;
    case ID_shocks:      setU(v, T_COUNTER32, im.shocks); break;
    case ID_shockAge:    setU(v, T_GAUGE32, ageOf(im.shockMs)); break;
    case ID_shockPeak:   setU(v, T_GAUGE32, milliU(im.shockG)); break;
    case ID_imuAge:      setU(v, T_GAUGE32, ageOf(im.updatedMs)); break;
    case ID_imuSamples:  setU(v, T_COUNTER32, im.samples); break;
    case ID_imuErrors:   setU(v, T_COUNTER32, im.errors); break;
    case ID_imuCalibrated: setInt(v, im.calibrated ? 1 : 0); break;
    case ID_gateState:   setInt(v, im.gate); break;   // 0 desconhecido, 1 fechada, 2 aberta
    default:             v.tag = T_NOSUCHOBJ; break;
    }
}

// ---------------------------------------------------------------------------
//  BER - leitura (para a frente)
// ---------------------------------------------------------------------------
struct BerR { const uint8_t *b; size_t len; size_t pos; };

static bool brTL(BerR &r, uint8_t *tag, size_t *len)
{
    if (r.pos + 2 > r.len) return false;
    *tag = r.b[r.pos++];
    uint8_t l = r.b[r.pos++];
    if (l < 0x80) { *len = l; }
    else {
        uint8_t n = l & 0x7F;
        if (n == 0 || n > 4 || r.pos + n > r.len) return false;
        size_t v = 0;
        while (n--) v = (v << 8) | r.b[r.pos++];
        *len = v;
    }
    return r.pos + *len <= r.len;
}

static bool brInt(BerR &r, int32_t *out)
{
    uint8_t tag; size_t len;
    if (!brTL(r, &tag, &len) || tag != T_INT || len == 0 || len > 5) return false;
    int32_t v = (r.b[r.pos] & 0x80) ? -1 : 0;
    for (size_t i = 0; i < len; i++) v = (v << 8) | r.b[r.pos + i];
    r.pos += len;
    *out = v;
    return true;
}

static bool brOid(BerR &r, uint32_t *oid, uint8_t *oidLen)
{
    uint8_t tag; size_t len;
    if (!brTL(r, &tag, &len) || tag != T_OID || len == 0) return false;
    size_t end = r.pos + len;
    uint8_t n = 0;
    uint32_t first = 0;
    while (r.pos < end && (r.b[r.pos] & 0x80)) { first = (first << 7) | (r.b[r.pos] & 0x7F); r.pos++; }
    if (r.pos >= end) return false;
    first = (first << 7) | r.b[r.pos++];
    if (first >= 80) { oid[0] = 2; oid[1] = first - 80; }
    else             { oid[0] = first / 40; oid[1] = first % 40; }
    n = 2;
    while (r.pos < end) {
        uint32_t v = 0;
        while (r.pos < end && (r.b[r.pos] & 0x80)) { v = (v << 7) | (r.b[r.pos] & 0x7F); r.pos++; }
        if (r.pos >= end) return false;
        v = (v << 7) | r.b[r.pos++];
        if (n >= MAX_OID_LEN) return false;
        oid[n++] = v;
    }
    *oidLen = n;
    return true;
}

// ---------------------------------------------------------------------------
//  BER - escrita (de tras para frente)
// ---------------------------------------------------------------------------
struct BerW { uint8_t *b; size_t pos; bool ok; };

static void bwRaw(BerW &w, const uint8_t *p, size_t n)
{
    if (!w.ok || w.pos < n) { w.ok = false; return; }
    w.pos -= n;
    memcpy(w.b + w.pos, p, n);
}
static void bwByte(BerW &w, uint8_t c)
{
    if (!w.ok || w.pos == 0) { w.ok = false; return; }
    w.b[--w.pos] = c;
}
static void bwLen(BerW &w, size_t len)
{
    if (len < 0x80) { bwByte(w, (uint8_t)len); return; }
    uint8_t t[4]; int n = 0; size_t v = len;
    while (v) { t[n++] = (uint8_t)(v & 0xFF); v >>= 8; }
    for (int i = 0; i < n; i++) bwByte(w, t[i]);   // de tras p/ frente: LSB primeiro
    bwByte(w, (uint8_t)(0x80 | n));
}
static void bwTL(BerW &w, uint8_t tag, size_t len) { bwLen(w, len); bwByte(w, tag); }

static int encSigned(int32_t v, uint8_t *out)
{
    uint8_t t[5]; int n = 0;
    for (;;) {
        t[n++] = (uint8_t)(v & 0xFF);
        v >>= 8;
        if ((v == 0 && !(t[n - 1] & 0x80)) || (v == -1 && (t[n - 1] & 0x80))) break;
        if (n >= 5) break;
    }
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    return n;
}

static int encUnsigned(uint32_t v, uint8_t *out)
{
    uint8_t t[5]; int n = 0;
    do { t[n++] = (uint8_t)(v & 0xFF); v >>= 8; } while (v);
    if (t[n - 1] & 0x80) t[n++] = 0;
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    return n;
}

static int encVarint(uint32_t v, uint8_t *out)
{
    uint8_t t[5]; int n = 0;
    do { t[n++] = (uint8_t)(v & 0x7F); v >>= 7; } while (v);
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i] | ((i < n - 1) ? 0x80 : 0);
    return n;
}

static int encOidBody(const uint32_t *oid, uint8_t len, uint8_t *out, int cap)
{
    if (len < 2) return -1;
    int p = 0;
    p += encVarint(oid[0] * 40 + oid[1], out + p);
    for (int i = 2; i < len; i++) {
        if (p + 5 > cap) return -1;
        p += encVarint(oid[i], out + p);
    }
    return p;
}

static void bwOid(BerW &w, const uint32_t *oid, uint8_t len)
{
    uint8_t tmp[MAX_OID_LEN * 5 + 4];
    int n = encOidBody(oid, len, tmp, sizeof(tmp));
    if (n < 0) { w.ok = false; return; }
    bwRaw(w, tmp, n);
    bwTL(w, T_OID, n);
}

static void bwValue(BerW &w, const SnmpVal &v)
{
    uint8_t tmp[8];
    switch (v.tag) {
    case T_INT: {
        int n = encSigned(v.ival, tmp);
        bwRaw(w, tmp, n); bwTL(w, T_INT, n); break;
    }
    case T_COUNTER32: case T_GAUGE32: case T_TIMETICKS: {
        int n = encUnsigned(v.uval, tmp);
        bwRaw(w, tmp, n); bwTL(w, v.tag, n); break;
    }
    case T_IPADDR: {
        uint8_t ip[4] = { (uint8_t)(v.uval), (uint8_t)(v.uval >> 8),
                          (uint8_t)(v.uval >> 16), (uint8_t)(v.uval >> 24) };
        bwRaw(w, ip, 4); bwTL(w, T_IPADDR, 4); break;
    }
    case T_STR:
        bwRaw(w, (const uint8_t *)v.sval, v.slen);
        bwTL(w, T_STR, v.slen);
        break;
    case T_OID:
        bwOid(w, v.oid, v.oidLen);
        break;
    case T_NOSUCHOBJ: case T_NOSUCHINST: case T_ENDOFMIB:
        bwTL(w, v.tag, 0);
        break;
    default:
        bwTL(w, T_NULL, 0);
        break;
    }
}

// ---------------------------------------------------------------------------
//  Busca na arvore
// ---------------------------------------------------------------------------
static int oidCmp(const uint32_t *a, uint8_t alen, const uint32_t *b, uint8_t blen)
{
    uint8_t n = alen < blen ? alen : blen;
    for (uint8_t i = 0; i < n; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return  1;
    }
    if (alen < blen) return -1;
    if (alen > blen) return  1;
    return 0;
}

static int findExact(const uint32_t *oid, uint8_t len)
{
    for (int i = 0; i < kTableLen; i++)
        if (oidCmp(oid, len, kTable[i].oid, kTable[i].len) == 0) return i;
    return -1;
}

static int findNext(const uint32_t *oid, uint8_t len)
{
    for (int i = 0; i < kTableLen; i++)
        if (oidCmp(kTable[i].oid, kTable[i].len, oid, len) > 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------
//  Varbinds da resposta
// ---------------------------------------------------------------------------
struct VB {
    uint32_t oid[MAX_OID_LEN];
    uint8_t  oidLen;
    SnmpVal  val;
};
static VB s_vb[MAX_VBS];

static void copyOid(uint32_t *dst, const uint32_t *src, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) dst[i] = src[i];
}

// ---------------------------------------------------------------------------
//  Processamento de um datagrama
// ---------------------------------------------------------------------------
static void handlePacket(const uint8_t *pkt, size_t plen, IPAddress from, uint16_t fport)
{
    BerR r = { pkt, plen, 0 };
    uint8_t tag; size_t len;

    if (!brTL(r, &tag, &len) || tag != T_SEQ) { g_ss.badPackets++; return; }

    int32_t version;
    if (!brInt(r, &version) || (version != 0 && version != 1)) { g_ss.badPackets++; return; }

    // community
    if (!brTL(r, &tag, &len) || tag != T_STR || len > 64) { g_ss.badPackets++; return; }
    char comm[65];
    memcpy(comm, pkt + r.pos, len); comm[len] = 0;
    r.pos += len;

    uint8_t pduTag;
    if (!brTL(r, &pduTag, &len)) { g_ss.badPackets++; return; }
    if (pduTag != PDU_GET && pduTag != PDU_GETNEXT &&
        pduTag != PDU_GETBULK && pduTag != PDU_SET) { g_ss.badPackets++; return; }
    if (pduTag == PDU_GETBULK && version == 0) { g_ss.badPackets++; return; }  // v1 nao tem GETBULK

    // community: leitura aceita RO ou RW; escrita exige RW nao vazia
    bool roOk = (strcmp(comm, g_cfg.roCommunity) == 0);
    bool rwOk = (g_cfg.rwCommunity[0] && strcmp(comm, g_cfg.rwCommunity) == 0);
    if (pduTag == PDU_SET ? !rwOk : !(roOk || rwOk)) { g_ss.authFailures++; return; }

    int32_t reqId, f1, f2;
    if (!brInt(r, &reqId) || !brInt(r, &f1) || !brInt(r, &f2)) { g_ss.badPackets++; return; }
    const int32_t nonRep = (pduTag == PDU_GETBULK) ? (f1 < 0 ? 0 : f1) : 0;
    const int32_t maxRep = (pduTag == PDU_GETBULK) ? (f2 < 0 ? 0 : f2) : 0;

    if (!brTL(r, &tag, &len) || tag != T_SEQ) { g_ss.badPackets++; return; }
    const size_t vbEnd = r.pos + len;

    // --- varbinds do pedido -------------------------------------------------
    // estaticos: snmpPoll() so e chamado pelo loop(), e isso poupa ~2 KB de pilha
    static uint32_t reqOid[MAX_VBS][MAX_OID_LEN];
    static uint8_t  reqLen[MAX_VBS];
    static int32_t  setVal[MAX_VBS];
    static uint8_t  setTag[MAX_VBS];
    int nReq = 0;

    while (r.pos < vbEnd && nReq < MAX_VBS) {
        if (!brTL(r, &tag, &len) || tag != T_SEQ) { g_ss.badPackets++; return; }
        size_t oneEnd = r.pos + len;
        if (!brOid(r, reqOid[nReq], &reqLen[nReq])) { g_ss.badPackets++; return; }
        setTag[nReq] = T_NULL;
        setVal[nReq] = 0;
        if (r.pos < oneEnd) {
            BerR vr = { pkt, oneEnd, r.pos };
            uint8_t vt; size_t vl;
            size_t save = vr.pos;
            if (brTL(vr, &vt, &vl)) {
                setTag[nReq] = vt;
                if (vt == T_INT) { vr.pos = save; brInt(vr, &setVal[nReq]); }
            }
        }
        r.pos = oneEnd;
        nReq++;
    }
    if (nReq == 0) { g_ss.badPackets++; return; }

    // --- monta as respostas -------------------------------------------------
    int32_t errStatus = ERR_NONE, errIndex = 0;
    int nOut = 0;

    if (pduTag == PDU_SET) {
        g_ss.sets++;
        for (int i = 0; i < nReq; i++) {
            int idx = findExact(reqOid[i], reqLen[i]);
            if (idx < 0 || !kTable[idx].writable) {
                errStatus = (idx < 0) ? ERR_NOSUCHNAME : ERR_NOTWRITABLE;
                errIndex  = i + 1;
                break;
            }
            if (setTag[i] != T_INT) { errStatus = ERR_WRONGTYPE; errIndex = i + 1; break; }
            if (kTable[idx].id == ID_pressKey) {
                if (setVal[i] < 0 || setVal[i] > 4) { errStatus = ERR_BADVALUE; errIndex = i + 1; break; }
                if (setVal[i] != 0) progKeyTap((ProgKey)setVal[i], PK_NONE, true);
            }
        }
        // ecoa os varbinds do pedido
        for (int i = 0; i < nReq; i++) {
            copyOid(s_vb[i].oid, reqOid[i], reqLen[i]);
            s_vb[i].oidLen = reqLen[i];
            int idx = findExact(reqOid[i], reqLen[i]);
            if (idx >= 0) { getValue(kTable[idx].id, s_vb[i].val);
                            if (kTable[idx].id == ID_pressKey) setInt(s_vb[i].val, setVal[i]); }
            else          { s_vb[i].val.tag = T_NOSUCHOBJ; }
        }
        nOut = nReq;

    } else if (pduTag == PDU_GET) {
        for (int i = 0; i < nReq; i++) {
            copyOid(s_vb[i].oid, reqOid[i], reqLen[i]);
            s_vb[i].oidLen = reqLen[i];
            int idx = findExact(reqOid[i], reqLen[i]);
            if (idx >= 0) getValue(kTable[idx].id, s_vb[i].val);
            else if (version == 0) {
                errStatus = ERR_NOSUCHNAME; errIndex = i + 1;
                memset(&s_vb[i].val, 0, sizeof(SnmpVal)); s_vb[i].val.tag = T_NULL;
            } else {
                memset(&s_vb[i].val, 0, sizeof(SnmpVal)); s_vb[i].val.tag = T_NOSUCHOBJ;
            }
        }
        nOut = nReq;

    } else {  // GETNEXT / GETBULK
        const int reps = (pduTag == PDU_GETBULK) ? maxRep : 1;
        // parte nao repetitiva
        int limit = (pduTag == PDU_GETBULK) ? (nonRep < nReq ? nonRep : nReq) : nReq;
        for (int i = 0; i < limit && nOut < MAX_VBS; i++) {
            int idx = findNext(reqOid[i], reqLen[i]);
            if (idx < 0) {
                copyOid(s_vb[nOut].oid, reqOid[i], reqLen[i]);
                s_vb[nOut].oidLen = reqLen[i];
                memset(&s_vb[nOut].val, 0, sizeof(SnmpVal));
                s_vb[nOut].val.tag = (version == 0) ? T_NULL : T_ENDOFMIB;
                if (version == 0 && errStatus == ERR_NONE) { errStatus = ERR_NOSUCHNAME; errIndex = i + 1; }
            } else {
                copyOid(s_vb[nOut].oid, kTable[idx].oid, kTable[idx].len);
                s_vb[nOut].oidLen = kTable[idx].len;
                getValue(kTable[idx].id, s_vb[nOut].val);
            }
            nOut++;
        }
        // parte repetitiva
        if (pduTag == PDU_GETBULK) {
            for (int i = nonRep; i < nReq && nOut < MAX_VBS; i++) {
                uint32_t cur[MAX_OID_LEN]; uint8_t curLen = reqLen[i];
                copyOid(cur, reqOid[i], curLen);
                for (int rp = 0; rp < reps && nOut < MAX_VBS; rp++) {
                    int idx = findNext(cur, curLen);
                    if (idx < 0) {
                        copyOid(s_vb[nOut].oid, cur, curLen);
                        s_vb[nOut].oidLen = curLen;
                        memset(&s_vb[nOut].val, 0, sizeof(SnmpVal));
                        s_vb[nOut].val.tag = T_ENDOFMIB;
                        nOut++;
                        break;
                    }
                    copyOid(s_vb[nOut].oid, kTable[idx].oid, kTable[idx].len);
                    s_vb[nOut].oidLen = kTable[idx].len;
                    getValue(kTable[idx].id, s_vb[nOut].val);
                    copyOid(cur, kTable[idx].oid, kTable[idx].len);
                    curLen = kTable[idx].len;
                    nOut++;
                }
            }
        }
    }

    // --- serializa (de tras para frente) ------------------------------------
    static uint8_t out[BUF_SZ];
    BerW w = { out, sizeof(out), true };

    size_t vbListEnd = w.pos;
    for (int i = nOut - 1; i >= 0; i--) {
        size_t oneEnd = w.pos;
        bwValue(w, s_vb[i].val);
        bwOid(w, s_vb[i].oid, s_vb[i].oidLen);
        bwTL(w, T_SEQ, oneEnd - w.pos);
    }
    if (!w.ok) { g_ss.badPackets++; return; }
    bwTL(w, T_SEQ, vbListEnd - w.pos);

    size_t pduEnd = w.pos;
    // A escrita e reversa, entao empilhamos na ordem inversa do quadro:
    // error-index, error-status, request-id -> sai request-id, error-status, error-index.
    { uint8_t t[5]; int n = encSigned(errIndex,  t); bwRaw(w, t, n); bwTL(w, T_INT, n); }
    { uint8_t t[5]; int n = encSigned(errStatus, t); bwRaw(w, t, n); bwTL(w, T_INT, n); }
    { uint8_t t[5]; int n = encSigned(reqId,     t); bwRaw(w, t, n); bwTL(w, T_INT, n); }
    bwTL(w, PDU_RESPONSE, pduEnd - w.pos);

    size_t msgEnd = w.pos;
    size_t cl = strlen(comm);
    bwRaw(w, (const uint8_t *)comm, cl);
    bwTL(w, T_STR, cl);
    { uint8_t t[5]; int n = encSigned(version, t); bwRaw(w, t, n); bwTL(w, T_INT, n); }
    bwTL(w, T_SEQ, msgEnd - w.pos);

    if (!w.ok) { g_ss.badPackets++; return; }

    udp.beginPacket(from, fport);
    udp.write(out + w.pos, sizeof(out) - w.pos);
    udp.endPacket();
    g_ss.txPackets++;
}

// ---------------------------------------------------------------------------
void snmpBegin()
{
    memset(&g_ss, 0, sizeof(g_ss));
    snmpRestart();
}

void snmpRestart()
{
    if (s_running) { udp.stop(); s_running = false; }
    if (!g_cfg.snmpEnabled) { Serial.println("[snmp] desabilitado"); return; }
    if (udp.begin(g_cfg.snmpPort)) {
        s_running = true;
        Serial.printf("[snmp] escutando em udp/%u (community RO \"%s\"%s)\n",
                      g_cfg.snmpPort, g_cfg.roCommunity,
                      g_cfg.rwCommunity[0] ? ", RW habilitada" : "");
    } else {
        Serial.printf("[snmp] FALHA ao abrir udp/%u\n", g_cfg.snmpPort);
    }
}

void snmpPoll()
{
    if (!s_running) return;
    int n = udp.parsePacket();
    if (n <= 0) return;
    static uint8_t in[BUF_SZ];
    if (n > (int)sizeof(in)) { udp.flush(); g_ss.badPackets++; return; }
    int got = udp.read(in, n);
    if (got <= 0) return;
    g_ss.rxPackets++;
    handlePacket(in, got, udp.remoteIP(), udp.remotePort());
}

SnmpStats snmpGetStats() { return g_ss; }
