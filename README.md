# espprog — programador da TRIFLEX CONNECT por Ethernet

Firmware para **WT32-ETH01** (ESP32 + LAN8720) que substitui o programador de mão da
placa de cancela **TRIFLEX CONNECT v2.36.33**, expondo a mesma tela e as mesmas quatro
teclas por rede.

- **Interface web** — o LCD 2×16 ao vivo e os quatro botões, como se você estivesse com
  o programador na mão. Configuração de rede (DHCP ou IP fixo), SNMP e ajustes finos.
- **API REST/JSON** — puxar a tela e gerar comandos de tecla por script.
- **SNMP v2c** — cada campo da tela em um OID próprio, para Zabbix e afins.
- **Acelerômetro e giroscópio** (MPU-6050 / GY-521, opcional) — inclinação, vibração,
  pancadas e movimento, na web, na API e no SNMP. Ver §1.1.

Endereço de fábrica: **`192.168.1.21/24`**, gateway `192.168.1.1`,
também acessível em `http://espprog.local`.

**Leia primeiro:** [`docs/COMO-FUNCIONA.md`](docs/COMO-FUNCIONA.md) — o que foi
estabelecido na bancada e **corrige o levantamento original** em pontos decisivos
(barramento de 9 V, a placa não inicia a conversa sozinha, `+`/`−` simultâneos).

API: [`docs/API.md`](docs/API.md) · MIB: [`mib/ESPPROG-MIB.txt`](mib/ESPPROG-MIB.txt) ·
Diagnóstico do PHY: [`docs/DIAGNOSTICO-PHY.md`](docs/DIAGNOSTICO-PHY.md)

---

## 1. Ligação

Conector PROG da placa (4 fios, lado a lado):
**P1 GND · P2 D3 (RX da placa) · P3 D2 (TX da placa) · P4 VCC_PROG**

| GPIO | Função | Ligação |
|---|---|---|
| **IO5** | RX — recebe D2 (placa → prog) | via Q2 (2N7000) + R3 10k para 3V3 |
| **IO17** | TX — envia D3 (prog → placa) | via Q3 (2N7000) + R5 10k para 3V3 |
| **IO4** | LED de atividade | LED → 1k → GND (opcional) |
| **IO14** | buzzer | 1k → base NPN → buzzer ativo 5 V (opcional) |
| **IO32** | SDA do MPU-6050 | pino `SDA` do GY-521 (opcional — ver §1.1) |
| **IO33** | SCL do MPU-6050 | pino `SCL` do GY-521 (opcional — ver §1.1) |
| IO0 | BOOT (gravação) | **também é o clock do LAN8720 — não usar para mais nada** |
| EN | RST | botão para GND |
| IO1 / IO3 | UART0 (gravação + log, 115200) | header J2 → CP2102 |
| IO16, 18, 19, 21–23, 25–27 | LAN8720 (RMII/MDIO) | reservados pelo módulo |

GND precisa ser curto e comum com a cancela. `VCC_PROG` (9 V, medido) referencia o
pull-up do lado alto; não alimenta nada.

> **Não monte o pull-up do `D2` (R4).** Com ele a placa **para de transmitir**. Verificado
> nos dois sentidos.
>
> A montagem validada também tem **4 × 4,7 kΩ em paralelo entre `VCC_PROG` e `GND`**
> (≈ 7,5 mA). Reproduza — ver [`COMO-FUNCIONA.md`](docs/COMO-FUNCIONA.md) §1.

### Pinos que NÃO podem ser usados

O header do WT32-ETH01 expõe apenas **IO0, IO1(TXD), IO2, IO3(RXD), IO4, IO5, IO12,
IO14, IO15, IO17, IO32, IO33, IO35, IO36, IO39** e a alimentação. Todo o resto é trilha
interna para o LAN8720:

| GPIO | Função interna | Consequência de encostar nele |
|---|---|---|
| **IO0** | entrada do clock de 50 MHz | qualquer carga — até um fio solto num header — derruba o clock e o PHY não inicializa |
| **IO16** | enable do oscilador | sem ele o oscilador não parte |
| **IO18** | MDIO | não sai no header |
| **IO23** | MDC | não sai no header |
| IO19, 21, 22, 25, 26, 27 | RMII | não saem no header |

O IO0 é o único desses que aparece no header (serve de BOOT), e é justamente o mais
sensível: ele carrega os 50 MHz. Deixe-o **completamente livre** em operação normal —
jumper só durante a gravação.

### 1.1 Acelerômetro e giroscópio — MPU-6050 (módulo GY-521)

Opcional. Sem o módulo o firmware funciona igual; com ele, aparece a aba **Sensor** na
interface, o `GET /api/imu` e o grupo SNMP `.63050.7`.

| Pino do GY-521 | Liga em (WT32-ETH01) | Observação |
|---|---|---|
| **VCC** | **3V3** | o módulo consome ~4 mA; não pesa na fonte |
| **GND** | **GND** | |
| **SCL** | **IO33** | clock I2C |
| **SDA** | **IO32** | dados I2C |
| AD0 | GND, ou deixe solto | endereço `0x68`. Em 3V3 vira `0x69` — o firmware acha os dois |
| XDA, XCL | **não ligar** | barramento auxiliar, para magnetômetro externo |
| INT | **não ligar** | o firmware lê por varredura a 100 Hz |

Por que **IO32/IO33** e não outros pinos livres do header:

- **IO12** com pull-up no boot configura a flash para 1,8 V e **o módulo não liga** — e
  o GY-521 já vem com pull-up de 4,7 kΩ no SDA e no SCL;
- **IO2** em nível alto impede entrar no modo de gravação;
- **IO15** também é pino de *strapping* (log de boot);
- **IO35, IO36 e IO39** são só entrada: não puxam a linha para baixo, e I2C precisa disso.

Cuidados de montagem:

- **Fio curto, até ~50 cm.** I2C foi feito para dentro da placa. O firmware usa 100 kHz
  para ter folga; se `errors` em `/api/imu` subir, encurte o cabo ou use par trançado
  (SDA com GND, SCL com GND).
- O GY-521 já tem os resistores de pull-up — não precisa acrescentar nada.
- **Para medir o ângulo do braço da cancela**, prenda o módulo com o **eixo Y ao longo do
  braço** (as setas X/Y estão impressas na placa): o ângulo aparece em `roll`. Ver
  [`docs/API.md`](docs/API.md) §3.1.
- **Ligue com o módulo desenergizado.** Inserir com o ESP ligado funciona — o firmware
  procura o sensor a cada 2 s —, mas não há por que arriscar.

No boot, o console serial confirma:

```
[imu] MPU-6050 em 0x68 (WHO_AM_I=0x68), +-8 g, +-500 graus/s, 100 Hz
[imu] giroscopio calibrado: bias -1.83 0.42 0.17 graus/s
```

ou, sem o módulo (ou com SDA/SCL trocados):

```
[imu] nenhum MPU-6050 em SDA=IO32 SCL=IO33 (0x68/0x69); procurando a cada 2 s
```

A calibração do giroscópio roda sozinha e só conclui com **2 s sem movimento**; enquanto
isso `moving` fica em `false`. O heartbeat mostra `imu=ok` ou `imu=ausente`.

Muitos GY-521 vendidos como MPU-6050 trazem um MPU-6500 ou um clone. O firmware identifica
o chip pelo registrador `WHO_AM_I` e se ajusta; o modelo aparece em `/api/imu` e na aba
Sensor. Em clones desconhecidos a temperatura pode sair deslocada — o resto funciona.

---

## 2. Compilar e gravar

Requer [PlatformIO Core](https://platformio.org/install/cli). A porta está fixada em
`COM8` no `platformio.ini` — ajuste se mudar.

```bash
pio run              # compila
pio run -t upload    # grava
pio device monitor   # log da UART0 a 115200
```

O WT32-ETH01 **não tem auto-reset**. Para gravar:

1. segure o botão **BOOT** (IO0);
2. dê um toque no **RST** (EN);
3. solte o **BOOT**;
4. rode `pio run -t upload`.

Depois da gravação, um toque no RST devolve o módulo à operação normal.

---

## 3. Primeira energização — faça nesta ordem

O firmware fala num barramento de um equipamento em operação. Vale a pena não pular etapas.

1. **Grave o firmware com a placa da cancela desligada.**
2. Ligue só o ESP e confirme no `pio device monitor`:
   `[eth] ip=192.168.1.21 ... (estatico, 100 Mbps, full-duplex)`.
3. Abra `http://192.168.1.21`, vá em **Avançado** e **desmarque
   "Transmitir respostas para a placa"** (modo escuta). Salve.
4. **Agora ligue a placa da cancela.**
5. Volte na aba **Tela**. O indicador *barramento PROG* deve ficar verde, o LCD deve
   mostrar o splash `TRIFLEX / CONNECT v2.36.33` e depois a tela de status, e em
   *Barramento* os contadores de polls e telas devem subir com **quadros ruins e
   checksum ruim em zero**.
   - Se o link não sobe: D2/D3 trocados, GND aberto, ou a placa não está energizada.
   - Se sobem quadros ruins: ruído na linha ou nível lógico marginal.
6. Só depois disso, **remarque "Transmitir respostas"** e teste as teclas. A placa deve
   responder ao toque com beep e uma tela nova.

O modo escuta continua disponível a qualquer momento — é a forma segura de diagnosticar
sem colocar nada na linha.

---

## 3.1 Alimentação — o erro mais comum

O ESP32 sozinho consome ~80 mA, mas assim que o `ETH.begin()` energiza o LAN8720 pelo
GPIO16 o consumo salta para **150–250 mA**, com um pico maior ainda no instante em que o
PHY liga. Se a fonte não segura isso, a tensão afunda e o detector de brownout reseta o
chip — em loop, antes mesmo do firmware imprimir a primeira linha.

**Não alimente pelo pino 3V3 de um adaptador USB-serial.** O regulador desses adaptadores
entrega ~100 mA. Use o jack de 5 V com uma fonte de **1 A ou mais** (quem regula é o
AMS1117 do próprio módulo), e confirme que o eletrolítico de 470 µF está populado — ele
é quem absorve o pico de partida do PHY.

Sintomas no log serial (115200), em ordem de gravidade:

```
Brownout detector was triggered          <- fonte fraca, resolva isto primeiro
ets Jun  8 2016 ... rst:0x1 (POWERON_RESET)
```

```
E (818) lan8720: lan8720_pwrctl(302): power up failed
E (818) lan8720: lan8720_init(338): power on Ethernet PHY failed
```

O segundo bloco é o firmware **de fábrica** do WT32-ETH01 falhando pelo mesmo motivo —
serve como confirmação independente de que o problema é elétrico, não de software.

Para medir: multímetro entre `3V3` e `GND` do módulo durante o boot. Abaixo de ~2,8 V
o brownout dispara.

> Existe o truque de desabilitar o detector de brownout por software
> (`WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`). **Não use aqui.** Ele só esconde o
> sintoma: o PHY continua sem tensão para operar, o Ethernet fica instável e escrita em
> flash com tensão baixa corrompe dados.

---

## 3.2 Diagnóstico do PHY Ethernet

Se o `eth` não subir, o firmware não deixa você no escuro. O heartbeat separa os dois
casos que de fora parecem iguais:

```
[hb] up=35s phy=ok     eth=down ...   -> PHY vivo, falta cabo/negociação
[hb] up=35s phy=FALHOU eth=down ...   -> o PHY não respondeu; veja o log de boot
```

Quando o PHY falha, o boot executa três diagnósticos automaticamente:

**1. Varredura de configurações.** Testa quatro combinações de endereço de PHY e pino de
reset, uma por boot (o índice vive em `RTC_NOINIT`), e grava na NVS a que funcionar:

```
[eth] tentativa 1/4: addr=1 reset=IO16   -> falhou; reiniciando...
[eth] tentativa 4/4: addr=0 reset=nenhum
```

**2. Sonda do clock de 50 MHz.** Força o enable pelo IO16 e amostra o IO0 20 mil vezes.
A amostragem alias o sinal, mas o que importa é a alternância:

```
[eth] IO0: 9676 de 20000 amostras em nivel alto
[eth]   -> ALTERNANDO: o clock de 50 MHz EXISTE.
```

Perto de 50% = oscilador saudável. `0` ou `20000` = clock parado.

**3. Varredura MDIO.** Fala IEEE 802.3 cláusula 22 na unha pelos IO23/IO18 e lê o
identificador do chip nos 32 endereços possíveis:

```
[eth]   addr  1: PHYID=0007C0F0 BMCR=3100 BMSR=7809  <- LAN8720
[eth]   resumo: 1 responderam, 31 leram 0xFFFF, 0 leram 0x0000
```

| Resumo | Conclusão |
|---|---|
| algum endereço responde | PHY vivo — problema é de driver, e você tem os registradores |
| tudo `0xFFFF` | linha solta, ninguém responde: **PHY danificado** |
| tudo `0x0000` | MDIO preso em terra: curto na linha |

As sondas rodam **antes** do `ETH.begin()` da última tentativa. Isso é proposital: o
`esp_eth_driver_install` configura MDC/MDIO no EMAC mesmo quando falha, e o periférico
continuaria dono dos pinos, invalidando o bit-bang.

Combinando os três: clock presente + MDIO mudo em 32 endereços = o LAN8720 não responde,
e como IO18/IO23 nem saem no header, não há fiação externa que possa ser a causa. Nesse
caso o módulo é para troca.

---

## 4. Rede

Aba **Rede**:

- **DHCP** — marque a caixa e salve; o módulo reinicia e pega IP do servidor.
- **IP fixo** — desmarque, preencha IP, máscara, gateway e os dois DNS, salve.

Mudança de IP só vale após o reinício (o próprio botão já reinicia). Se você perder o
acesso, use `http://espprog.local` ou o console serial, que imprime o IP a cada boot.
O botão **Restaurar padrões de fábrica** volta para `192.168.1.21` fixo.

---

## 5. SNMP

Aba **SNMP**: habilitar/desabilitar, porta, **community de leitura** e **community de
escrita** (vazia = `SET` desabilitado), além de `sysContact` e `sysLocation`.

Raiz privada `1.3.6.1.4.1.63050` — número **provisório**. Se sua empresa tem um PEN da
IANA, troque `-DESPPROG_ENTERPRISE=` no `platformio.ini` **e** o `enterprises` da MIB.

```bash
snmpwalk -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050
snmpget  -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050.4.3.0   # BOT
snmpget  -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050.5.1.0   # ciclos
```

O grupo `system` padrão também responde, então `snmpwalk` a partir da raiz funciona e as
ferramentas de descoberta identificam o equipamento.

**Ciclos, RPM e encoder** só existem enquanto a placa exibe a tela de ciclos. Cada um tem
um OID `*Age` companheiro dizendo há quantos ms foi lido (`4294967295` = nunca). Sempre
consulte o `*Age` antes de confiar no valor — ver §6.

Tabela completa de OIDs: [`docs/API.md`](docs/API.md) §8, e também na própria aba SNMP
da interface web.

---

## 6. Varredura automática de menus

Para manter ciclos/RPM/encoder frescos sem ninguém mexendo, o firmware pode navegar os
menus sozinho: aba **Avançado → Varredura automática**.

Sequência no formato `+,W1500,-,W1500` — teclas `+ - < >` e esperas `W<ms>`.
O padrão vai até a tela de ciclos, espera 1,5 s e volta.

> ⚠️ O mapa completo de menus da placa **não foi levantado**. Uma sequência errada pode
> entrar em telas de configuração e alterar parâmetros da cancela. Valide a sequência na
> mão antes de habilitar, e em equipamento de produção prefira o disparo sob demanda
> (`POST /api/scan`, ou o botão *Executar agora*) a deixar o modo automático ligado.

Por isso a varredura vem **desabilitada de fábrica**.

---

## 7. Como funciona

### Protocolo

Quadro, nos dois sentidos:

```
7E | LEN | 01 | CMD | DADOS… | CHK | TAIL
```

`LEN` é o tamanho total. `TAIL` = `0x51` quando a placa fala, `0x81` quando o
programador fala. `CHK` = XOR de `LEN`…último byte de dados, semente `0x81`.

A placa pergunta, o emulador responde — **nunca** inicia transação:

| CMD | ASCII | O que é | Resposta |
|---|---|---|---|
| `3F` | `?` | poll, a cada 23,68 ms | eco + estado das teclas |
| `53` | `S` | tela inteira, 32 bytes ASCII | eco + teclas |
| `42` | `B` | beep (`t_on t_off rep`) | eco + teclas |
| `4C` | `L` | luz/contraste | eco + teclas |

A resposta é sempre `7E 08 01 <CMD ecoado> <TECLA_A> <TECLA_B> <CHK> 81`, seguida de um
byte `0x00` isolado ~2,2 ms depois.

Teclas, em dois canais independentes: repouso `B4 B4`; `−` = `54 B4`; `+` = `A8 B4`;
`←` = `B4 54`; `→` = `B4 A8`; **`−`+`+` = `48 B4`** (valor capturado do programador
real — ver [`COMO-FUNCIONA.md`](docs/COMO-FUNCIONA.md) §3).

**A placa não inicia a conversa sozinha.** Com o conector ocioso ela repete o splash a
cada 1000 ms e só começa a pollar depois que um programador se anuncia. O firmware
detecta isso e reengata sozinho — ver [`COMO-FUNCIONA.md`](docs/COMO-FUNCIONA.md) §2.

### Arquitetura

O ponto crítico é o *turnaround*: a resposta precisa sair **2,8–3,3 ms** depois do fim do
pedido. Isso não pode depender da rede.

```
      core 1                                  core 1 (prio 1)
 ┌──────────────────────┐               ┌──────────────────────────┐
 │ task "proglink"      │  memória      │ loopTask                 │
 │ prio 19              │  compartilhada│  · HTTP  (WebServer)     │
 │  · parser do quadro  │◄─── mux ────► │  · SNMP  (UDP 161)       │
 │  · resposta em 3 ms  │               │  · varredura de menus    │
 │  · parser da tela    │               └──────────────────────────┘
 └──────────────────────┘
 ┌──────────────────────┐
 │ task "progout" prio 4│  buzzer, LED, watchdog de tecla
 └──────────────────────┘
```

A task do barramento tem prioridade 19 e preempta o `loopTask`, então tráfego HTTP ou
SNMP não desloca a resposta. Toda a comunicação com a rede é assíncrona: a tecla mais
recente recebida pela rede é o que vai na próxima resposta, e cada comando de tela traz o
LCD inteiro — não há estado a sincronizar.

Uma tecla vinda da rede é mantida por um número configurável de polls (padrão 3, ≈ 70 ms)
para garantir que a placa a enxergue, e um watchdog de 3 s solta qualquer tecla presa
caso o cliente desapareça.

---

## 8. Ajustes finos (aba Avançado)

| Parâmetro | Padrão | Para quê |
|---|---|---|
| Transmitir respostas | ligado | desligue para o modo escuta |
| Turnaround | 3000 µs | medido no programador original: 2800–3300 µs |
| Intervalo até o `0x00` | 2100 µs | ~2 tempos de byte |
| Polls com a tecla ativa | 3 | mínimo observado para a placa reconhecer: 2 |
| Unidade do beep | 10 ms | **hipótese** — `B 14 14 01` daria 200 ms |
| Buzzer / LED | ligados | saídas locais |

---

## 9. Acentuação do LCD

A placa usa um charset próprio, que **não** é o HD44780 A00 puro. A única
correspondência confirmada em captura é `0xEA` = `ê` (visto em `Português-Brasil`).

Bytes ≥ `0x80` sem mapeamento aparecem como `·`. Para completar a tabela:

1. navegue até uma tela com acento;
2. na aba **Tela**, clique em **ver HEX** — cada célula mostra o byte;
3. adicione a linha correspondente em `kCharMap[]`, em `src/proglink.cpp`;
4. recompile.

`raw` / `screenRawHex` sempre trazem os 32 bytes originais, então nenhuma informação se
perde por causa do mapeamento.

---

## 10. Pendências herdadas da captura

Itens marcados como **[a confirmar]** no levantamento original, que continuam abertos e
merecem uma recaptura a ≥ 500 kHz:

1. O programador original transmite algo no *power-up*? (o emulador não transmite)
2. Existem comandos além de `3F 53 42 4C`? (o emulador ecoa qualquer CMD válido)
3. Significado de `L 64 0A`.
4. Unidade de tempo do beep — configurável, padrão 10 ms.
5. Endereço diferente de `01`? (o emulador exige `01`)
6. Existe escape de `7E` nos dados?
7. O byte `0x00` final é obrigatório? (o emulador sempre envia)
8. A placa valida o `TAIL`?
9. Auto-repeat de tecla.
10. Tabela completa de acentos — ver §9.
11. Timeout da placa no meio de um menu.
12. Tensão de `VCC_PROG` (3,3 ou 5 V — o conversor funciona nos dois).

Os contadores `badFrames` / `badChecksum` e o campo `unknown` em `/api/state` ajudam a
fechar os itens 2, 5 e 6 em campo: se aparecer comando desconhecido ou quadro rejeitado
durante uso normal, é sinal de que o levantamento está incompleto.

---

## 11. Estrutura

```
platformio.ini            build, pinos, raiz OID
src/
  main.cpp                setup/loop
  config.h                constantes do protocolo e padrões
  settings.h/.cpp         configuração persistente (NVS)
  proglink.h/.cpp         motor do protocolo, parser da tela, teclas, buzzer
  netmgr.h/.cpp           Ethernet (DHCP/estático), mDNS
  websrv.h/.cpp           HTTP: página e API REST
  web_ui.h                interface web embarcada
  snmpagent.h/.cpp        agente SNMP v1/v2c próprio (BER na mão)
  scan.h/.cpp             varredura automática de menus
  imu.h/.cpp              acelerômetro/giroscópio MPU-6050 (I2C, task no core 0)
docs/API.md               documentação da API
mib/ESPPROG-MIB.txt       MIB para Zabbix/NMS
```

Sem dependências externas — só o que vem no core Arduino-ESP32 (o I2C usa o `Wire` do
próprio core).
