#include "netmgr.h"
#include "settings.h"
#include <ETH.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_eth.h"
#include "esp_eth_mac.h"
// --- WT32-ETH01 / LAN8720 --------------------------------------------------
// Ajustaveis por build flag: revisoes e clones do WT32-ETH01 diferem no
// endereco do PHY (1 ou 0) e no pino de reset (IO16 ou nenhum). Ver README 3.2.
#ifndef ETH_ADDR
#define ETH_ADDR       1
#endif
#ifndef ETH_POWER_PIN
#define ETH_POWER_PIN  16     // passado ao driver como phy reset_gpio_num
#endif
#ifndef ETH_CLK_MODE_SEL
#define ETH_CLK_MODE_SEL ETH_CLOCK_GPIO0_IN
#endif
#ifndef ESPPROG_ETH_DIAG
#define ESPPROG_ETH_DIAG 0        // sondas de bancada: desligadas por padrao
#endif
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN   18

// Combinacoes de endereco de PHY e pino de reset que variam entre revisoes e
// clones do WT32-ETH01. O driver do Arduino passa o "power pin" como
// phy_config.reset_gpio_num, e -1 significa "nao tocar nesse pino".
// preMs: liga o IO16 e ESPERA antes de chamar o driver. A WT32-ETH01 amarra o
// nRST do LAN8720 e o enable do oscilador no mesmo GPIO16, entao o PHY sai do
// reset junto com a partida do cristal e a logica digital ainda nao esta pronta
// quando o driver consulta o MDIO -- que e o classico "power up timeout".
// Ligar o oscilador antes, esperar estabilizar, e entao inicializar com
// reset=nenhum evita o problema sem mexer no hardware.
// lowMs: segura o IO16 em LOW antes de subir. Isso da ao PHY um pulso de reset
//        de verdade (com descarga da capacitancia residual) em vez de partir de
//        um estado indefinido -- so subir o pino nao reseta nada.
// preMs: tempo em HIGH antes de entregar ao driver, para o cristal estabilizar.
struct EthCombo { int addr; int rst; uint16_t lowMs; uint16_t preMs; const char *desc; };
// A PRIMEIRA entrada e a configuracao padrao do WT32-ETH01, identica a da
// biblioteca de referencia: nada de pre-acionar pinos, nada de pulso de reset.
// Numa placa saudavel o PHY sobe aqui e nenhuma das demais chega a rodar.
// As seguintes existem so como recuperacao para placas problematicas.
static const EthCombo kCombos[] = {
    { ETH_ADDR, ETH_POWER_PIN, 0,   0,    "padrao WT32-ETH01 (addr=1, reset=IO16)"       },
    { ETH_ADDR, -1,            500, 250,  "addr=1 sem reset, IO16 500ms LOW + 250ms HIGH" },
    { ETH_ADDR, -1,            500, 1500, "addr=1 sem reset, IO16 500ms LOW + 1.5s HIGH"  },
    { 0,        ETH_POWER_PIN, 0,   0,    "addr=0, reset=IO16"                            },
    { 0,        -1,            500, 250,  "addr=0 sem reset, IO16 500ms LOW + 250ms HIGH" },
    { ETH_ADDR, -1,            0,   1000, "addr=1 sem reset, IO16 so HIGH 1s"             },
};
static const uint8_t N_COMBOS = sizeof(kCombos) / sizeof(kCombos[0]);

// RTC_NOINIT sobrevive ao soft reset (nao ao corte de energia), que e
// exatamente o escopo desejado para a varredura.
#define ETH_SWEEP_MAGIC 0x45544831
RTC_NOINIT_ATTR static uint32_t s_sweepMagic;
RTC_NOINIT_ATTR static uint8_t  s_comboIdx;

// O clock de 50 MHz do PHY entra pelo GPIO0 -- por isso o IO0 nao pode ser
// usado para mais nada alem do BOTAO DE BOOT.

static volatile bool s_linkUp = false;
static volatile bool s_hasIp  = false;
static bool s_mdnsUp = false;
static bool s_phyOk  = false;
static uint8_t s_phyAddr = ETH_ADDR;

static void onEvent(WiFiEvent_t event)
{
    switch (event) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    case ARDUINO_EVENT_ETH_START:
        ETH.setHostname(g_cfg.hostname);
        break;
#else
    case ARDUINO_EVENT_ETH_START:
        ETH.setHostname(g_cfg.hostname);
        break;
#endif
    case ARDUINO_EVENT_ETH_CONNECTED:
        s_linkUp = true;
        Serial.println("[eth] link up");
        break;

    case ARDUINO_EVENT_ETH_GOT_IP:
        s_hasIp = true;
        Serial.printf("[eth] ip=%s mask=%s gw=%s dns=%s (%s, %u Mbps, %s)\n",
                      ETH.localIP().toString().c_str(),
                      ETH.subnetMask().toString().c_str(),
                      ETH.gatewayIP().toString().c_str(),
                      ETH.dnsIP().toString().c_str(),
                      g_cfg.dhcp ? "DHCP" : "estatico",
                      ETH.linkSpeed(),
                      ETH.fullDuplex() ? "full-duplex" : "half-duplex");
        if (!s_mdnsUp && MDNS.begin(g_cfg.hostname)) {
            MDNS.addService("http", "tcp", 80);
            s_mdnsUp = true;
            Serial.printf("[mdns] http://%s.local\n", g_cfg.hostname);
        }
        break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
        s_linkUp = false;
        s_hasIp  = false;
        Serial.println("[eth] link down");
        break;

    case ARDUINO_EVENT_ETH_STOP:
        s_linkUp = false;
        s_hasIp  = false;
        break;

    default:
        break;
    }
}

// Sonda de hardware: o LAN8720 so responde no MDIO se o oscilador de 50 MHz
// estiver correndo, e esse clock entra pelo IO0. Amostrando o IO0 por software
// (~5 MHz, bem abaixo dos 50 MHz) o sinal alias, mas continua alternando -- e e
// justamente a ALTERNANCIA que interessa, nao a forma de onda. Linha estatica
// significa oscilador parado; mistura de 0 e 1 significa clock presente.
static uint32_t sampleIo0(uint8_t mode, uint32_t n)
{
    pinMode(0, mode);
    delayMicroseconds(200);
    uint32_t ones = 0;
    for (uint32_t i = 0; i < n; i++) ones += (uint32_t)digitalRead(0);
    return ones;
}

static void probeEthClock()
{
    Serial.println("[eth] --- sonda do clock de 50 MHz ---");

    pinMode(16, OUTPUT);          // enable do oscilador
    digitalWrite(16, HIGH);
    delay(500);                   // tempo de estabilizacao do cristal

    // Amostrar so uma vez nao distingue clock real de pino solto: um pino
    // flutuando tambem le ruido perto de 50%. Amostrando com pull-up e com
    // pull-down a diferenca aparece -- um sinal REALMENTE acionado ignora os
    // pull-ups internos (dezenas de kohm) e da o mesmo resultado nos dois.
    const uint32_t N = 20000;
    uint32_t up   = sampleIo0(INPUT_PULLUP,   N);
    uint32_t down = sampleIo0(INPUT_PULLDOWN, N);

    Serial.printf("[eth] IO0: pull-up %u/%u (%u%%), pull-down %u/%u (%u%%)\n",
                  up, N, up * 100 / N, down, N, down * 100 / N);

    bool upMid   = (up   > N / 10) && (up   < N * 9 / 10);
    bool downMid = (down > N / 10) && (down < N * 9 / 10);

    if (upMid && downMid)
        Serial.println("[eth]   -> CLOCK REAL: alterna mesmo contra pull-up e"
                       " pull-down. O oscilador esta funcionando.");
    else if (up > N * 9 / 10 && down < N / 10)
        Serial.println("[eth]   -> PINO SOLTO: segue o pull-up/pull-down. NAO ha"
                       " clock; o oscilador nao parte ou nao chega ao IO0.");
    else
        Serial.println("[eth]   -> resultado ambiguo; ver os percentuais acima.");
}

// --- Sonda dos pinos RMII de recepcao --------------------------------------
// Caminho totalmente independente do MDIO. RXD0/RXD1/CRS_DV sao SAIDAS do PHY
// para o ESP32. Se o LAN8720 estiver energizado e funcionando, ele dirige esses
// pinos mesmo sem link e mesmo com a gerencia quebrada -- o MDIO e so gerencia,
// o PHY auto-negocia sozinho pelos straps.
//
//   dirigido (mesma leitura com pull-up e pull-down) = o chip esta vivo
//   flutuante (segue o pull)                         = o chip nao dirige nada
//
// Se estes pinos estiverem flutuando, o PHY nao esta apenas "sem gerencia":
// ele nao esta operando de forma alguma.
static void probeRmiiPins()
{
    Serial.println("[eth] --- sonda dos pinos RMII (saidas do PHY) ---");
    const int  pinos[] = { 25, 26, 27 };
    const char *nomes[] = { "RXD0  (IO25)", "RXD1  (IO26)", "CRS_DV(IO27)" };
    const uint32_t N = 5000;

    int flutuando = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t up = 0, dn = 0;
        pinMode(pinos[i], INPUT_PULLUP);
        delayMicroseconds(500);
        for (uint32_t k = 0; k < N; k++) up += (uint32_t)digitalRead(pinos[i]);
        pinMode(pinos[i], INPUT_PULLDOWN);
        delayMicroseconds(500);
        for (uint32_t k = 0; k < N; k++) dn += (uint32_t)digitalRead(pinos[i]);

        const char *v;
        if (up > N * 9 / 10 && dn < N / 10)       { v = "FLUTUANDO - o PHY nao dirige"; flutuando++; }
        else if (up < N / 10 && dn < N / 10)        v = "DIRIGIDO em 0 - PHY VIVO";
        else if (up > N * 9 / 10 && dn > N * 9 / 10) v = "DIRIGIDO em 1 - PHY VIVO";
        else                                        v = "ALTERNANDO - ha atividade!";

        Serial.printf("[eth]   %s: pull-up %u/%u, pull-down %u/%u -> %s\n",
                      nomes[i], up, N, dn, N, v);
    }

    if (flutuando == 3)
        Serial.println("[eth]   -> os 3 pinos flutuam: o LAN8720 nao esta operando"
                       " (sem alimentacao interna ou danificado).");
    else
        Serial.println("[eth]   -> o PHY DIRIGE o barramento RMII: esta energizado e"
                       " operando. O defeito e so na gerencia (MDIO).");
}

// --- Caca ao pino de reset do PHY ------------------------------------------
// Os pinos RMII dirigidos em 0 + MDIO mudo + sem auto-negociacao descrevem um
// PHY ALIMENTADO E PRESO EM RESET. Acionamos o IO16 de todas as formas sem
// efeito -- entao talvez o nRST desta revisao esteja em outro pino. Aqui cada
// candidato e levado a nivel alto e o MDIO e reconsultado.
// Declarada depois do bit-bang; ver chamada em netBegin().

// --- MDIO na unha (IEEE 802.3 clausula 22) ---------------------------------
// O driver so tenta um endereco por vez e nao diz se o barramento responde.
// Falando MDIO direto da para varrer os 32 enderecos e ler o identificador do
// chip, separando "PHY vivo em outro endereco" de "barramento MDIO mudo".
static int s_mdc  = ETH_MDC_PIN;
static int s_mdio = ETH_MDIO_PIN;
static void mdioSetPins(int mdc, int mdio) { s_mdc = mdc; s_mdio = mdio; }

static inline void mdcTick(bool sample, uint16_t *v, int bit)
{
    digitalWrite(s_mdc, HIGH);
    delayMicroseconds(2);
    if (sample && v) *v |= (uint16_t)(digitalRead(s_mdio) & 1) << bit;
    digitalWrite(s_mdc, LOW);
    delayMicroseconds(2);
}

static void mdioOut(uint32_t val, int bits)
{
    pinMode(s_mdio, OUTPUT);
    for (int i = bits - 1; i >= 0; i--) {
        digitalWrite(s_mdio, (val >> i) & 1);
        delayMicroseconds(1);
        mdcTick(false, nullptr, 0);
    }
}

// Retorna o dado e, em *ta, o bit de turnaround. O turnaround da leitura tem
// DOIS tempos de bit: no primeiro o host solta a linha, no segundo o PHY dirige
// um ZERO. Esse zero e a prova de vida: se ele vier 1, ninguem assumiu o
// barramento e o valor lido nao significa nada.
static uint16_t mdioRead(uint8_t phy, uint8_t reg, uint8_t *ta = nullptr)
{
    mdioOut(0xFFFFFFFFul, 32);                       // preambulo
    mdioOut((1u << 12) | (2u << 10) |                // ST=01, OP=10 (read)
            ((uint32_t)(phy & 0x1F) << 5) | (reg & 0x1F), 14);

    pinMode(s_mdio, INPUT_PULLUP);
    mdcTick(false, nullptr, 0);                      // TA bit 1: host tri-state

    uint16_t taBit = 0;
    mdcTick(true, &taBit, 0);                        // TA bit 2: o PHY dirige 0
    if (ta) *ta = (uint8_t)taBit;

    uint16_t v = 0;
    for (int i = 15; i >= 0; i--) mdcTick(true, &v, i);
    mdcTick(false, nullptr, 0);
    return v;
}

// Leva cada pino candidato a nivel alto e reconsulta o MDIO. Se o nRST desta
// revisao nao estiver no IO16, algum destes o libera e o PHY volta a responder.
static void probeResetPins()
{
    Serial.println("[eth] --- caca ao nRST: varre pinos candidatos ---");
    // NAO incluir pinos de strapping: IO12 (MTDI) define a tensao da flash --
    // em nivel alto no boot coloca a VDD_SDIO em 1,8 V e corrompe a leitura da
    // flash. IO2 e IO15 tambem sao strapping. Ficam de fora.
    const int cand[] = { 16, 4, 5, 14, 17, 32, 33 };

    pinMode(s_mdc, OUTPUT);
    digitalWrite(s_mdc, LOW);

    for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        const int p = cand[i];
        pinMode(p, OUTPUT);
        digitalWrite(p, HIGH);
        delay(200);

        int resp = 0;
        for (uint8_t a = 0; a < 32; a++) {
            uint8_t ta = 1;
            uint16_t id = mdioRead(a, 2, &ta);
            if (!ta) {
                Serial.printf("[eth]   *** IO%d alto -> addr %u RESPONDEU "
                              "(PHYID1=%04X) ***\n", p, a, id);
                resp++;
            }
        }
        Serial.printf("[eth]   IO%-2d em nivel alto: %d respostas\n", p, resp);

        if (resp) {
            Serial.printf("[eth]   >>> O nRST DO PHY PARECE SER O IO%d <<<\n", p);
            return;                       // deixa o pino alto de proposito
        }
        pinMode(p, INPUT);                // devolve e segue
    }
    Serial.println("[eth]   nenhum pino candidato liberou o PHY.");
}

static void probeMdio()
{
    Serial.println("[eth] --- varredura MDIO (32 enderecos) ---");
    pinMode(s_mdc, OUTPUT);
    digitalWrite(s_mdc, LOW);

    int achados = 0, allOnes = 0, allZeros = 0, taBaixo = 0;
    for (uint8_t a = 0; a < 32; a++) {
        uint8_t ta1 = 1, ta2 = 1;
        uint16_t id1 = mdioRead(a, 2, &ta1);         // PHYIDR1
        uint16_t id2 = mdioRead(a, 3, &ta2);         // PHYIDR2
        if (!ta1 || !ta2) {
            taBaixo++;
            Serial.printf("[eth]   addr %2u: TURNAROUND=0 (alguem respondeu!) "
                          "PHYID=%04X%04X\n", a, id1, id2);
        }
        if (id1 == 0xFFFF && id2 == 0xFFFF) { allOnes++;  continue; }
        if (id1 == 0x0000 && id2 == 0x0000) { allZeros++; continue; }
        uint16_t bmcr = mdioRead(a, 0), bmsr = mdioRead(a, 1);
        Serial.printf("[eth]   addr %2u: PHYID=%04X%04X BMCR=%04X BMSR=%04X%s\n",
                      a, id1, id2, bmcr, bmsr,
                      (id1 == 0x0007 && (id2 & 0xFFF0) == 0xC0F0) ? "  <- LAN8720" : "");
        achados++;
    }

    Serial.printf("[eth]   resumo: %d com dado, %d com turnaround=0, "
                  "%d leram 0xFFFF, %d leram 0x0000\n",
                  achados, taBaixo, allOnes, allZeros);
    if (achados) {
        Serial.println("[eth]   -> o MDIO FUNCIONA e o PHY esta vivo.");
    } else if (allZeros == 32) {
        Serial.println("[eth]   -> MDIO preso em 0: linha em curto com o GND,"
                       " ou algo no IO18 segurando o barramento.");
    } else {
        Serial.println("[eth]   -> MDIO so le 1 (linha solta pelo pull-up): nada");
        Serial.println("[eth]      responde. IO18/IO23 obstruidos, ou PHY danificado.");
    }
}

// --- MDIO com os pinos trocados -------------------------------------------
// Se a placa tiver MDC/MDIO invertidos em relacao ao padrao, tudo que fizemos
// ate aqui falharia exatamente do mesmo jeito. Custa 2 s descartar.
static void probeMdioSwapped()
{
    Serial.println("[eth] --- MDIO com pinos TROCADOS (mdc=18 mdio=23) ---");
    pinMode(18, OUTPUT);
    digitalWrite(18, LOW);
    mdioSetPins(18, 23);
    int resp = 0;
    for (uint8_t a = 0; a < 32; a++) {
        uint8_t ta = 1;
        uint16_t id1 = mdioRead(a, 2, &ta);
        if (!ta) {
            Serial.printf("[eth]   addr %2u: TURNAROUND=0 com pinos trocados! "
                          "PHYID1=%04X\n", a, id1);
            resp++;
        }
    }
    mdioSetPins(ETH_MDC_PIN, ETH_MDIO_PIN);
    Serial.printf("[eth]   pinos trocados: %d respostas\n", resp);
}

// --- SMI por hardware (implementacao da Espressif) -------------------------
// Cria so o objeto MAC e usa o read_phy_reg dele. Isso exercita o periferico
// EMAC do ESP32, sem passar pelo meu bit-bang -- e a unica leitura MDIO desta
// investigacao que nao depende de codigo meu.
static esp_err_t medStateChanged(esp_eth_mediator_t *, esp_eth_state_t, void *) { return ESP_OK; }
static esp_err_t medStackInput(esp_eth_mediator_t *, uint8_t *b, uint32_t) { free(b); return ESP_OK; }
static esp_err_t medRegRead(esp_eth_mediator_t *, uint32_t, uint32_t, uint32_t *) { return ESP_OK; }
static esp_err_t medRegWrite(esp_eth_mediator_t *, uint32_t, uint32_t, uint32_t) { return ESP_OK; }

static void probeHwSmi()
{
    Serial.println("[eth] --- SMI por HARDWARE (driver da Espressif) ---");

    static esp_eth_mediator_t med;
    med.phy_reg_read    = medRegRead;
    med.phy_reg_write   = medRegWrite;
    med.stack_input     = medStackInput;
    med.on_state_changed = medStateChanged;

    eth_mac_config_t cfg = ETH_MAC_DEFAULT_CONFIG();
    cfg.smi_mdc_gpio_num  = ETH_MDC_PIN;
    cfg.smi_mdio_gpio_num = ETH_MDIO_PIN;
    cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    cfg.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&cfg);
    if (!mac) { Serial.println("[eth]   esp_eth_mac_new_esp32 falhou"); return; }

    mac->set_mediator(mac, &med);
    esp_err_t e = mac->init(mac);
    Serial.printf("[eth]   mac->init() = %s\n", esp_err_to_name(e));

    int resp = 0;
    for (uint8_t a = 0; a < 32; a++) {
        uint32_t v = 0xDEAD;
        if (mac->read_phy_reg(mac, a, 2, &v) == ESP_OK && v != 0xFFFF && v != 0) {
            uint32_t v2 = 0;
            mac->read_phy_reg(mac, a, 3, &v2);
            Serial.printf("[eth]   addr %2u: PHYID=%04X%04X  <- RESPONDEU\n",
                          a, (unsigned)v, (unsigned)v2);
            resp++;
        }
    }
    Serial.printf("[eth]   SMI por hardware: %d respostas em 32 enderecos\n", resp);
    if (!resp)
        Serial.println("[eth]   -> confirma o bit-bang: barramento mudo.");
}

void netBegin()
{
    WiFi.onEvent(onEvent);

    if (s_sweepMagic != ETH_SWEEP_MAGIC) {   // primeiro boot apos corte de energia
        s_sweepMagic = ETH_SWEEP_MAGIC;
        s_comboIdx   = (g_cfg.ethCombo < N_COMBOS) ? g_cfg.ethCombo : 0;
    }

    // Na ultima tentativa, sonda o hardware ANTES de qualquer ETH.begin(): o
    // esp_eth_driver_install configura MDC/MDIO no EMAC mesmo quando falha, e o
    // periferico continuaria dono dos pinos, invalidando o bit-bang.
    if (s_comboIdx + 1 >= N_COMBOS) {
#if ESPPROG_ETH_DIAG
        // Sondas de bancada: acionam IO0, IO16, IO23/IO18 e IO32/IO33 (que nesta
        // placa sao CFG e 485_EN). So fazem sentido investigando hardware
        // suspeito -- ficam desligadas por padrao. Ligue com -DESPPROG_ETH_DIAG=1.
        probeEthClock();
        probeRmiiPins();
        probeMdio();
        probeMdioSwapped();
        probeResetPins();
        probeHwSmi();
#else
        Serial.println("[eth]   (sondas de diagnostico desligadas; recompile com "
                       "-DESPPROG_ETH_DIAG=1 para investigar o hardware)");
#endif
    }

    // ETH.begin() cria um esp_netif novo a cada chamada, entao NAO pode ser
    // repetido na mesma inicializacao. Para varrer as combinacoes que diferem
    // entre revisoes/clones do WT32-ETH01, cada tentativa usa um boot proprio:
    // o indice vive em RTC_NOINIT (sobrevive ao soft reset) e a combinacao que
    // funcionar e gravada na NVS, para os boots seguintes irem direto nela.
    const EthCombo &c = kCombos[s_comboIdx % N_COMBOS];
    s_phyAddr = c.addr;
    Serial.printf("[eth] tentativa %u/%u: %s\n", s_comboIdx + 1, N_COMBOS, c.desc);

    if (c.lowMs || c.preMs) {
        pinMode(ETH_POWER_PIN, OUTPUT);
        if (c.lowMs) {
            digitalWrite(ETH_POWER_PIN, LOW);    // reset de verdade: derruba tudo
            delay(c.lowMs);                      // e deixa descarregar
        }
        digitalWrite(ETH_POWER_PIN, HIGH);       // sai do reset, oscilador parte
        delay(c.preMs);                          // e estabiliza antes do driver
    }

    // Nada de mexer em MDC/MDIO aqui: o driver e quem configura esses pinos, e a
    // biblioteca de referencia entrega-os intocados. Divergir disso so criaria
    // diferenca entre o nosso caminho e o comprovadamente bom.

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    bool ok = ETH.begin(ETH_PHY_LAN8720, c.addr, ETH_MDC_PIN, ETH_MDIO_PIN,
                        c.rst, ETH_CLK_MODE_SEL);
#else
    bool ok = ETH.begin(c.addr, c.rst, ETH_MDC_PIN, ETH_MDIO_PIN,
                        ETH_PHY_LAN8720, ETH_CLK_MODE_SEL);
#endif

    // Distingue "PHY nao inicializou" de "PHY ok, mas sem cabo": sem isto, os
    // dois casos aparecem igual (eth=down) e o diagnostico vira adivinhacao.
    s_phyOk = ok;
    if (ok) {
        Serial.printf("[eth] PHY LAN8720 OK com %s (aguardando link)\n", c.desc);
        if (g_cfg.ethCombo != s_comboIdx) {   // memoriza para os proximos boots
            g_cfg.ethCombo = s_comboIdx;
            settingsSave();
            Serial.println("[eth] combinacao gravada na configuracao");
        }
    } else if (s_comboIdx + 1 < N_COMBOS) {
        s_comboIdx++;
        Serial.printf("[eth] falhou; reiniciando para tentar %s\n",
                      kCombos[s_comboIdx].desc);
        Serial.flush();
        delay(150);
        ESP.restart();
    } else {
        s_comboIdx = 0;               // zera para varrer de novo no proximo boot
        Serial.println("[eth] ERRO: o PHY nao respondeu no MDIO em NENHUMA das");
        Serial.println("[eth]   combinacoes. 'power up timeout' = o LAN8720 esta");
        Serial.println("[eth]   sem clock de 50 MHz. Confira, nesta ordem:");
        Serial.println("[eth]     1) NADA no IO0 - nem jumper, nem header, nem trilha");
        Serial.println("[eth]     2) NADA no IO16 - e o enable do oscilador");
        Serial.println("[eth]     3) 3V3 estavel sob carga (fonte >= 1 A)");
        Serial.println("[eth]   Seguindo sem Ethernet; web e SNMP ficam inacessiveis.");
    }

    ETH.setHostname(g_cfg.hostname);

    if (!g_cfg.dhcp) {
        bool cfgOk = ETH.config(IPAddress(g_cfg.ip), IPAddress(g_cfg.gw), IPAddress(g_cfg.mask),
                             IPAddress(g_cfg.dns1), IPAddress(g_cfg.dns2));
        Serial.printf("[eth] IP estatico %s -> %s\n",
                      IPAddress(g_cfg.ip).toString().c_str(), cfgOk ? "ok" : "FALHOU");
    } else {
        Serial.println("[eth] DHCP habilitado");
    }
}

// Sonda periodica: se o PHY so acordar tempos depois da energizacao (regulador
// interno carregando devagar, efeito termico, RC de reset com valor errado), a
// varredura do boot nao veria. Aqui o MDIO continua sendo consultado enquanto o
// modulo estiver ligado -- so bit-bang, sem tocar no driver.
void netRetryProbe()
{
#if !ESPPROG_ETH_DIAG
    return;                 // so em modo diagnostico: aciona MDC/MDIO por bit-bang
#else
    if (s_phyOk) return;                       // ja inicializou, nada a fazer

    static uint32_t next = 15000;              // comeca depois da varredura do boot
    if ((int32_t)(millis() - next) < 0) return;
    next = millis() + 10000;

    pinMode(s_mdc, OUTPUT);
    digitalWrite(s_mdc, LOW);

    int resp = 0;
    uint8_t achouAddr = 0;
    uint16_t achouId = 0;
    for (uint8_t a = 0; a < 32; a++) {
        uint8_t ta = 1;
        uint16_t id = mdioRead(a, 2, &ta);
        if (!ta) { resp++; achouAddr = a; achouId = id; }
    }

    uint32_t s = millis() / 1000;
    if (resp) {
        Serial.printf("\n[eth] *** O PHY RESPONDEU em t=%lus! addr=%u PHYID1=%04X ***\n",
                      (unsigned long)s, achouAddr, achouId);
        Serial.println("[eth] o PHY acordou depois do boot -- reiniciando para "
                       "inicializar o Ethernet agora que ele esta vivo.");
        Serial.flush();
        delay(300);
        ESP.restart();
    } else {
        Serial.printf("[eth] t=%lus: MDIO ainda mudo (sonda a cada 10s)\n",
                      (unsigned long)s);
    }
#endif
}

bool      netLinkUp()      { return s_linkUp; }
bool      netPhyOk()       { return s_phyOk; }
bool      netHasIp()       { return s_hasIp; }
IPAddress netLocalIp()     { return ETH.localIP(); }
IPAddress netSubnet()      { return ETH.subnetMask(); }
IPAddress netGateway()     { return ETH.gatewayIP(); }
IPAddress netDns()         { return ETH.dnsIP(); }
String    netMac()         { return ETH.macAddress(); }
uint16_t  netLinkSpeed()   { return ETH.linkSpeed(); }
bool      netFullDuplex()  { return ETH.fullDuplex(); }
