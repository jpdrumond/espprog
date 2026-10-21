#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>espprog</title>
<style>
:root{--bg:#12141a;--panel:#1a1d26;--line:#2a2f3d;--fg:#e6e9f0;--dim:#8b93a7;
--acc:#5aa9e6;--ok:#3ddc84;--bad:#ff5f56;--warn:#ffbd2e;
--lcd:#9fd13a;--lcdink:#16240a;--lcdoff:#7fae2e}
*{box-sizing:border-box}
[hidden]{display:none!important}  /* .pill define display e anularia o atributo */
body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.5 ui-sans-serif,system-ui,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;gap:14px;flex-wrap:wrap;
padding:12px 18px;background:var(--panel);border-bottom:1px solid var(--line)}
h1{font-size:16px;margin:0;letter-spacing:.5px}
h1 small{color:var(--dim);font-weight:400;font-size:12px;margin-left:8px}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--dim);
background:#0d0f14;border:1px solid var(--line);border-radius:999px;padding:3px 10px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--bad)}
.dot.on{background:var(--ok)}.dot.warn{background:var(--warn)}
nav{display:flex;gap:2px;padding:0 12px;background:var(--panel);border-bottom:1px solid var(--line);
overflow-x:auto}
nav button{background:none;border:0;border-bottom:2px solid transparent;color:var(--dim);
padding:10px 14px;cursor:pointer;font:inherit;white-space:nowrap}
nav button.sel{color:var(--fg);border-bottom-color:var(--acc)}
main{max-width:940px;margin:0 auto;padding:20px 16px 60px}
section{display:none}section.sel{display:block}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px;
padding:18px;margin-bottom:16px}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:1px;color:var(--dim);
margin:0 0 14px}
/* ---- LCD ---- */
.lcdwrap{display:flex;flex-direction:column;align-items:center;gap:22px}
.lcd{background:var(--lcd);padding:14px 16px;border-radius:6px;
box-shadow:inset 0 0 0 2px #6d9626,0 6px 18px #0006;user-select:text}
.row{display:flex}
.cell{width:17px;height:26px;display:flex;align-items:center;justify-content:center;
font:600 17px/1 ui-monospace,"Cascadia Mono",Consolas,monospace;color:var(--lcdink);
background:linear-gradient(var(--lcdoff),var(--lcdoff)) center/1px 0 no-repeat}
.lcd.hex .cell{font-size:9px;width:22px;letter-spacing:0}
/* ---- teclado ---- */
.pad{display:grid;grid-template-columns:repeat(3,64px);grid-template-rows:repeat(3,64px);
gap:8px}
.pad button{background:#232733;border:1px solid var(--line);border-radius:10px;color:var(--fg);
font-size:22px;cursor:pointer;touch-action:none;transition:.06s}
.pad button:active,.pad button.dn{background:var(--acc);color:#08121c;transform:translateY(1px)}
.pad button.lat{box-shadow:inset 0 0 0 2px var(--warn)}
.pad .p{grid-area:1/2}.pad .l{grid-area:2/1}.pad .r{grid-area:2/3}.pad .m{grid-area:3/2}
.pad .c{grid-area:2/2;font-size:15px}
.hint{color:var(--dim);font-size:12px;text-align:center}
/* ---- grade de valores ---- */
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:10px}
.kv{background:#0d0f14;border:1px solid var(--line);border-radius:8px;padding:10px 12px}
.kv b{display:block;font-size:11px;color:var(--dim);font-weight:500;text-transform:uppercase;
letter-spacing:.6px}
.kv span{font:600 19px/1.4 ui-monospace,Consolas,monospace}
.kv.stale span{color:var(--dim)}
.kv em{font-style:normal;font-size:10px;color:var(--dim);display:block}
/* ---- formularios ---- */
label{display:block;margin-bottom:12px;font-size:12px;color:var(--dim)}
label span{display:block;margin-bottom:4px}
input[type=text],input[type=number],input[type=password],select{width:100%;background:#0d0f14;
border:1px solid var(--line);border-radius:6px;color:var(--fg);padding:8px 10px;
font:inherit}
input:disabled{opacity:.4}
.two{display:grid;grid-template-columns:1fr 1fr;gap:0 14px}
.chk{display:flex;align-items:center;gap:8px;margin-bottom:14px;color:var(--fg);font-size:14px}
.chk input{width:16px;height:16px;accent-color:var(--acc)}
.btn{background:var(--acc);border:0;border-radius:6px;color:#08121c;font:600 14px/1 inherit;
padding:10px 18px;cursor:pointer}
.btn.sec{background:#2a3040;color:var(--fg)}
.btn.danger{background:var(--bad);color:#fff}
.bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:6px}
.msg{font-size:13px;padding:8px 0}
.msg.ok{color:var(--ok)}.msg.err{color:var(--bad)}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:7px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{color:var(--dim);font-weight:500;font-size:11px;text-transform:uppercase;letter-spacing:.6px}
code{font:12px/1.5 ui-monospace,Consolas,monospace;background:#0d0f14;padding:1px 5px;
border-radius:4px;border:1px solid var(--line)}
pre{background:#0d0f14;border:1px solid var(--line);border-radius:8px;padding:12px;
overflow-x:auto;font:12px/1.6 ui-monospace,Consolas,monospace;margin:0 0 14px}
.warnbox{background:#3a2a10;border:1px solid #6b4d18;border-radius:8px;padding:12px 14px;
font-size:13px;margin-bottom:16px}
</style></head><body>

<header>
  <h1>espprog <small id="ver"></small></h1>
  <span class="pill"><i class="dot" id="dProg"></i> barramento PROG <b id="tProg">--</b></span>
  <span class="pill"><i class="dot" id="dEth"></i> <b id="tEth">--</b></span>
  <span class="pill" id="pTx" hidden>modo escuta (nao transmite)</span>
</header>

<nav>
  <button class="sel" data-t="tela">Tela</button>
  <button data-t="sensor">Sensor</button>
  <button data-t="rede">Rede</button>
  <button data-t="snmp">SNMP</button>
  <button data-t="avanc">Avancado</button>
  <button data-t="api">API</button>
</nav>

<main>
<!-- =================== TELA =================== -->
<section id="tela" class="sel">
  <div class="card">
    <div class="lcdwrap">
      <div class="lcd" id="lcd"></div>
      <div class="pad">
        <button class="p" data-k="plus">+</button>
        <button class="l" data-k="left">&#8592;</button>
        <button class="r" data-k="right">&#8594;</button>
        <button class="m" data-k="minus">&#8722;</button>
        <button class="c" data-k="plusminus" title="+ e &#8722; simultaneos"><span style="font-size:15px">+&#8722;</span></button>
      </div>
      <div class="hint">Teclado: setas &#8592; &#8594; e + / &#8722; &nbsp;&middot;&nbsp;
        segure para repetir &nbsp;&middot;&nbsp;
        o botao central manda <b>+ e &#8722; juntos</b> &nbsp;&middot;&nbsp;
        botao direito trava a tecla &nbsp;&middot;&nbsp;
        <a href="#" id="hexToggle" style="color:var(--acc)">ver HEX</a></div>
    </div>
  </div>

  <div class="card">
    <h2>Valores extraidos</h2>
    <div class="grid" id="vals"></div>
    <p class="hint" style="text-align:left;margin:14px 0 0">
      Ciclos, RPM e encoder so aparecem quando a tela correspondente e exibida pela placa.
      Use a varredura automatica (aba Avancado) para atualiza-los sozinho.</p>
  </div>

  <div class="card">
    <h2>Barramento</h2>
    <div class="grid" id="stats"></div>
  </div>
</section>

<!-- =================== SENSOR =================== -->
<section id="sensor">
  <div class="card">
    <h2>Cancela</h2>
    <div class="grid" id="gateVals"></div>
    <div class="bar" style="margin-top:14px">
      <button class="btn" type="button" data-gate="fechada">Gravar posicao FECHADA</button>
      <button class="btn" type="button" data-gate="aberta">Gravar posicao ABERTA</button>
      <button class="btn sec" type="button" id="gateClear">Apagar</button>
      <span class="msg" id="mGate"></span>
    </div>
    <p class="hint" style="text-align:left;margin:10px 0 0">
      Com o sensor ja preso no braco: feche a cancela, espere parar e clique em
      <b>Gravar posicao FECHADA</b>; abra, espere parar e clique em <b>Gravar posicao
      ABERTA</b>. Dai em diante a cancela vale a posicao gravada mais proxima &mdash; no
      meio do curso ela so troca depois de passar da metade. Funciona em qualquer
      orientacao de montagem. Para script: <code>GET /api/gate</code>.</p>
  </div>
  <div class="card">
    <h2>Acelerometro e giroscopio</h2>
    <p class="hint" id="imuInfo" style="text-align:left;margin:0 0 12px">--</p>
    <div class="grid" id="imuVals"></div>
    <div class="bar" style="margin-top:14px">
      <button class="btn sec" type="button" id="imuCal">Calibrar giroscopio</button>
      <span class="msg" id="mImuCal"></span>
    </div>
    <p class="hint" style="text-align:left;margin:10px 0 0">
      A calibracao mede o desvio do giroscopio com o sensor <b>parado</b> por 2 s. Ela roda
      sozinha ao ligar e so conclui quando nada se mexe. Pitch e roll vem da direcao da
      gravidade: montando o modulo com o eixo <b>Y ao longo do braco</b> da cancela, o angulo
      do braco aparece em <b>roll</b> (-180&deg; a 180&deg;).</p>
  </div>
  <div class="card">
    <h2>Configuracao do sensor</h2>
    <form id="fImu">
      <div class="two">
        <label><span>Faixa do acelerometro</span><select name="imuAccelG" id="imuAccelG">
          <option value="2">&plusmn;2 g</option><option value="4">&plusmn;4 g</option>
          <option value="8">&plusmn;8 g</option><option value="16">&plusmn;16 g</option>
          </select></label>
        <label><span>Faixa do giroscopio</span><select name="imuGyroDps" id="imuGyroDps">
          <option value="250">&plusmn;250 &deg;/s</option>
          <option value="500">&plusmn;500 &deg;/s</option>
          <option value="1000">&plusmn;1000 &deg;/s</option>
          <option value="2000">&plusmn;2000 &deg;/s</option></select></label>
        <label><span>Limiar de pancada (mg alem da gravidade)</span>
          <input type="number" name="imuShockMg" id="imuShockMg" min="100" max="16000"></label>
      </div>
      <div class="bar"><button class="btn" type="submit">Salvar</button>
        <span class="msg" id="mImu"></span></div>
      <p class="hint" style="text-align:left;margin-top:10px">Trocar a faixa reprograma
        o sensor e refaz a calibracao. Faixa menor = mais resolucao; faixa maior = pancada
        forte nao satura.</p>
    </form>
  </div>
  <div class="card">
    <h2>Ligacao do GY-521 (MPU-6050)</h2>
    <table><thead><tr><th>GY-521</th><th>WT32-ETH01</th><th>Observacao</th></tr></thead>
    <tbody>
      <tr><td><code>VCC</code></td><td><code>3V3</code></td><td>o modulo consome ~4 mA</td></tr>
      <tr><td><code>GND</code></td><td><code>GND</code></td><td></td></tr>
      <tr><td><code>SCL</code></td><td><code>IO33</code></td><td>clock I2C</td></tr>
      <tr><td><code>SDA</code></td><td><code>IO32</code></td><td>dados I2C</td></tr>
      <tr><td><code>AD0</code></td><td>GND ou solto</td><td>endereco 0x68 (em 3V3: 0x69)</td></tr>
      <tr><td><code>XDA</code> <code>XCL</code> <code>INT</code></td><td>nao ligar</td>
        <td></td></tr>
    </tbody></table>
    <p class="hint" style="text-align:left;margin:10px 0 0">Fios curtos (ate ~50 cm): I2C
      nao foi feito para cabo longo. O GY-521 ja tem os resistores de pull-up.</p>
  </div>
</section>

<!-- =================== REDE =================== -->
<section id="rede">
  <div class="card">
    <h2>Configuracao IP</h2>
    <form id="fNet">
      <div class="chk"><input type="checkbox" id="dhcp" name="dhcp"><label for="dhcp"
        style="margin:0;color:var(--fg)">Obter IP automaticamente (DHCP)</label></div>
      <div class="two">
        <label><span>Endereco IP</span><input type="text" name="ip" id="ip"></label>
        <label><span>Mascara de sub-rede</span><input type="text" name="mask" id="mask"></label>
        <label><span>Gateway</span><input type="text" name="gw" id="gw"></label>
        <label><span>DNS primario</span><input type="text" name="dns1" id="dns1"></label>
        <label><span>DNS secundario</span><input type="text" name="dns2" id="dns2"></label>
        <label><span>Hostname (mDNS)</span><input type="text" name="hostname" id="hostname"
          maxlength="31"></label>
      </div>
      <div class="bar">
        <button class="btn" type="submit">Salvar e reiniciar</button>
        <span class="msg" id="mNet"></span>
      </div>
      <p class="hint" style="text-align:left;margin-top:10px">
        A mudanca de IP so entra em vigor apos o reinicio. Se perder o acesso, reconecte pelo
        novo endereco ou por <code>http://hostname.local</code>.</p>
    </form>
  </div>
  <div class="card">
    <h2>Estado atual</h2>
    <div class="grid" id="netinfo"></div>
  </div>
</section>

<!-- =================== SNMP =================== -->
<section id="snmp">
  <div class="card">
    <h2>Agente SNMP v2c</h2>
    <form id="fSnmp">
      <div class="chk"><input type="checkbox" id="snmpEnabled" name="snmpEnabled"><label
        for="snmpEnabled" style="margin:0;color:var(--fg)">Habilitar agente SNMP</label></div>
      <div class="two">
        <label><span>Porta UDP</span><input type="number" name="snmpPort" id="snmpPort"
          min="1" max="65535"></label>
        <label><span>Community de leitura</span><input type="text" name="roCommunity"
          id="roCommunity" maxlength="32"></label>
        <label><span>Community de escrita (vazio = SET desabilitado)</span>
          <input type="text" name="rwCommunity" id="rwCommunity" maxlength="32"></label>
        <label><span>sysContact</span><input type="text" name="sysContact" id="sysContact"
          maxlength="63"></label>
        <label><span>sysLocation</span><input type="text" name="sysLocation" id="sysLocation"
          maxlength="63"></label>
      </div>
      <div class="bar">
        <button class="btn" type="submit">Salvar</button>
        <span class="msg" id="mSnmp"></span>
      </div>
    </form>
  </div>
  <div class="card">
    <h2>OIDs disponiveis</h2>
    <p class="hint" style="text-align:left;margin:0 0 12px">Raiz privada
      <code id="entRoot"></code>. Todos os escalares terminam em <code>.0</code>.
      Os campos <code>*Age</code> retornam ha quantos milissegundos o valor foi lido
      (<code>4294967295</code> = nunca).</p>
    <table id="oids"><thead><tr><th>OID</th><th>Nome</th><th>Tipo</th><th>Descricao</th>
      </tr></thead><tbody></tbody></table>
  </div>
  <div class="card">
    <h2>Estatisticas</h2>
    <div class="grid" id="snmpstats"></div>
  </div>
</section>

<!-- =================== AVANCADO =================== -->
<section id="avanc">
  <div class="card">
    <h2>Barramento PROG</h2>
    <form id="fAdv">
      <div class="chk"><input type="checkbox" id="txEnabled" name="txEnabled"><label
        for="txEnabled" style="margin:0;color:var(--fg)">Transmitir respostas para a placa</label></div>
      <p class="hint" style="text-align:left;margin:-6px 0 14px">Desmarque para o modo
        escuta: o firmware so recebe e registra, sem colocar nada na linha.</p>
      <div class="two">
        <label><span>Turnaround (us) &mdash; medido: 2800..3300</span>
          <input type="number" name="turnaroundUs" id="turnaroundUs" min="500" max="20000"></label>
        <label><span>Intervalo ate o byte 0x00 (us)</span>
          <input type="number" name="tailGapUs" id="tailGapUs" min="0" max="20000"></label>
        <label><span>Polls que a tecla fica ativa</span>
          <input type="number" name="keyHoldPolls" id="keyHoldPolls" min="1" max="50"></label>
        <label><span>Unidade de tempo do beep (ms)</span>
          <input type="number" name="beepUnitMs" id="beepUnitMs" min="1" max="100"></label>
      </div>
      <div class="chk"><input type="checkbox" id="buzzer" name="buzzer"><label for="buzzer"
        style="margin:0;color:var(--fg)">Buzzer local (IO14)</label></div>
      <div class="chk"><input type="checkbox" id="led" name="led"><label for="led"
        style="margin:0;color:var(--fg)">LED de atividade (IO4)</label></div>
      <div class="bar"><button class="btn" type="submit">Salvar</button>
        <span class="msg" id="mAdv"></span></div>
    </form>
  </div>

  <div class="card">
    <h2>Varredura automatica de menus</h2>
    <div class="warnbox"><b>Cuidado.</b> A varredura aperta teclas sozinha para chegar
      as telas de ciclos/RPM/encoder e voltar. O mapa de menus da placa nao foi levantado
      por completo &mdash; uma sequencia errada pode entrar em telas de configuracao.
      Valide a sequencia na mao antes de ligar isto, e mantenha o equipamento supervisionado.</div>
    <form id="fScan">
      <div class="chk"><input type="checkbox" id="scanEnabled" name="scanEnabled"><label
        for="scanEnabled" style="margin:0;color:var(--fg)">Habilitar varredura</label></div>
      <div class="two">
        <label><span>Intervalo entre varreduras (s)</span>
          <input type="number" name="scanIntervalSec" id="scanIntervalSec" min="5"
            max="3600"></label>
        <label><span>So varrer apos N s sem tecla humana</span>
          <input type="number" name="scanIdleSec" id="scanIdleSec" min="1" max="3600"></label>
      </div>
      <label><span>Sequencia &mdash; teclas <code>+ - &lt; &gt;</code> e esperas
        <code>W&lt;ms&gt;</code>, separadas por virgula</span>
        <input type="text" name="scanSeq" id="scanSeq" maxlength="63"></label>
      <div class="bar"><button class="btn" type="submit">Salvar</button>
        <button class="btn sec" type="button" id="scanNow">Executar agora</button>
        <span class="msg" id="mScan"></span></div>
    </form>
  </div>

  <div class="card">
    <h2>Acesso a interface</h2>
    <form id="fAuth">
      <div class="chk"><input type="checkbox" id="authEnabled" name="authEnabled"><label
        for="authEnabled" style="margin:0;color:var(--fg)">Exigir usuario e senha
        (HTTP Basic)</label></div>
      <div class="two">
        <label><span>Usuario</span><input type="text" name="authUser" id="authUser"
          maxlength="23"></label>
        <label><span>Senha</span><input type="password" name="authPass" id="authPass"
          maxlength="31"></label>
      </div>
      <div class="bar"><button class="btn" type="submit">Salvar</button>
        <span class="msg" id="mAuth"></span></div>
    </form>
  </div>

  <div class="card">
    <h2>Sistema</h2>
    <div class="bar">
      <button class="btn sec" id="bReboot">Reiniciar</button>
      <button class="btn danger" id="bReset">Restaurar padroes de fabrica</button>
      <span class="msg" id="mSys"></span>
    </div>
  </div>
</section>

<!-- =================== API =================== -->
<section id="api">
  <div class="card">
    <h2>Leitura da tela</h2>
    <table><tbody>
      <tr><td><code>GET /api/screen</code></td><td>JSON com as duas linhas, os 32 bytes crus
        em hexadecimal, o id da tela e a idade em ms.</td></tr>
      <tr><td><code>GET /api/screen.txt</code></td><td>As duas linhas em texto puro, uma por
        linha &mdash; pronto para <code>curl</code> em script.</td></tr>
      <tr><td><code>GET /api/state</code></td><td>Tudo: tela, valores extraidos, estatisticas
        do barramento, sensor, rede e SNMP. E o que a propria pagina consome.</td></tr>
      <tr><td><code>GET /api/values</code></td><td>So os valores extraidos
        (FEC/ABR/BOT/FOT/RF, ciclos, rpm, encoder) com a idade de cada um.</td></tr>
    </tbody></table>
    <pre id="exScreen"></pre>
  </div>
  <div class="card">
    <h2>Teclas</h2>
    <table><tbody>
      <tr><td><code>GET|POST /api/key?k=plus</code></td><td>Toque: segura pelos polls
        configurados e solta sozinha. Aceita <code>plus</code>, <code>minus</code>,
        <code>left</code>, <code>right</code> (ou <code>+ - &lt; &gt;</code>).</td></tr>
      <tr><td><code>?k=plus&amp;action=down</code></td><td>Segura a tecla ate o
        <code>up</code> (ou 3 s de watchdog, caso o cliente suma).</td></tr>
      <tr><td><code>?action=up</code></td><td>Solta, respeitando o minimo de polls.</td></tr>
      <tr><td><code>?k=plus,left</code></td><td>Combinacao entre os dois canais. So valem
        pares de canais diferentes (<code>+</code>/<code>&minus;</code> com
        <code>&lt;</code>/<code>&gt;</code>).</td></tr>
    </tbody></table>
    <pre id="exKey"></pre>
  </div>
  <div class="card">
    <h2>Acelerometro e giroscopio</h2>
    <table><tbody>
      <tr><td><code>GET /api/imu</code></td><td>Leitura atual: aceleracao (g), rotacao
        (&deg;/s), pitch/roll (&deg;), temperatura do chip, vibracao, pancadas e movimento.</td></tr>
      <tr><td><code>POST /api/imu/calibrate</code></td><td>Mede de novo o desvio do
        giroscopio. Deixe o sensor parado; acompanhe por <code>calibrating</code>.</td></tr>
      <tr><td><code>GET /api/gate</code></td><td>So o estado da cancela, em texto puro:
        <code>aberta</code> ou <code>fechada</code> (<code>desconhecido</code> sem sensor
        ou sem as posicoes gravadas).</td></tr>
      <tr><td><code>POST /api/gate/set?pos=fechada</code></td><td>Grava a posicao atual
        como referencia (<code>fechada</code> ou <code>aberta</code>). Cancela parada.</td></tr>
      <tr><td><code>POST /api/gate/clear</code></td><td>Apaga as duas referencias.</td></tr>
    </tbody></table>
    <pre id="exImu"></pre>
  </div>
  <div class="card">
    <h2>Configuracao</h2>
    <table><tbody>
      <tr><td><code>GET /api/config</code></td><td>Devolve todas as configuracoes.</td></tr>
      <tr><td><code>POST /api/config</code></td><td>Grava. Aceita os mesmos nomes de campo
        do GET, em <code>application/x-www-form-urlencoded</code> ou na query string.
        Campos omitidos ficam como estao.</td></tr>
      <tr><td><code>POST /api/reboot</code></td><td>Reinicia o modulo.</td></tr>
      <tr><td><code>POST /api/scan</code></td><td>Dispara a varredura de menus na hora.</td></tr>
    </tbody></table>
    <pre id="exCfg"></pre>
  </div>
  <div class="card">
    <h2>SNMP</h2>
    <pre id="exSnmp"></pre>
  </div>
</section>
</main>

<script>
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
let CFG={}, ENT="", hexMode=false, busy=false;

/* ---------- navegacao ---------- */
$$("nav button").forEach(b=>b.onclick=()=>{
  $$("nav button").forEach(x=>x.classList.toggle("sel",x===b));
  $$("main section").forEach(s=>s.classList.toggle("sel",s.id===b.dataset.t));
});

/* ---------- LCD ---------- */
const lcd=$("#lcd");
for(let r=0;r<2;r++){const d=document.createElement("div");d.className="row";
  for(let c=0;c<16;c++){const s=document.createElement("div");s.className="cell";
    s.id="c"+(r*16+c);d.appendChild(s);}lcd.appendChild(d);}
$("#hexToggle").onclick=e=>{e.preventDefault();hexMode=!hexMode;
  lcd.classList.toggle("hex",hexMode);
  e.target.textContent=hexMode?"ver texto":"ver HEX";};

function drawLcd(sc){
  const t=[...sc.l1.padEnd(16," "),...sc.l2.padEnd(16," ")];
  for(let i=0;i<32;i++){
    const el=document.getElementById("c"+i);
    el.textContent=hexMode?sc.raw.substr(i*2,2):t[i];
  }
}

/* ---------- teclas ---------- */
/* Combinacoes so existem ENTRE canais: (+ ou -) com (< ou >). O firmware aceita
   "plus,left" e monta os dois bytes. Aqui mantemos um conjunto unico das teclas
   pressionadas, vindas do teclado, do toque e da trava do mouse, e enviamos
   sempre o estado completo -- so assim duas teclas chegam juntas. */
const held = new Set();     /* pressionadas por teclado/ponteiro */
const latch = new Set();    /* travadas com o botao direito */

/* O firmware solta a tecla sozinho apos 3 s sem noticias, para nao deixar uma
   tecla presa numa cancela de verdade se o navegador travar. Enquanto o usuario
   realmente segura, renovamos esse prazo a cada segundo. */
let keepAlive=null;
async function sendKeys(){
  const all=[...new Set([...held,...latch])];
  try{
    if(!all.length) await fetch("/api/key?action=up");
    else await fetch("/api/key?k="+encodeURIComponent(all.join(","))+"&action=down");
  }catch(e){}
  if(all.length && !keepAlive){
    keepAlive=setInterval(()=>{
      const cur=[...new Set([...held,...latch])];
      if(!cur.length){clearInterval(keepAlive);keepAlive=null;return;}
      fetch("/api/key?k="+encodeURIComponent(cur.join(","))+"&action=down").catch(()=>{});
    },1000);
  }else if(!all.length && keepAlive){
    clearInterval(keepAlive); keepAlive=null;
  }
  $$(".pad button").forEach(b=>{
    const on=all.includes(b.dataset.k);
    b.classList.toggle("dn",on);
    b.classList.toggle("lat",latch.has(b.dataset.k));
  });
}
function press(k){ if(!held.has(k)){ held.add(k); sendKeys(); } }
function release(k){ if(held.delete(k)) sendKeys(); }

$$(".pad button").forEach(b=>{
  const k=b.dataset.k;
  b.addEventListener("pointerdown",e=>{
    if(e.button===2)return;                 /* direito e trava, tratado abaixo */
    e.preventDefault(); b.setPointerCapture(e.pointerId); press(k);
  });
  const up=()=>release(k);
  b.addEventListener("pointerup",up);
  b.addEventListener("pointercancel",up);
  /* botao direito trava a tecla: permite combinar usando so o mouse */
  b.addEventListener("contextmenu",e=>{
    e.preventDefault();
    if(latch.has(k)) latch.delete(k); else latch.add(k);
    sendKeys();
  });
});

const KMAP={ArrowLeft:"left",ArrowRight:"right",ArrowUp:"plus",ArrowDown:"minus",
  "+":"plus","-":"minus","=":"plus"};
addEventListener("keydown",e=>{
  if(e.target.matches("input,select,textarea"))return;
  const k=KMAP[e.key];if(!k)return;e.preventDefault();press(k);
});
addEventListener("keyup",e=>{
  const k=KMAP[e.key];if(!k)return;release(k);
});
/* solta tudo se a aba perder o foco, para nao deixar tecla presa */
addEventListener("blur",()=>{ held.clear(); latch.clear(); sendKeys(); });

/* ---------- render ---------- */
const kv=(l,v,extra,stale)=>'<div class="kv'+(stale?" stale":"")+'"><b>'+l+"</b><span>"+v+
  "</span>"+(extra?"<em>"+extra+"</em>":"")+"</div>";
const age=ms=>ms<0?"nunca":ms<1000?ms+" ms":ms<90000?Math.round(ms/1000)+" s":
  Math.round(ms/60000)+" min";
const flag=v=>v<0?"--":String(v);

function render(s){
  drawLcd(s.screen);
  $("#ver").textContent="v"+s.fw;
  $("#dProg").className="dot"+(s.prog.link?" on":"");
  $("#tProg").textContent=s.prog.link?
    "ativo ("+s.prog.period_ms+" ms)":"sem quadros";
  $("#dEth").className="dot"+(s.eth.link?" on":"");
  $("#tEth").textContent=s.eth.link?(s.eth.ip+" "+s.eth.speed+"M"):"sem link";
  $("#pTx").hidden=s.prog.tx;

  const v=s.val, st=v.status_age;
  $("#vals").innerHTML=
    kv("BOT",flag(v.bot),age(st),st<0)+kv("FOT",flag(v.fot),age(st),st<0)+
    kv("ABR",flag(v.abr),age(st),st<0)+kv("FEC",flag(v.fec),age(st),st<0)+
    kv("RF",flag(v.rf),age(st),st<0)+
    kv("Ciclos",v.cycles_age<0?"--":v.cycles,age(v.cycles_age),v.cycles_age<0)+
    kv("RPM",v.rpm_age<0?"--":v.rpm,age(v.rpm_age),v.rpm_age<0)+
    kv("Encoder",v.enc_age<0?"--":(v.enc>0?"+":"")+v.enc,age(v.enc_age),v.enc_age<0);

  const p=s.prog;
  $("#stats").innerHTML=
    kv("Polls",p.polls)+kv("Telas",p.screens)+kv("Beeps",p.beeps)+
    kv("Respostas",p.resp)+kv("Quadros ruins",p.bad)+kv("Checksum ruim",p.badchk)+
    kv("Periodo",p.period_ms+" ms")+kv("Ultimo quadro",age(p.age_ms))+
    kv("Tecla",p.keya+" "+p.keyb);

  const e=s.eth;
  $("#netinfo").innerHTML=
    kv("Modo",e.dhcp?"DHCP":"Estatico")+kv("IP",e.ip)+kv("Mascara",e.mask)+
    kv("Gateway",e.gw)+kv("DNS",e.dns)+kv("MAC",e.mac)+
    kv("Enlace",e.link?(e.speed+" Mbps "+(e.fd?"FD":"HD")):"caido")+
    kv("Uptime",age(s.up))+kv("Heap livre",(s.heap/1024).toFixed(0)+" kB");

  const m=s.imu;
  if(m){
    const off=!m.present, f=(v,d)=>Number(v).toFixed(d);
    $("#imuInfo").textContent=m.present?
      (m.model+" em "+m.addr+" \u00b7 \u00b1"+m.range_g+" g \u00b7 \u00b1"+m.range_dps+
       " \u00b0/s \u00b7 "+m.rate_hz+" Hz"+
       (m.calibrating?" \u00b7 calibrando: mantenha o sensor parado":
        m.calibrated?" \u00b7 giroscopio calibrado":"")):
      "Sensor nao detectado. Ligue o GY-521 em SDA=IO32 e SCL=IO33 (tabela abaixo); "+
      "o firmware procura de novo a cada 2 s.";
    $("#imuVals").innerHTML=
      kv("Pitch",f(m.pitch,1)+"\u00b0","",off)+kv("Roll",f(m.roll,1)+"\u00b0","",off)+
      kv("Movimento",off?"--":m.calibrated?(m.moving?"sim":"parado"):"--",
         m.calibrated||off?"":"aguarda calibracao",off||!m.calibrated)+
      kv("Acel X",f(m.ax,3)+" g","",off)+kv("Acel Y",f(m.ay,3)+" g","",off)+
      kv("Acel Z",f(m.az,3)+" g","",off)+
      kv("Giro X",f(m.gx,1)+" \u00b0/s","",off)+kv("Giro Y",f(m.gy,1)+" \u00b0/s","",off)+
      kv("Giro Z",f(m.gz,1)+" \u00b0/s","",off)+
      kv("Vibracao RMS",f(m.vib_rms,3)+" g","janela de 1 s",off)+
      kv("Vibracao pico",f(m.vib_peak,3)+" g","janela de 1 s",off)+
      kv("Pancadas",m.shocks,m.shock_age<0?"nenhuma":
         f(m.shock_g,2)+" g, ha "+age(m.shock_age),off)+
      kv("Temp. do chip",f(m.temp,1)+" \u00b0C","",off)+
      kv("Leituras",m.samples,m.errors+" com erro",off);
    const st=m.gate, unk=st==="desconhecido",
      ang=(set,d)=>!set?"nao gravada":off?"--":f(d,1)+"°";
    $("#gateVals").innerHTML=
      kv("Estado",unk?"--":st.toUpperCase(),
         off?"sensor ausente":!(m.gate_closed_set&&m.gate_open_set)?
           "grave as duas posicoes":unk?"posicoes iguais: grave de novo":"",unk)+
      kv("Ate FECHADA",ang(m.gate_closed_set,m.gate_deg_closed),"",
         off||!m.gate_closed_set)+
      kv("Ate ABERTA",ang(m.gate_open_set,m.gate_deg_open),"",off||!m.gate_open_set);
  }

  $("#snmpstats").innerHTML=
    kv("Estado",s.snmp.en?("udp/"+s.snmp.port):"desligado")+
    kv("Recebidos",s.snmp.rx)+kv("Respondidos",s.snmp.tx)+
    kv("Community errada",s.snmp.auth_fail)+kv("Pacotes ruins",s.snmp.bad)+
    kv("SETs",s.snmp.sets);
}

async function tick(){
  if(!busy){busy=true;
    try{const r=await fetch("/api/state");if(r.ok)render(await r.json());}catch(e){}
    busy=false;}
  setTimeout(tick,250);
}

/* ---------- configuracao ---------- */
function fill(c){
  CFG=c; ENT=c.entRoot;
  for(const k in c){
    const el=document.getElementById(k);
    if(!el)continue;
    if(el.type==="checkbox")el.checked=!!c[k];else el.value=c[k];
  }
  $("#dhcp").dispatchEvent(new Event("change"));
  $("#entRoot").textContent=ENT;
  buildOidTable();buildExamples();
}
$("#dhcp").addEventListener("change",e=>{
  ["ip","mask","gw","dns1","dns2"].forEach(id=>$("#"+id).disabled=e.target.checked);
});

async function save(form,msgSel,reboot){
  const fd=new FormData(form);
  form.querySelectorAll('input[type=checkbox]').forEach(c=>{
    if(!fd.has(c.name))fd.append(c.name,"0");else fd.set(c.name,"1");});
  const body=new URLSearchParams(fd).toString();
  const m=$(msgSel);m.className="msg";m.textContent="salvando...";
  try{
    const r=await fetch("/api/config",{method:"POST",
      headers:{"Content-Type":"application/x-www-form-urlencoded"},body});
    if(!r.ok)throw new Error(await r.text());
    m.className="msg ok";
    m.textContent=reboot?"salvo, reiniciando...":"salvo";
    if(reboot){await fetch("/api/reboot",{method:"POST"});
      setTimeout(()=>location.reload(),6000);}
    else fill(await (await fetch("/api/config")).json());
  }catch(err){m.className="msg err";m.textContent="erro: "+err.message;}
}
$("#fNet").onsubmit=e=>{e.preventDefault();save(e.target,"#mNet",true);};
$("#fSnmp").onsubmit=e=>{e.preventDefault();save(e.target,"#mSnmp",false);};
$("#fAdv").onsubmit=e=>{e.preventDefault();save(e.target,"#mAdv",false);};
$("#fScan").onsubmit=e=>{e.preventDefault();save(e.target,"#mScan",false);};
$("#fAuth").onsubmit=e=>{e.preventDefault();save(e.target,"#mAuth",false);};
$("#fImu").onsubmit=e=>{e.preventDefault();save(e.target,"#mImu",false);};
document.querySelectorAll("[data-gate]").forEach(b=>b.onclick=async()=>{
  const m=$("#mGate"), pos=b.dataset.gate;
  try{
    const j=await (await fetch("/api/gate/set?pos="+pos,{method:"POST"})).json();
    m.className="msg "+(j.ok&&!j.warning?"ok":"err");
    m.textContent=!j.ok?"erro: "+j.error:j.warning?j.warning:
      "posicao "+pos+" gravada"+(j.sep_deg>=0?" ("+j.sep_deg.toFixed(1)+"° entre as duas)":"");
  }catch(e){m.className="msg err";m.textContent="erro: "+e.message;}
});
$("#gateClear").onclick=async()=>{
  if(!confirm("Apagar as posicoes gravadas da cancela?"))return;
  await fetch("/api/gate/clear",{method:"POST"});
  $("#mGate").className="msg ok";$("#mGate").textContent="posicoes apagadas";};
$("#imuCal").onclick=async()=>{
  const m=$("#mImuCal");
  try{
    const j=await (await fetch("/api/imu/calibrate",{method:"POST"})).json();
    m.className="msg "+(j.ok?"ok":"err");
    m.textContent=j.ok?"calibrando: mantenha o sensor parado por 2 s":"erro: "+j.error;
  }catch(e){m.className="msg err";m.textContent="erro: "+e.message;}
};
$("#scanNow").onclick=async()=>{await fetch("/api/scan",{method:"POST"});
  $("#mScan").className="msg ok";$("#mScan").textContent="varredura disparada";};
$("#bReboot").onclick=async()=>{if(!confirm("Reiniciar o modulo?"))return;
  await fetch("/api/reboot",{method:"POST"});
  $("#mSys").textContent="reiniciando...";setTimeout(()=>location.reload(),6000);};
$("#bReset").onclick=async()=>{
  if(!confirm("Restaurar padroes de fabrica? A rede volta para o IP fixo 192.168.1.21."))return;
  await fetch("/api/factory",{method:"POST"});
  $("#mSys").textContent="restaurado, reiniciando...";setTimeout(()=>location.reload(),6000);};

/* ---------- tabela de OIDs / exemplos ---------- */
const OIDS=[
 ["1.3.6.1.2.1.1","system","","grupo padrao (sysDescr, sysUpTime, sysName, ...)"],
 ["{E}.1.1.0","fwVersion","String","versao do firmware"],
 ["{E}.1.2.0","uptimeSec","Gauge32","segundos desde o boot"],
 ["{E}.1.3.0","macAddress","String","MAC da interface Ethernet"],
 ["{E}.1.4.0","ipAddress","IpAddress","IP atual"],
 ["{E}.1.5.0","ethLink","Integer","1 = enlace Ethernet ativo"],
 ["{E}.1.6.0","ethSpeed","Gauge32","Mbps negociados"],
 ["{E}.1.7.0","freeHeap","Gauge32","heap livre em bytes"],
 ["{E}.2.1.0","progLink","Integer","1 = a placa esta enviando quadros"],
 ["{E}.2.2.0","lastFrameAge","Gauge32","ms desde o ultimo quadro"],
 ["{E}.2.3.0","pollPeriod","Gauge32","periodo medio do poll em ms (~24)"],
 ["{E}.2.4.0","framesTotal","Counter32","quadros validos recebidos"],
 ["{E}.2.5.0","pollsTotal","Counter32","comandos de poll recebidos"],
 ["{E}.2.6.0","screensTotal","Counter32","comandos de tela recebidos"],
 ["{E}.2.7.0","beepsTotal","Counter32","comandos de beep recebidos"],
 ["{E}.2.8.0","badFrames","Counter32","quadros descartados por forma invalida"],
 ["{E}.2.9.0","badChecksum","Counter32","quadros descartados por checksum"],
 ["{E}.2.10.0","responses","Counter32","respostas enviadas para a placa"],
 ["{E}.2.11.0","txEnabled","Integer","0 = modo escuta"],
 ["{E}.3.1.0","screenLine1","String","linha 1 do LCD, ja em UTF-8"],
 ["{E}.3.2.0","screenLine2","String","linha 2 do LCD"],
 ["{E}.3.3.0","screenRawHex","String","os 32 bytes crus em hexadecimal"],
 ["{E}.3.4.0","screenId","Integer","0 desconhecida, 1 status, 2 ciclos, 3 idioma, 4 splash"],
 ["{E}.3.5.0","screenAge","Gauge32","ms desde a ultima mudanca de tela"],
 ["{E}.3.6.0","screenSeq","Counter32","contador de mudancas de tela"],
 ["{E}.4.1.0","fec","Integer","estado FEC: 0, 1 ou -1 se nunca visto"],
 ["{E}.4.2.0","abr","Integer","estado ABR"],
 ["{E}.4.3.0","bot","Integer","estado BOT"],
 ["{E}.4.4.0","fot","Integer","estado FOT"],
 ["{E}.4.5.0","rf","Integer","estado RF"],
 ["{E}.4.6.0","statusAge","Gauge32","ms desde a ultima leitura da tela de status"],
 ["{E}.5.1.0","cycles","Gauge32","contador de ciclos da placa"],
 ["{E}.5.2.0","cyclesAge","Gauge32","ms desde a leitura dos ciclos"],
 ["{E}.5.3.0","rpm","Gauge32","rotacao lida na tela de ciclos"],
 ["{E}.5.4.0","rpmAge","Gauge32","ms desde a leitura do rpm"],
 ["{E}.5.5.0","encoder","Integer","angulo do encoder (com sinal)"],
 ["{E}.5.6.0","encoderAge","Gauge32","ms desde a leitura do encoder"],
 ["{E}.6.1.0","pressKey","Integer","SET 1=menos 2=mais 3=esquerda 4=direita"],
 ["{E}.7.1.0","imuPresent","Integer","1 = acelerometro/giroscopio respondendo"],
 ["{E}.7.2.0","imuModel","String","chip detectado (MPU-6050, MPU-6500, ...)"],
 ["{E}.7.3.0","accelX","Integer","aceleracao X em mg (milesimos de g)"],
 ["{E}.7.4.0","accelY","Integer","aceleracao Y em mg"],
 ["{E}.7.5.0","accelZ","Integer","aceleracao Z em mg"],
 ["{E}.7.6.0","gyroX","Integer","rotacao X em milligraus/s"],
 ["{E}.7.7.0","gyroY","Integer","rotacao Y em milligraus/s"],
 ["{E}.7.8.0","gyroZ","Integer","rotacao Z em milligraus/s"],
 ["{E}.7.9.0","imuTemp","Integer","temperatura do chip em milligraus Celsius"],
 ["{E}.7.10.0","pitch","Integer","inclinacao em milligraus (-90000..90000)"],
 ["{E}.7.11.0","roll","Integer","inclinacao em milligraus (-180000..180000)"],
 ["{E}.7.12.0","vibRms","Gauge32","vibracao RMS da ultima janela de 1 s, em mg"],
 ["{E}.7.13.0","vibPeak","Gauge32","pico de vibracao da ultima janela, em mg"],
 ["{E}.7.14.0","moving","Integer","1 = girando (so com o giroscopio calibrado)"],
 ["{E}.7.15.0","shocks","Counter32","pancadas detectadas desde o boot"],
 ["{E}.7.16.0","shockAge","Gauge32","ms desde a ultima pancada"],
 ["{E}.7.17.0","shockPeak","Gauge32","pico da ultima pancada, em mg"],
 ["{E}.7.18.0","imuAge","Gauge32","ms desde a ultima leitura boa do sensor"],
 ["{E}.7.19.0","imuSamples","Counter32","leituras boas"],
 ["{E}.7.20.0","imuErrors","Counter32","leituras com erro de I2C"],
 ["{E}.7.21.0","imuCalibrated","Integer","1 = giroscopio calibrado"],
 ["{E}.7.22.0","gateState","Integer","cancela: 1 = fechada, 2 = aberta, 0 = desconhecido"],
];
function buildOidTable(){
  $("#oids tbody").innerHTML=OIDS.map(r=>
    "<tr><td><code>"+r[0].replace("{E}",ENT)+"</code></td><td>"+r[1]+"</td><td>"+r[2]+
    "</td><td>"+r[3]+"</td></tr>").join("");
}
function buildExamples(){
  const h=location.host||"espprog.local";
  $("#exScreen").textContent=
    "curl http://"+h+"/api/screen.txt\n"+
    "curl -s http://"+h+"/api/screen\n"+
    '  {"l1":"FEC:0 ABR:0     ","l2":"BOT:0 FOT:0 RF:0",\n'+
    '   "raw":"464543...","id":1,"seq":12,"age_ms":4210}';
  $("#exKey").textContent=
    "curl http://"+h+"/api/key?k=plus            # toque\n"+
    "curl http://"+h+"/api/key?k=left&action=down\n"+
    "curl http://"+h+"/api/key?action=up\n"+
    "curl http://"+h+"/api/key?k=plus,left       # combinacao";
  $("#exImu").textContent=
    "curl -s http://"+h+"/api/imu\n"+
    '  {"present":true,"model":"MPU-6050","pitch":0.42,"roll":-1.07,\n'+
    '   "ax":-0.007,"ay":-0.019,"az":1.002,"moving":false,"shocks":0, ...}\n'+
    "curl -X POST http://"+h+"/api/imu/calibrate";
  $("#exCfg").textContent=
    "curl -s http://"+h+"/api/config\n"+
    "curl -X POST http://"+h+"/api/config -d 'dhcp=0&ip=192.168.1.50&mask=255.255.255.0&gw=192.168.1.1'\n"+
    "curl -X POST http://"+h+"/api/config -d 'roCommunity=cancela&snmpPort=161'\n"+
    "curl -X POST http://"+h+"/api/reboot";
  $("#exSnmp").textContent=
    "snmpwalk -v2c -c "+(CFG.roCommunity||"public")+" "+(CFG.ip||h)+" "+ENT+"\n"+
    "snmpget  -v2c -c "+(CFG.roCommunity||"public")+" "+(CFG.ip||h)+" "+ENT+".4.3.0   # BOT\n"+
    "snmpget  -v2c -c "+(CFG.roCommunity||"public")+" "+(CFG.ip||h)+" "+ENT+".5.1.0   # ciclos\n"+
    "snmpwalk -v2c -c "+(CFG.roCommunity||"public")+" "+(CFG.ip||h)+" "+ENT+".7       # sensor\n"+
    "snmpset  -v2c -c <rw> "+(CFG.ip||h)+" "+ENT+".6.1.0 i 2      # aperta a tecla +";
}

/* ---------- inicio ---------- */
(async()=>{
  try{fill(await (await fetch("/api/config")).json());}catch(e){}
  tick();
})();
</script>
</body></html>
)HTML";
