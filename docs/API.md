# espprog — documentação da API

Emulador do programador de mão da placa **TRIFLEX CONNECT v2.36.33**, acessível por rede.
Tudo que o programador físico mostra e faz está exposto em três interfaces:

| Interface | Porta | Para quê |
|---|---|---|
| Web | TCP/80 | operar à distância, ver a tela, configurar |
| REST/JSON | TCP/80 | automação, scripts, integração |
| SNMP v2c | UDP/161 | monitoramento (Zabbix, LibreNMS, Observium…) |

Endereço de fábrica: **`192.168.1.21/24`**, gateway `192.168.1.1`, hostname `espprog`
(também responde em `http://espprog.local`).

---

## 1. Convenções

- Todas as respostas JSON usam `Content-Type: application/json` e `Cache-Control: no-store`.
- Campos `*_age_ms` / `*_age`: **há quantos milissegundos** aquele valor foi lido da placa.
  `-1` (REST) ou `4294967295` (SNMP) significa **nunca lido desde o boot**.
- As linhas do LCD vêm em UTF-8, já convertidas do charset da placa. Bytes ainda não
  mapeados aparecem como `·` — o campo `raw` traz sempre os 32 bytes originais em hexadecimal.
- Se a autenticação estiver ligada (aba *Avançado*), **todos** os endpoints exigem
  HTTP Basic: `curl -u admin:senha …`.
- Erros retornam `{"ok":false,"error":"..."}` com status 400 ou 404.

---

## 2. Leitura da tela

### `GET /api/screen`

Estado do LCD 2×16.

```bash
curl -s http://192.168.1.21/api/screen
```

```json
{
  "l1": "FEC:0 ABR:0     ",
  "l2": "BOT:0 FOT:0 RF:0",
  "raw": "4645433A30204142523A3020202020204254...",
  "id": 1,
  "seq": 12,
  "age_ms": 4210,
  "rx_age_ms": 4210
}
```

| Campo | Tipo | Significado |
|---|---|---|
| `l1`, `l2` | string | linhas 1 e 2, 16 caracteres, em UTF-8 |
| `raw` | string | os 32 bytes crus em hexadecimal maiúsculo (64 caracteres) |
| `id` | int | tela reconhecida: `0` desconhecida · `1` status · `2` ciclos · `3` idioma · `4` splash |
| `seq` | int | contador que incrementa a cada **mudança** de conteúdo — use para detectar troca de tela sem comparar strings |
| `age_ms` | int | ms desde a última *mudança* de conteúdo |
| `rx_age_ms` | int | ms desde o último comando de tela recebido (a placa reescreve mesmo sem mudar) |

### `GET /api/screen.txt`

As duas linhas em texto puro, uma por linha. Feito para script de shell.

```bash
$ curl -s http://192.168.1.21/api/screen.txt
FEC:0 ABR:0
BOT:0 FOT:0 RF:0
```

---

## 3. Valores extraídos

### `GET /api/values`

Os campos que o firmware sabe interpretar das telas conhecidas.

```json
{
  "fec": 0, "abr": 0, "bot": 0, "fot": 0, "rf": 0, "status_age": 4210,
  "cycles": 382400, "cycles_age": -1,
  "rpm": 222, "rpm_age": -1,
  "enc": 0, "enc_age": -1
}
```

| Campo | Origem | Observação |
|---|---|---|
| `fec` `abr` `bot` `fot` `rf` | tela de status | `0`/`1`; **`-1` = nunca visto** |
| `status_age` | — | ms desde a última leitura da tela de status |
| `cycles` | tela `Ciclos: 00382400` | contador de ciclos da placa |
| `rpm` | tela `Enc+0000  222rpm` | rotação |
| `enc` | mesma tela | ângulo do encoder, **com sinal** |

> **Importante.** A placa só manda a tela que está exibindo. `cycles`, `rpm` e `enc` ficam
> com `age = -1` até alguém (ou a varredura automática) navegar até a tela de ciclos.
> Os valores de status ficam atualizados o tempo todo, porque a tela de status é a padrão.

---

## 4. Estado completo

### `GET /api/state`

Tudo de uma vez — é o que a própria interface web consome, a cada 250 ms.

```json
{
  "fw": "1.0.0", "up": 812345, "heap": 214512,
  "eth":   {"link":true,"dhcp":false,"ip":"192.168.1.21","mask":"255.255.255.0",
            "gw":"192.168.1.1","dns":"192.168.1.1","mac":"AA:BB:CC:DD:EE:FF",
            "speed":100,"fd":true},
  "prog":  {"link":true,"tx":true,"polls":34210,"frames":34255,"screens":41,
            "beeps":6,"lights":1,"unknown":0,"bad":0,"badchk":0,"resp":34255,
            "period_ms":23,"age_ms":11,"keya":"B4","keyb":"B4","scanning":false},
  "screen":{"l1":"...","l2":"...","raw":"...","id":1,"seq":12,"age_ms":4210},
  "val":   { ... igual a /api/values ... },
  "snmp":  {"en":true,"port":161,"rx":88,"tx":88,"bad":0,"auth_fail":0,"sets":0}
}
```

Destaques de `prog`:

| Campo | Significado |
|---|---|
| `link` | `true` se chegou quadro válido da placa nos últimos 2 s |
| `tx` | `false` = modo escuta, o firmware não responde à placa |
| `period_ms` | período médio do poll (esperado ≈ 23–24 ms) |
| `bad` / `badchk` | quadros descartados por forma inválida / checksum — devem ficar em 0 |
| `keya` / `keyb` | bytes de tecla que estão sendo enviados agora (`B4 B4` = repouso) |
| `scanning` | varredura automática em andamento |

---

## 5. Teclas

O programador tem quatro teclas: **−**, **+**, **←**, **→**, em dois canais independentes
(`−`/`+` num canal, `←`/`→` no outro). Combinações entre canais são naturais.

E **`−`+`+` também existe**: as duas no mesmo canal produzem um terceiro valor na escada
resistiva (`0x48`), capturado do programador real. Só `←`+`→` não existe.

### `GET /api/key` · `POST /api/key`

| Parâmetro | Valores | Padrão |
|---|---|---|
| `k` | `minus`, `plus`, `left`, `right` — ou os atalhos `-`, `+`, `<`, `>`, ou `menos`, `mais`, `esq`, `dir`. Duas teclas separadas por vírgula para combinação. | — |
| `action` | `tap`, `down`, `up` | `tap` |

**`tap`** — aperta e solta sozinha, mantendo a tecla ativa pelos polls configurados
(padrão 3, ≈ 70 ms). É o que você quer em 99% dos casos.

```bash
curl "http://192.168.1.21/api/key?k=plus"
curl "http://192.168.1.21/api/key?k=left"
curl "http://192.168.1.21/api/key?k=plus,left"     # combinação entre canais
```

**`down` / `up`** — segura e solta explicitamente, para repetição contínua (é o que a
interface web usa enquanto o botão está pressionado).

```bash
curl "http://192.168.1.21/api/key?k=plus&action=down"
sleep 1
curl "http://192.168.1.21/api/key?action=up"
```

Uma tecla segurada é **solta automaticamente após 3 s** caso o cliente suma sem mandar
`up`. E mesmo num `up` imediato, o firmware garante o mínimo de polls configurado — a
placa não perde o toque.

> **Para segurar por mais de 3 s, reenvie `action=down` a cada segundo.** O watchdog
> existe para que uma tecla não fique presa numa cancela em operação se o cliente travar
> ou a rede cair; renovar o prazo é responsabilidade de quem está segurando. A interface
> web faz isso sozinha.

### `+` e `−` simultâneos

Essa combinação **existe** e usa um terceiro valor de canal (`0x48`), capturado do
programador original — não estava no levantamento inicial. Use `k=plusminus`:

```bash
curl "http://192.168.1.21/api/key?k=plusminus"
curl "http://192.168.1.21/api/key?k=plus,minus"    # equivalente
```

Aceita também `+-` e `maismenos`. Na interface web é o botão central do teclado.

Resposta:

```json
{"ok":true,"action":"tap","a":"plus","b":"none","bytes":"A8 B4"}
```

Tecla inválida → `400` com `{"ok":false,"error":"tecla invalida"}`.

### Exemplo: navegar até a tela de ciclos e ler

```bash
H=http://192.168.1.21
curl -s "$H/api/key?k=plus" >/dev/null
sleep 1
curl -s "$H/api/screen.txt"
curl -s "$H/api/key?k=minus" >/dev/null      # volta
```

---

## 6. Configuração

### `GET /api/config`

Devolve todos os parâmetros. `authPass` volta sempre vazio (nunca é exposto).

```json
{
  "entRoot": "1.3.6.1.4.1.63050",
  "dhcp": false, "ip": "192.168.1.21", "mask": "255.255.255.0",
  "gw": "192.168.1.1", "dns1": "192.168.1.1", "dns2": "8.8.8.8",
  "hostname": "espprog",
  "snmpEnabled": true, "snmpPort": 161,
  "roCommunity": "public", "rwCommunity": "",
  "sysContact": "", "sysLocation": "",
  "txEnabled": true, "turnaroundUs": 3000, "tailGapUs": 2100,
  "keyHoldPolls": 3, "beepUnitMs": 10, "buzzer": true, "led": true,
  "scanEnabled": false, "scanIntervalSec": 60, "scanIdleSec": 30,
  "scanSeq": "+,W1500,-,W1500",
  "authEnabled": false, "authUser": "admin", "authPass": ""
}
```

### `POST /api/config`

Aceita `application/x-www-form-urlencoded` (ou os mesmos nomes na query string).
**Campos omitidos permanecem como estão** — dá para alterar um parâmetro só.

```bash
# IP fixo
curl -X POST http://192.168.1.21/api/config \
  -d 'dhcp=0&ip=10.0.0.21&mask=255.255.255.0&gw=10.0.0.1&dns1=10.0.0.1'

# voltar para DHCP
curl -X POST http://192.168.1.21/api/config -d 'dhcp=1'

# SNMP
curl -X POST http://192.168.1.21/api/config \
  -d 'roCommunity=cancela&rwCommunity=cancela-rw&sysLocation=Portaria 2'

# modo escuta (não transmite nada para a placa)
curl -X POST http://192.168.1.21/api/config -d 'txEnabled=0'
```

| Campo | Tipo | Faixa | Efeito |
|---|---|---|---|
| `dhcp` | bool | `0`/`1` | **exige reinício** |
| `ip` `mask` `gw` `dns1` `dns2` | IPv4 | — | **exige reinício** |
| `hostname` | string ≤31 | — | nome mDNS e `sysName`; **exige reinício** |
| `snmpEnabled` | bool | — | aplicado na hora (reabre o socket) |
| `snmpPort` | int | 1–65535 | aplicado na hora |
| `roCommunity` | string ≤32 | — | community de leitura |
| `rwCommunity` | string ≤32 | — | community de escrita; **vazio desabilita SET** |
| `sysContact` `sysLocation` | string ≤63 | — | grupo `system` do SNMP |
| `txEnabled` | bool | — | `0` = modo escuta |
| `turnaroundUs` | int | 500–20000 | atraso até responder à placa (medido: 2800–3300) |
| `tailGapUs` | int | 0–20000 | intervalo até o byte `0x00` final |
| `keyHoldPolls` | int | 1–50 | por quantos polls a tecla fica ativa |
| `beepUnitMs` | int | 1–100 | unidade de tempo do comando de beep |
| `buzzer` `led` | bool | — | saídas locais IO14 / IO4 |
| `scanEnabled` | bool | — | varredura automática de menus |
| `scanIntervalSec` | int | 5–3600 | período entre varreduras |
| `scanIdleSec` | int | 1–3600 | só varre após N s sem tecla humana |
| `scanSeq` | string ≤63 | — | sequência (ver §7) |
| `authEnabled` | bool | — | HTTP Basic |
| `authUser` | string ≤23 | — | usuário |
| `authPass` | string ≤31 | — | senha; **string vazia mantém a atual** |

Booleanos aceitam `1`/`0`, `true`/`false`, `on`/`off`.

Resposta: `{"ok":true}`.

### `POST /api/reboot` · `POST /api/factory`

`reboot` reinicia em ~400 ms. `factory` restaura os padrões (**volta para IP fixo
192.168.1.21**) e reinicia.

---

## 7. Varredura automática de menus

Ciclos, RPM e encoder só chegam quando a placa está exibindo a tela correspondente.
A varredura resolve isso apertando teclas sozinha quando ninguém está usando a interface.

`scanSeq` é uma lista separada por vírgula:

| Token | Ação |
|---|---|
| `+` `-` `<` `>` | aperta a tecla (também aceita `plus`, `minus`, `left`, `right`) |
| `W<ms>` | espera N milissegundos (ex.: `W1500`) |

Após cada tecla há uma espera implícita de 250 ms. Padrão: `+,W1500,-,W1500` — vai
para a tela de ciclos, espera 1,5 s (tempo de ler e parsear), volta e espera de novo.

### `POST /api/scan`

Dispara a varredura imediatamente, mesmo com `scanEnabled=0`. Útil para "atualizar
ciclos agora" sob demanda, sem deixar a varredura ligada o tempo todo:

```bash
curl -X POST http://192.168.1.21/api/scan
sleep 3
curl -s http://192.168.1.21/api/values
```

> ⚠️ **Cuidado.** O mapa completo de menus da placa não foi levantado. Uma sequência
> errada pode entrar em telas de configuração e alterar parâmetros da cancela. Valide a
> sequência na mão antes de habilitar a varredura, e prefira o disparo sob demanda
> (`POST /api/scan`) ao modo automático em equipamento em produção.

---

## 8. SNMP v2c

Raiz privada: **`1.3.6.1.4.1.63050`** (`ESPPROG-MIB`, arquivo em `mib/ESPPROG-MIB.txt`).

> Esse número é um **placeholder**. Se sua empresa tem um PEN da IANA, troque
> `-DESPPROG_ENTERPRISE=` no `platformio.ini` e o `enterprises` da MIB.

Suporta `GetRequest`, `GetNextRequest`, `GetBulkRequest` e `SetRequest`, além do grupo
`system` padrão (RFC 1213) — logo `snmpwalk` a partir da raiz funciona, e ferramentas de
descoberta identificam o dispositivo.

```bash
snmpwalk  -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050
snmpget   -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050.4.3.0    # BOT
snmpbulkwalk -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050.4     # todo o status
```

### 8.1 Sistema — `.63050.1`

| OID | Nome | Tipo | Descrição |
|---|---|---|---|
| `.1.1.0` | `fwVersion` | String | versão do firmware |
| `.1.2.0` | `uptimeSec` | Gauge32 | segundos desde o boot |
| `.1.3.0` | `macAddress` | String | MAC da interface Ethernet |
| `.1.4.0` | `ipAddress` | IpAddress | IP atual |
| `.1.5.0` | `ethLink` | Integer | `1` = enlace Ethernet ativo |
| `.1.6.0` | `ethSpeed` | Gauge32 | Mbps negociados |
| `.1.7.0` | `freeHeap` | Gauge32 | heap livre em bytes |

### 8.2 Barramento PROG — `.63050.2`

| OID | Nome | Tipo | Descrição |
|---|---|---|---|
| `.2.1.0` | `progLink` | Integer | **`1` = a placa está falando**; alarme principal |
| `.2.2.0` | `lastFrameAge` | Gauge32 | ms desde o último quadro |
| `.2.3.0` | `pollPeriod` | Gauge32 | período médio do poll em ms (≈ 23) |
| `.2.4.0` | `framesTotal` | Counter32 | quadros válidos recebidos |
| `.2.5.0` | `pollsTotal` | Counter32 | polls recebidos |
| `.2.6.0` | `screensTotal` | Counter32 | comandos de tela recebidos |
| `.2.7.0` | `beepsTotal` | Counter32 | beeps recebidos |
| `.2.8.0` | `badFrames` | Counter32 | descartados por forma inválida |
| `.2.9.0` | `badChecksum` | Counter32 | descartados por checksum |
| `.2.10.0` | `responses` | Counter32 | respostas enviadas à placa |
| `.2.11.0` | `txEnabled` | Integer | `0` = modo escuta |

### 8.3 Tela — `.63050.3`

| OID | Nome | Tipo | Descrição |
|---|---|---|---|
| `.3.1.0` | `screenLine1` | String | linha 1 em UTF-8 |
| `.3.2.0` | `screenLine2` | String | linha 2 em UTF-8 |
| `.3.3.0` | `screenRawHex` | String | os 32 bytes crus em hexadecimal |
| `.3.4.0` | `screenId` | Integer | `0` desconhecida `1` status `2` ciclos `3` idioma `4` splash |
| `.3.5.0` | `screenAge` | Gauge32 | ms desde a última mudança |
| `.3.6.0` | `screenSeq` | Counter32 | contador de mudanças de tela |

### 8.4 Status da cancela — `.63050.4`

| OID | Nome | Tipo | Descrição |
|---|---|---|---|
| `.4.1.0` | `fec` | Integer | fechada — `0`/`1`, `-1` nunca visto |
| `.4.2.0` | `abr` | Integer | aberta |
| `.4.3.0` | `bot` | Integer | botoeira |
| `.4.4.0` | `fot` | Integer | fotocélula |
| `.4.5.0` | `rf` | Integer | rádio-frequência |
| `.4.6.0` | `statusAge` | Gauge32 | ms desde a última leitura da tela de status |

### 8.5 Contadores da placa — `.63050.5`

| OID | Nome | Tipo | Descrição |
|---|---|---|---|
| `.5.1.0` | `cycles` | Gauge32 | contador de ciclos |
| `.5.2.0` | `cyclesAge` | Gauge32 | ms desde a leitura |
| `.5.3.0` | `rpm` | Gauge32 | rotação |
| `.5.4.0` | `rpmAge` | Gauge32 | ms desde a leitura |
| `.5.5.0` | `encoder` | Integer | ângulo do encoder, com sinal |
| `.5.6.0` | `encoderAge` | Gauge32 | ms desde a leitura |

**Sempre cheque o `*Age`** antes de confiar nesses três: `4294967295` significa que a
tela de ciclos nunca foi exibida desde o boot. Um valor grande significa dado velho.

### 8.6 Controle — `.63050.6`

| OID | Nome | Tipo | Acesso | Descrição |
|---|---|---|---|---|
| `.6.1.0` | `pressKey` | Integer | **read-write** | `1` menos · `2` mais · `3` esquerda · `4` direita |

Escrever aqui dá um toque na tecla, igual a `GET /api/key?k=…`. Exige `rwCommunity`
configurada e não vazia — sem isso o firmware nem responde ao `SetRequest`.

```bash
snmpset -v2c -c cancela-rw 192.168.1.21 1.3.6.1.4.1.63050.6.1.0 i 2
```

### 8.7 Segurança

SNMP v2c manda a community em claro. O agente:

- responde a `GET`/`GETNEXT`/`GETBULK` com a community de leitura **ou** de escrita;
- responde a `SET` **apenas** com a community de escrita, e só se ela não estiver vazia;
- **descarta silenciosamente** (sem responder) qualquer pacote com community errada,
  contabilizando em `auth_fail` — não dá pista para quem estiver tentando adivinhar.

Restrinja por firewall quem alcança a UDP/161 se o `rwCommunity` estiver ligado: quem
souber a community consegue apertar teclas na cancela.

---

## 9. Integração com Zabbix

Itens SNMP agent, `{$SNMP_COMMUNITY}` no host:

| Item | OID | Tipo | Observação |
|---|---|---|---|
| Link PROG | `1.3.6.1.4.1.63050.2.1.0` | Numérico | trigger em `last()=0` |
| Idade do último quadro | `1.3.6.1.4.1.63050.2.2.0` | Numérico | ms |
| Cancela aberta | `1.3.6.1.4.1.63050.4.2.0` | Numérico | |
| Fotocélula | `1.3.6.1.4.1.63050.4.4.0` | Numérico | |
| Ciclos | `1.3.6.1.4.1.63050.5.1.0` | Numérico | use *Change per second* para ciclos/hora |
| Idade dos ciclos | `1.3.6.1.4.1.63050.5.2.0` | Numérico | trigger se > 1 h → varredura parou |
| Tela (linha 1) | `1.3.6.1.4.1.63050.3.1.0` | Texto | |
| Tela (linha 2) | `1.3.6.1.4.1.63050.3.2.0` | Texto | |

Trigger sugerida para o link do barramento:

```
last(/espprog/1.3.6.1.4.1.63050.2.1.0)=0
```

Se preferir HTTP agent em vez de SNMP, aponte para `http://192.168.1.21/api/values` e
use pré-processamento JSONPath (`$.cycles`, `$.bot`, …).

---

## 9.1 Endpoints de diagnóstico

Ferramentas de bancada, criadas durante o bring-up. Detalhes de uso em
[`COMO-FUNCIONA.md`](COMO-FUNCIONA.md) §8.

| Endpoint | O que faz |
|---|---|
| `GET /api/rxdiag?ms=300&pull=0..3` | Estado elétrico da linha de RX. `pull=3` liga o pull-down interno: se a linha continuar alta, existe pull-up **externo**. |
| `GET /api/edges?timeout=3000` | Tempos entre bordas em µs. Mede o baud real (1 bit ≈ 104 µs a 9600). |
| `GET /api/frames` | Últimos 16 quadros em hexadecimal, com intervalo entre eles. Inclui quadros de outros programadores (`TAIL 0x81`). |
| `GET /api/txraw?hex=7E0801…` | Transmite bytes crus na linha. |
| `GET /api/txlow?ms=8000` | Segura o TX em nível baixo, para medir o `D3` com multímetro. |
| `GET /api/keyraw?a=48&b=B4&hold=800` | Envia bytes de tecla arbitrários, em hexadecimal. |

> Para avaliar **qualidade de sinal**, prefira `bad` e `badchk` de `/api/state`. A sonda
> de bordas roda em prioridade baixa e é preemptada pela task do barramento, o que
> produz intervalos falsos muito curtos.

---

## 10. Códigos de erro HTTP

| Status | Quando |
|---|---|
| `200` | ok |
| `400` | parâmetro inválido (ex.: `k` não é uma tecla conhecida) |
| `401` | autenticação exigida e ausente/errada |
| `404` | rota inexistente |

---

## 11. Referência rápida

```bash
H=http://192.168.1.21

curl -s  $H/api/screen.txt                     # tela em texto
curl -s  $H/api/screen                         # tela em JSON + hex
curl -s  $H/api/values                         # valores extraídos
curl -s  $H/api/state                          # tudo
curl -s  "$H/api/key?k=plus"                   # toque
curl -s  "$H/api/key?k=plusminus"              # + e - juntos
curl -s  "$H/api/key?k=left&action=down"       # segura
curl -s  "$H/api/key?action=up"                # solta
curl -s  $H/api/config                         # configuração
curl -sX POST $H/api/config -d 'dhcp=1'        # muda config
curl -sX POST $H/api/scan                      # varre menus agora
curl -sX POST $H/api/reboot                    # reinicia

snmpwalk -v2c -c public 192.168.1.21 1.3.6.1.4.1.63050
snmpset  -v2c -c <rw>   192.168.1.21 1.3.6.1.4.1.63050.6.1.0 i 2
```
