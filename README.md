# vita-coldboot (`coldboot_reset.skprx`)

Plugin de kernel (`*KERNEL`) para PS Vita (HENkaku Ensō) que executa automaticamente **um único Cold Reset (`kscePowerRequestColdReset`)** quando o console dá boot, e depois deixa o sistema subir normalmente.

## O que ele faz (e o que não faz)

Quando o PS Vita liga do zero, o plugin automatiza o equivalente a ir em **Configurações → Configurações do HENkaku → Reiniciar dispositivo** uma única vez na inicialização:

* **O que faz:** dispara `kscePowerRequestColdReset()`, reiniciando o kernel, os drivers (incluindo GPU/vídeo), os registradores do SoC e a RAM carregada naquele boot; no segundo boot, remove sua flag temporária e encerra a própria thread (sem ficar rodando em segundo plano).
* **Quando atua:** apenas em **boots reais** (ex.: ao ligar o console após desligar por completo).
* **Quando NÃO atua:** não roda ao entrar ou acordar do **Modo de Repouso (*standby*)**, porque o kernel apenas retoma a RAM suspensa sem recarregar plugins do `taiHEN`.
* **O que NÃO faz:** não apaga caches em disco, não faz *Rebuild Database* e não reseta o controlador de energia/bateria (*Syscon / Ernie*).
* **Observação sobre reinício manual:** como a flag é removida no 2º boot, se você mandar reiniciar manualmente pelo HENkaku/VitaShell enquanto o Vita já está ligado, ele também executará 1 ciclo extra na volta.

## Como funciona passo a passo

1. **1º boot (ao ligar o console):**
   * Aguarda `SceSyscon` e o processo da LiveArea (`SceShell`) iniciarem (`ksceKernelSysrootGetShellPid() > 0`), mais `1,5 s` de estabilização.
   * Verifica se `ur0:tai/.coldboot_done` existe.
   * Como **não existe**, grava a flag em `ur0:tai/.coldboot_done`, força sincronização no armazenamento (`ksceIoSyncByFd` + `ksceIoSync("ur0:", 0)`), confirma com `ksceIoGetstat` que o arquivo está salvo com tamanho > 0 e chama `kscePowerRequestColdReset()`.
2. **2º boot (logo após o Cold Reset):**
   * Encontra `ur0:tai/.coldboot_done`, apaga a flag e sobrescreve `ur0:tai/coldboot_reset.log` com os indicadores do `SceKblParam` dos dois boots (`boot1_coldreset` e `boot2_ready`).
   * Encerra a thread (`ksceKernelExitDeleteThread(0)`) e o PS Vita segue normalmente na LiveArea.

## Proteções contra boot loop

* **Modo de Segurança / Update / BSOD:** verifica `ksceSysrootIsSafeMode()`, `ksceSysrootIsUpdateMode()` e `ksceSysrootIsBsodReboot()` e sai sem fazer nada (`SCE_KERNEL_START_NO_RESIDENT`).
* **Verificação em disco:** se `ur0:tai/.coldboot_done` não puder ser gravada e confirmada com tamanho > 0, o plugin **aborta o reset**.
* **Botões para pular o reset:**
  * Segurar **`SELECT`** ou **`R`** durante o boot cancela o reset automático apenas daquela inicialização (mantendo todos os outros plugins).
  * Segurar **`L`** ao ligar desativa todos os plugins do `taiHEN` normalmente.

## Instalação

1. Baixe o arquivo `coldboot_reset.skprx` na seção **Releases** deste repositório.
2. Copie `coldboot_reset.skprx` para `ur0:/tai/coldboot_reset.skprx` (via VitaShell / FTP).
3. Adicione a linha abaixo na seção `*KERNEL` do seu `ur0:/tai/config.txt`:

```text
*KERNEL
ur0:tai/0syscall6.skprx
ur0:tai/nonpdrm.skprx
ur0:tai/coldboot_reset.skprx
```

4. Desligue o console por completo e ligue novamente.
   * Ele entrará na LiveArea por ~1,5 s, fará 1 reinício automático e permanecerá ligado no 2º boot.
   * Para confirmar o funcionamento, abra `ur0:/tai/coldboot_reset.log` no VitaShell e verifique se constam `boot1_coldreset` e `boot2_ready`.

## Compilação (VitaSDK)

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
mkdir -p build && cd build
cmake ..
make
```
