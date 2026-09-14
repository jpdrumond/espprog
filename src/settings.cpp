#include "settings.h"
#include "config.h"
#include <Preferences.h>

Settings g_cfg;
static Preferences prefs;
static const char *NS = "espprog";

void settingsFactoryDefaults()
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    // Padrao de fabrica: IP FIXO 192.168.1.21 (DHCP pode ser ligado na aba Rede).
    g_cfg.dhcp = false;
    g_cfg.ip   = IPAddress(192, 168, 1, 21);
    g_cfg.mask = IPAddress(255, 255, 255, 0);
    g_cfg.gw   = IPAddress(192, 168, 1, 1);
    g_cfg.dns1 = IPAddress(192, 168, 1, 1);
    g_cfg.dns2 = IPAddress(8, 8, 8, 8);
    strlcpy(g_cfg.hostname, DEF_HOSTNAME, sizeof(g_cfg.hostname));

    g_cfg.snmpEnabled = true;
    g_cfg.snmpPort    = DEF_SNMP_PORT;
    strlcpy(g_cfg.roCommunity, DEF_SNMP_RO, sizeof(g_cfg.roCommunity));
    strlcpy(g_cfg.rwCommunity, DEF_SNMP_RW, sizeof(g_cfg.rwCommunity));
    strlcpy(g_cfg.sysContact,  "",          sizeof(g_cfg.sysContact));
    strlcpy(g_cfg.sysLocation, "",          sizeof(g_cfg.sysLocation));

    g_cfg.txEnabled    = true;
    g_cfg.turnaroundUs = DEF_TURNAROUND_US;
    g_cfg.tailGapUs    = DEF_TAILGAP_US;
    g_cfg.keyHoldPolls = DEF_KEY_HOLD_POLLS;
    g_cfg.beepUnitMs   = 10;
    g_cfg.buzzer       = true;
    g_cfg.led          = true;
    g_cfg.ethCombo     = 0;

    g_cfg.scanEnabled     = false;         // desligado por seguranca (ver README)
    g_cfg.scanIntervalSec = 60;
    g_cfg.scanIdleSec     = 30;
    strlcpy(g_cfg.scanSeq, "+,W1500,-,W1500", sizeof(g_cfg.scanSeq));

    g_cfg.authEnabled = false;
    strlcpy(g_cfg.authUser, "admin", sizeof(g_cfg.authUser));
    strlcpy(g_cfg.authPass, "admin", sizeof(g_cfg.authPass));
}

void settingsLoad()
{
    settingsFactoryDefaults();
    if (!prefs.begin(NS, true)) {
        // Primeiro boot: cria o namespace agora para nao repetir o erro
        // "nvs_open failed: NOT_FOUND" em todo boot ate a primeira gravacao.
        settingsSave();
        return;
    }
    size_t n = prefs.getBytesLength("blob");
    if (n == sizeof(Settings)) {
        Settings tmp;
        prefs.getBytes("blob", &tmp, sizeof(tmp));
        g_cfg = tmp;
        // saneamento minimo
        if (g_cfg.snmpPort == 0)      g_cfg.snmpPort = DEF_SNMP_PORT;
        if (g_cfg.turnaroundUs < 500) g_cfg.turnaroundUs = DEF_TURNAROUND_US;
        if (g_cfg.keyHoldPolls == 0)  g_cfg.keyHoldPolls = DEF_KEY_HOLD_POLLS;
        if (g_cfg.beepUnitMs == 0)    g_cfg.beepUnitMs = 10;
        g_cfg.hostname[sizeof(g_cfg.hostname) - 1]       = 0;
        g_cfg.roCommunity[sizeof(g_cfg.roCommunity) - 1] = 0;
        g_cfg.rwCommunity[sizeof(g_cfg.rwCommunity) - 1] = 0;
        g_cfg.scanSeq[sizeof(g_cfg.scanSeq) - 1]         = 0;
    }
    prefs.end();
}

void settingsSave()
{
    if (!prefs.begin(NS, false)) return;
    prefs.putBytes("blob", &g_cfg, sizeof(g_cfg));
    prefs.end();
}
