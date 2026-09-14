# WT32-ETH01 — LAN8720 não responde no MDIO (`lan87xx_pwrctl: power up timeout`)

Descritivo do problema para busca em fóruns/issues. Versão em inglês no fim.

---

## Sintoma

Módulo **WT32-ETH01** (ESP32-D0WD + LAN8720, RJ45 integrado). O Ethernet nunca
inicializa. No boot, com Arduino core 2.0.17 / platform-espressif32 6.9.0:

```
E (338) lan87xx: lan87xx_pwrctl(409): power up timeout
E (338) lan87xx: lan87xx_init(491): power control failed
E (338) esp_eth: esp_eth_driver_install(215): init phy failed
[   345][E][ETH.cpp:324] begin(): esp_eth_driver_install failed
```

O resto do firmware roda normalmente (HTTP, SNMP, UART, tasks). Só o PHY não sobe.
Nenhum link nos LEDs do RJ45 nem na porta do switch.

Configuração usada (a padrão do WT32-ETH01):

```cpp
ETH.begin(/*addr*/1, /*power*/16, /*mdc*/23, /*mdio*/18,
          ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
```

## O que já foi testado e DESCARTADO

**1. Firmware de fábrica falha igual.** O módulo veio com o firmware do próprio
fabricante (`Project name: ethernet_basic`, Wireless-Tag, compilado em nov/2019,
caminho `D:/work/wireless-tag/project/esp32-eth/code/esp-idf/examples/app/basic/`).
Ele falha com a mesma mensagem (`lan8720_pwrctl: power up failed`) e entra em boot
loop. Ou seja: **o problema existe desde antes de qualquer firmware nosso.**

**2. Alimentação.** Inicialmente havia brownout (`Brownout detector was triggered`)
alimentando pelo USB-serial. Resolvido com fonte externa **5 V / 2 A**. Depois disso:
uptime de 8+ minutos, heap constante, zero resets. **Não é alimentação.**

**3. GPIO0 (entrada do clock de 50 MHz).** Havia um header soldado com jumper
encaixado no IO0. Removido completamente. Sem mudança.

**4. O oscilador de 50 MHz ESTÁ funcionando.** Medido por software: com o IO16
forçado em nível alto e após 500 ms de estabilização, o IO0 foi amostrado 20 000
vezes com pull-up interno e 20 000 vezes com pull-down interno:

```
IO0: pull-up 9689/20000 (48%), pull-down 9644/20000 (48%)
```

48% nos dois casos. Um pino solto seguiria o pull (≈100% / ≈0%); um sinal realmente
acionado ignora os pull-ups internos. **O clock existe e é acionado.**

**5. Endereço do PHY.** Varridos os 32 endereços possíveis por MDIO bit-bang.

**6. Sequenciamento de power-on.** Esta é a causa mais reportada para esse módulo —
a WT32-ETH01 amarra o `nRST` do LAN8720 e o `EN` do oscilador no mesmo GPIO16, então
o PHY sai do reset junto com a partida do cristal. Testadas 6 combinações, uma por
boot:

| # | Config | Resultado |
|---|---|---|
| 1 | addr=1, sem reset, IO16 ligado **1 s** antes | falhou |
| 2 | addr=1, sem reset, IO16 ligado **3 s** antes | falhou |
| 3 | addr=0, sem reset, IO16 ligado 1 s antes | falhou |
| 4 | addr=1, reset=IO16, após 1 s ligado | falhou |
| 5 | addr=1, reset=IO16 (padrão) | falhou |
| 6 | addr=0, reset=IO16 | falhou |

**7. Fiação externa no MDIO é impossível.** IO18 (MDIO) e IO23 (MDC) são trilhas
internas do módulo — não saem no header. O header expõe apenas IO0, IO1, IO2, IO3,
IO4, IO5, IO12, IO14, IO15, IO17, IO32, IO33, IO35, IO36, IO39 e alimentação.

## Estado atual

MDIO bit-bang (IEEE 802.3 cláusula 22) direto nos IO23/IO18, varrendo os 32
endereços, lendo PHYIDR1/PHYIDR2:

```
resumo: 0 com dado, 0 com turnaround=0, 32 leram 0xFFFF, 0 leram 0x0000
```

Todos os endereços leem `0xFFFF` — linha solta no pull-up, ninguém dirige o
barramento.

O campo **`turnaround=0`** é o teste mais direto: numa leitura de cláusula 22, o
turnaround tem dois tempos de bit e no segundo **o PHY dirige um zero**, antes mesmo
de qualquer dado. Em 64 transações (32 endereços × 2 registradores), esse bit nunca
veio baixo. Não é "o PHY respondeu algo estranho" — é **ninguém no barramento**.

(Nota metodológica: a primeira versão desta sonda clocava apenas 1 tempo de
turnaround em vez de 2, e descartava justamente esse bit. Bug corrigido; o resultado
não mudou.)

## Teste com o módulo ISOLADO

O módulo foi **removido de qualquer montagem**, ficando apenas com:

- fonte externa 5 V / 2 A no pino `5V`
- CP2102 no `IO1`/`IO3`/`GND` (só console serial)
- cabo de rede no RJ45
- **nada em nenhum outro pino** — `IO0` completamente livre, sem header, sem trilha

Resultado **idêntico** ao da montagem: seis sequências falham, clock real presente,
MDIO mudo nas três implementações, `mac->init() = ESP_OK`.

Isso elimina a última variável ambiental. O defeito acompanha o módulo, não a
montagem — relevante para RMA: não é atribuível à fiação do usuário.

---

## O PHY está ENERGIZADO (correção de um diagnóstico anterior)

`RXD0` (IO25), `RXD1` (IO26) e `CRS_DV` (IO27) são **saídas do PHY** para o ESP32,
e independem completamente do MDIO. Amostrando-as com pull-up e pull-down internos:

```
RXD0  (IO25): pull-up 0/5000, pull-down 0/5000 -> DIRIGIDO em 0
RXD1  (IO26): pull-up 0/5000, pull-down 0/5000 -> DIRIGIDO em 0
CRS_DV(IO27): pull-up 0/5000, pull-down 0/5000 -> DIRIGIDO em 0
```

Leem 0 **mesmo contra o pull-up interno** — algo energizado está dirigindo essas
linhas ativamente. **O LAN8720 tem alimentação e dirige suas saídas.** Isso derruba a
hipótese de chip morto ou VDDCR sem tensão.

Mas três fatos precisam ser conciliados:

1. o PHY dirige as saídas RMII (está energizado)
2. não responde ao MDIO por nenhum caminho
3. **não auto-negocia** — o PC não vê link nenhum

O item 3 é decisivo e elimina a hipótese de "só o MDIO quebrado": a auto-negociação é
analógica, interna ao PHY, e **não usa MDIO**. Um LAN8720 operante negociaria com o
switch mesmo com a gerência inacessível.

O quadro que encaixa nos três é **PHY alimentado e preso em reset**: nesse estado ele
segura as saídas em nível baixo, ignora o MDIO e não negocia.

### E o reset não vem do ESP32

Sete pinos candidatos foram levados a nível alto, com nova varredura MDIO após cada
um — `IO16, IO4, IO5, IO14, IO17, IO32, IO33` (mais `IO2`, `IO12` e `IO15` numa
versão anterior). **Zero respostas em todos.**

O diagrama de blocos do fabricante corrobora: a única ligação desenhada entre o
WT32-S1 e o LAN8720A é o **RMII**, e o clock de 50 MHz é um bloco separado que
alimenta os dois. Não há linha de reset partindo do ESP32.

Conclusão: o `nRST` do PHY é gerado por um RC no próprio rail de 3V3. Se esse
circuito estiver defeituoso, o reset nunca é liberado — e **nenhum software alcança
isso**. O conserto seria um pull-up de 10 k do `nRST` para 3V3, solda no chip.

---

## Conclusão — software esgotado

Três implementações **independentes** de leitura MDIO, todas em silêncio:

| Implementação | Resultado |
|---|---|
| Bit-bang próprio (IO23/IO18) | 0 respostas em 32 endereços |
| Bit-bang com pinos trocados (IO18/IO23) | 0 respostas em 32 endereços |
| **SMI por hardware da Espressif** (`esp_eth_mac_new_esp32` → `read_phy_reg`) | 0 respostas em 32 endereços |

E o dado decisivo sobre o lado ESP32:

```
mac->init() = ESP_OK
```

**O periférico EMAC do ESP32 inicializa com sucesso.** Ele só faz isso com o clock
RMII de 50 MHz válido — é confirmação independente, pelo próprio hardware, de que o
clock está bom e de que a metade ESP32 do circuito está íntegra.

Seis sequências de reset/alimentação foram testadas, incluindo pulsos reais
`LOW 500 ms → HIGH` (não apenas subir o pino), com esperas de 250 ms a 3 s, nos
endereços 0 e 1, com e sem o driver reassertando o reset. Pull-ups internos forçados
no barramento SMI antes da inicialização. Nenhuma diferença.

**Nenhuma alteração de firmware pode corrigir isto.** O ESP32 funciona; o LAN8720 não
responde a nenhum mestre MDIO, por nenhum caminho.

---

## Próximos passos, em ordem de custo

### 1. Medir o VDDCR — 1,2 V do núcleo digital  ← faça isto primeiro

O LAN8720 gera internamente sua tensão de núcleo de **1,2 V**, disponível no pino
**VDDCR**, que exige um capacitor de desacoplamento externo. Esse rail alimenta
**apenas a parte digital** do chip — inclusive a interface MDIO.

Isso explica com precisão a combinação que observamos:

| Observação | Explicação se o VDDCR estiver morto |
|---|---|
| Clock de 50 MHz presente no IO0 | o oscilador é **externo** ao LAN8720, não depende dele |
| MDIO mudo, turnaround nunca baixo | o núcleo digital não está energizado |
| Resto do módulo perfeito | só o rail interno do PHY falhou |

Causas: solda fria no capacitor de desacoplamento (comum em QFN barato) ou o
regulador interno queimado.

**Como medir:** multímetro em DC, ponta preta no GND do módulo, ponta vermelha no
capacitor de desacoplamento adjacente ao pino VDDCR do LAN8720 (medir no capacitor é
mais seguro que na perna do QFN). Confira a numeração do pino no datasheet do
LAN8720A para o seu encapsulamento antes de tocar.

- **~1,2 V** → o núcleo está energizado; o problema é outro
- **0 V ou muito abaixo** → **causa raiz encontrada**; tente ressoldar o capacitor

### 2. Modificação do RC de reset (nRST)

O datasheet exige que o `nRST` fique baixo até a alimentação estabilizar **e** o clock
estar limpo. A WT32-ETH01 aciona o `EN` do oscilador e o `nRST` do PHY pelo **mesmo
GPIO16**, então o PHY pode ler seus pinos de strap antes do clock estabilizar e travar
a máquina de estados.

Modificação: cortar a trilha do `nRST` que vem do GPIO16 e montar no pino de reset um
RC próprio — **pull-up de 10 k para 3V3 + capacitor de 10 µF para GND**. Isso impõe o
atraso em hardware, independente de qualquer sequenciamento por software.

Já testamos o equivalente em software (IO16 ligado 1 s e 3 s antes, sem re-assertar o
reset) sem sucesso, mas a versão em hardware é mais forte porque também cobre o
instante do power-on, antes do firmware existir.

### 3. Substituir o módulo (ou fazer reflow de um LAN8720 novo)

Há relatos consistentes de lotes de clones da WT32-ETH01 com LAN8720 falsificado ou
danificado de fábrica. O firmware **de fábrica** falhando numa placa nova é o
indicador mais forte de hardware inoperante na origem.

### Descartado: `ETH_CLOCK_GPIO17_OUT`

Dessoldar o cristal e gerar os 50 MHz pelo APLL interno do ESP32 resolve **ausência de
clock**. Nossa medição (48% com pull-up e 48% com pull-down) prova que o clock existe
e é acionado, então essa modificação não deve ressuscitar o chip.

---

## A pergunta

Com o clock de 50 MHz comprovadamente presente, alimentação estável, MDC/MDIO
inacessíveis externamente, e as seis combinações de endereço/reset/sequenciamento
esgotadas — **o LAN8720 está morto, ou falta algo?**

Interessa especialmente saber de quem:

- teve `power up timeout` **com o clock comprovadamente presente** e conseguiu
  resolver (o que era?);
- conhece outra dependência de hardware do LAN8720 na WT32-ETH01 (nRST separado,
  strap, algum resistor que precise sair);
- tem experiência com o mod de acrescentar 10 µF no RC de reset do LAN8720, ou com
  dessoldar o cristal e usar `ETH_CLOCK_GPIO17_OUT`;
- já viu o firmware **de fábrica** da Wireless-Tag falhar no PHY numa placa nova.

---

# English version

## Symptom

**WT32-ETH01** (ESP32-D0WD + LAN8720, onboard RJ45). Ethernet never initializes.
Arduino core 2.0.17 / platform-espressif32 6.9.0:

```
E (338) lan87xx: lan87xx_pwrctl(409): power up timeout
E (338) lan87xx: lan87xx_init(491): power control failed
E (338) esp_eth: esp_eth_driver_install(215): init phy failed
[   345][E][ETH.cpp:324] begin(): esp_eth_driver_install failed
```

Everything else runs fine (HTTP, SNMP, UART, FreeRTOS tasks). No link LEDs, no link
on the switch port. Standard config: `ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720,
ETH_CLOCK_GPIO0_IN)`.

## Already ruled out

1. **Stock vendor firmware fails identically.** Board shipped with Wireless-Tag's own
   `ethernet_basic` (built Nov 2019); it fails with `lan8720_pwrctl: power up failed`
   and boot-loops. The fault predates any of our firmware.
2. **Power.** Initial brownouts when powered from the USB-serial adapter; fixed with an
   external 5 V / 2 A supply. Now 8+ minutes uptime, flat heap, zero resets.
3. **GPIO0 loading.** A soldered header with a jumper was present on IO0; fully
   removed. No change.
4. **The 50 MHz oscillator IS running.** With IO16 forced high and 500 ms settle, GPIO0
   sampled 20 000× with internal pull-up and 20 000× with internal pull-down:
   `pull-up 9689/20000 (48%), pull-down 9644/20000 (48%)`. A floating pin would follow
   the pull (~100%/~0%); a truly driven signal overrides the internal pulls.
   **The clock is present and driven.**
5. **PHY address.** All 32 addresses scanned via bit-banged MDIO.
6. **Power-on sequencing** (the most-reported cause — WT32-ETH01 ties LAN8720 `nRST`
   and the oscillator `EN` to the same GPIO16). Six combinations tried, one per boot:
   addr 1/0 × reset=IO16/none × IO16 pre-enabled 0/1/3 s before init. All failed.
7. **External MDIO wiring is impossible.** IO18 (MDIO) and IO23 (MDC) are internal
   traces; they are not exposed on the header.

## Current state

Bit-banged MDIO (IEEE 802.3 clause 22) directly on IO23/IO18, scanning all 32
addresses, reading PHYIDR1/PHYIDR2:

```
summary: 0 responded, 32 read 0xFFFF, 0 read 0x0000
```

Every address reads `0xFFFF` — line idle at the pull-up, nothing drives the bus.

## Question

With the 50 MHz clock proven present, power stable, MDC/MDIO not externally
accessible, and address/reset/sequencing combinations exhausted — **is the LAN8720
dead, or is something still missing?**

Especially interested in anyone who: hit `power up timeout` **with the clock proven
present** and fixed it; knows another LAN8720 hardware dependency on this board
(separate nRST, a strap, a resistor to remove); has tried the 10 µF cap on the
LAN8720 reset RC, or removing the crystal and using `ETH_CLOCK_GPIO17_OUT`; or has
seen Wireless-Tag's **stock** firmware fail on the PHY of a new board.
