# STM32F103ZET6 音乐播放知识点与调试流程

本文档记录当前工程中 MP3 音乐播放涉及的核心知识、软件流程、硬件链路、关键配置和调试方法。文档以当前已经能正常播放音乐的版本为准，重点说明“数据从 SD 卡到耳机/扬声器发声”这条完整链路。

相关源码主要在：

- `STD_STM32F103ZET6_USART/USER/main.c`
- `STD_STM32F103ZET6_USART/AUDIO/audio_player.c`
- `STD_STM32F103ZET6_USART/AUDIO/audio_player.h`
- `STD_STM32F103ZET6_USART/AUDIO/board_audio.c`
- `STD_STM32F103ZET6_USART/AUDIO/board_audio.h`
- `STD_STM32F103ZET6_USART/AUDIO/sdcard_stub.c`
- `STD_STM32F103ZET6_USART/AUDIO/pff/`
- `STD_STM32F103ZET6_USART/AUDIO/helix/`
- `STD_STM32F103ZET6_USART/MDK-ARM/STD_Proj.uvprojx`

## 1. 总体目标

当前音乐播放目标是：

1. STM32F103ZET6 通过 SDIO 从 SD 卡读取 FAT 文件系统中的 MP3 文件。
2. 软件找到根目录下第一首 `.mp3` 文件。
3. 使用 Helix 定点 MP3 解码器把 MP3 压缩数据解码为 16 bit PCM。
4. 对 PCM 做音量缩放和声道处理。
5. 通过 I2S3 + DMA 把 PCM 持续送到 ES8311。
6. ES8311 把数字音频转换成模拟音频。
7. 模拟音频从耳机输出，或者再经过功放输出到扬声器。

最终数据流可以概括为：

```text
SD 卡
  -> SDIO 读扇区
  -> Petit FatFs 读文件
  -> MP3 字节流
  -> Helix MP3 解码
  -> 16 bit PCM
  -> 音量缩放 / 声道处理
  -> I2S3 DMA 环形缓冲
  -> I2S3: MCLK/BCLK/LRCK/SD
  -> ES8311 DAC
  -> 耳机输出
  -> 可选：NS4150B 功放 -> 扬声器
```

## 2. 当前硬件链路

### 2.1 控制关系

当前板子使用 STM32F103ZET6 控制 ES8311 音频 Codec。ES8311 负责完成数字音频和模拟音频之间的转换。

播放链路：

```text
STM32F103ZET6
  -> I2S3 数字音频
  -> ES8311 DAC
  -> 耳机输出
  -> 可选功放 NS4150B
  -> 扬声器
```

配置链路：

```text
STM32F103ZET6
  -> 软件模拟 I2C
  -> ES8311 寄存器
```

存储链路：

```text
SD 卡
  -> SDIO
  -> STM32F103ZET6
```

### 2.2 I2S 引脚

当前 I2S3 引脚定义在 `board_audio.c`：

| 功能        | STM32 引脚 | 说明                                             |
| ----------- | ---------: | ------------------------------------------------ |
| MCLK        |        PC7 | ES8311 主时钟                                    |
| BCLK / CK   |        PB3 | I2S 位时钟                                       |
| LRCK / WS   |       PA15 | 左右声道选择时钟                                 |
| SD / TX     |        PB5 | STM32 发给 ES8311 的音频数据                     |
| RX / ASDOUT |        PB4 | ES8311 发回 STM32 的录音数据，播放时不是关键路径 |

注意：PB3 和 PA15 默认与 JTAG 功能相关。当前代码通过：

```c
GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
```

释放 JTAG 占用，让 PB3/PA15 可用于 I2S。若逻辑分析仪看不到 PB3 或 PA15 的波形，优先检查 AFIO/JTAG 重映射。

### 2.3 ES8311 I2C 引脚

当前 ES8311 配置使用软件模拟 I2C：

| 功能 | STM32 引脚 |
| ---- | ---------: |
| SCL  |        PB6 |
| SDA  |        PB7 |

ES8311 地址当前探测两个写地址：

|   写地址 | 说明             |
| -------: | ---------------- |
| `0x30` | 常见 ES8311 地址 |
| `0x32` | 备用地址         |

读地址由写地址加 `0x01` 得到。

### 2.4 功放使能 PC13

本工程里 PC13 控制功放使能，但硬件中间有 SS8050 三极管反相。因此逻辑必须按原理图理解：

| PC13 电平 | 功放状态 |
| --------: | -------- |
|    低电平 | 使能     |
|    高电平 | 关闭     |

当前代码定义：

```c
#define AUDIO_AMP_ENABLE()  GPIO_ResetBits(GPIOC, GPIO_Pin_13)
#define AUDIO_AMP_DISABLE() GPIO_SetBits(GPIOC, GPIO_Pin_13)
```

这表示：

- `board_audio_amp_set(1)` 会把 PC13 拉低，打开功放。
- `board_audio_amp_set(0)` 会把 PC13 拉高，关闭功放。

耳机直连 ES8311 时理论上不经过功放，PC13 不应该影响耳机是否有声。但如果用的是 ES8311 + NS4150B 成品模块并听扬声器，就必须保证 PC13 低电平。

## 3. I2S 基础知识

### 3.1 I2S 信号含义

播放时 STM32 是 I2S 主机，ES8311 是 I2S 从机。

| 信号    | 含义                                   |
| ------- | -------------------------------------- |
| MCLK    | 主时钟，供 Codec 内部 PLL/采样系统使用 |
| BCLK    | 位时钟，每传输一位数据跳变一次         |
| LRCK/WS | 左右声道选择，频率等于采样率           |
| SD      | 串行音频数据                           |

### 3.2 采样率和时钟关系

以 44.1 kHz、16 bit、双声道 I2S 为例：

- LRCK 约等于 `44.1 kHz`。
- 每个采样点左右声道各 16 bit。
- 常见 BCLK 约等于 `44.1 kHz * 32 = 1.4112 MHz`，也可能因为外设帧格式形成 `64 * Fs` 的位时钟。
- MCLK 常见为 `256 * Fs`，44.1 kHz 时约为 `11.2896 MHz`，48 kHz 时约为 `12.288 MHz`。

逻辑分析仪验证时最重要的是：

1. LRCK 是否稳定接近实际播放采样率。
2. BCLK 是否连续。
3. SD 是否有数据变化，而不是长期全 0 或固定电平。
4. MCLK 是否输出。
5. 左右声道边界是否符合 Philips I2S 时序。

### 3.3 当前 I2S 配置

当前 `board_audio_i2s_init_tx_rate()` 配置：

```text
I2S 外设: SPI3/I2S3
模式: MasterTx
标准: Philips I2S
数据宽度: 16 bit
MCLK: Enable
CPOL: Low
采样率: 根据 MP3 帧采样率选择 44.1k 或 48k
```

代码中有一个采样率映射：

```c
if (sample_rate >= 43000U && sample_rate <= 45000U)
    return I2S_AudioFreq_44k;
return I2S_AudioFreq_48k;
```

所以：

- MP3 是 44.1 kHz 时，I2S 设置为 44.1 kHz。
- 其他情况默认按 48 kHz 输出。

如果 MP3 解码出来是 44.1 kHz，但 I2S 错配成 48 kHz，声音会变调。若 I2S 时钟低很多，音乐会明显慢速、卡顿或断续。

## 4. ES8311 基础知识

### 4.1 ES8311 在播放中的角色

ES8311 是音频 Codec。播放时它主要做三件事：

1. 从 I2S 接收 STM32 发来的 PCM 数字音频。
2. 通过内部 DAC 转成模拟音频。
3. 通过模拟输出驱动耳机输出或后级功放输入。

如果 I2S 数据正确，但 ES8311 寄存器配置错误，可能出现：

- 完全无声。
- 只有爆音或短促响声。
- 音量极小。
- 左右声道不对。
- 耳机无声但功放有声，或相反。

### 4.2 ES8311 配置入口

播放配置在：

```text
STD_STM32F103ZET6_USART/AUDIO/board_audio.c
  -> board_audio_codec_init_playback()
```

初始化大致流程：

1. 探测 ES8311 I2C 地址。
2. 复位 ES8311。
3. 配置系统时钟和数字音频接口。
4. 配置 DAC 播放路径。
5. 配置输出音量。
6. 打开模拟输出相关模块。
7. 读取关键寄存器，输出调试信息。

当前调试会读取寄存器，例如：

```text
r00 r01 r02 r03 r04 r05 r06 r07 r08 r09 r0A r0B r0C
r0D r0E r0F r10 r11 r12 r13 r14 r16 r25 r31 r32 r33 r34 r37 r44
```

这些寄存器用于确认：

- I2C 是否真的能读写 ES8311。
- 初始化后寄存器值是否保持。
- DAC/输出路径是否被打开。
- 音量寄存器是否符合预期。

### 4.3 当前音量策略

当前软件音量在 `board_audio.c` 中：

```c
#define AUDIO_PLAYBACK_VOLUME_PERCENT 10U
```

`board_audio_apply_volume()` 会把所有输出 PCM 乘以 10%：

```c
value = sample * 10 / 100;
```

这样做的原因：

- 成品模块或功放输入灵敏度较高，100% 音量可能过大。
- 软件统一缩放比直接改 Codec 多处模拟增益更直观。
- 能避免满幅 PCM 导致后级过载、破音或扬声器冲击。

如果后续觉得声音太小，应先确认硬件输出链路正常，再小幅提高此百分比，例如 15%、20%、30%。不建议一开始直接开到 100%。

## 5. SD 卡和文件系统知识

### 5.1 SDIO 初始化流程

当前 SD 卡底层在 `sdcard_stub.c`。初始化流程大致如下：

```text
SDIO_DeInit
GPIO 初始化
低速 1-bit 时钟
CMD0
CMD8
ACMD41
CMD2
CMD3
CMD7
CMD16
ACMD6
切换传输时钟
进入 ready 状态
```

关键命令含义：

| 命令   | 作用                             |
| ------ | -------------------------------- |
| CMD0   | 复位 SD 卡进入 idle              |
| CMD8   | 检查 SD 版本和电压范围           |
| ACMD41 | 初始化并等待卡 ready             |
| CMD2   | 读取 CID                         |
| CMD3   | 获取 RCA                         |
| CMD7   | 选中卡                           |
| CMD16  | 设置块长度 512 字节              |
| ACMD6  | 设置总线宽度，当前工程保持 1-bit |
| CMD17  | 读取单个 512 字节扇区            |

当前传输模式：

```text
SDIO 1-bit
polling 读扇区
512 字节扇区
```

当前为了稳定验证，保留 1-bit 总线；后续性能优化可以改 4-bit + DMA。

### 5.2 FAT 挂载

文件系统使用 Petit FatFs，只读访问 SD 卡。

初始化中：

```text
audio_player_init()
  -> sdcard_stub_init()
  -> pf_mount(&g_fs)
```

`pf_mount` 成功表示：

- SDIO 能读到扇区。
- sector0 或分区引导扇区能被 FatFs 识别。
- FAT 文件系统结构基本正常。

曾经出现过：

```text
[fs] sector0 sig=241D
[audio-demo] audio_player_init=-3 err=pf_mount failed 6
```

这类情况说明读到的 sector0 不是标准 FAT/MBR 引导扇区。后来重新整理 SD 卡和 MP3 文件后，sector0 变成：

```text
sig=AA55 jump=EB 58 90
```

这才是典型 FAT 引导扇区特征。

### 5.3 MP3 文件查找

当前代码不是固定播放某一个名字，而是在根目录查找第一首 `.mp3`：

```text
audio_player_find_first_mp3()
  -> pf_opendir("/")
  -> pf_readdir()
  -> 判断扩展名 .mp3
```

注意：

- 当前只扫描根目录。
- 当前路径缓冲区大小为 13 字节，适合 8.3 短文件名，例如 `TEST.MP3`。
- 如果 SD 卡里是长文件名、中文文件名或子目录文件，当前版本可能找不到。

建议实际测试时：

1. 将一个确认正常的 MP3 放到 SD 卡根目录。
2. 文件名使用 8.3 格式，例如 `TEST.MP3`。
3. 不要使用损坏文件或特殊编码文件名。

## 6. MP3 解码知识

### 6.1 MP3 文件不是 PCM

MP3 是压缩音频，不能直接送 I2S。I2S 需要的是连续 PCM 采样点。

正确流程是：

```text
MP3 文件字节流
  -> 找帧同步头
  -> 解码 MP3 帧
  -> 得到 PCM
  -> PCM 送 I2S
```

如果把 MP3 压缩数据当 PCM 播放，听到的通常是刺耳噪声、滋滋声或无意义杂音。

### 6.2 当前使用 Helix 定点解码器

当前工程使用：

```c
#include "helix/pub/mp3dec.h"
```

主要 API：

| API                       | 作用                                 |
| ------------------------- | ------------------------------------ |
| `MP3InitDecoder()`      | 初始化解码器                         |
| `MP3FreeDecoder()`      | 释放/重置解码器                      |
| `MP3FindSyncWord()`     | 在字节流里寻找 MP3 帧同步头          |
| `MP3Decode()`           | 解码一帧 MP3                         |
| `MP3GetLastFrameInfo()` | 获取上一帧采样率、声道数、码率等信息 |

当前 MP3 解码主循环在：

```text
audio_player_play_mp3_from_sd()
```

核心步骤：

1. 用 `pf_open()` 打开 MP3 文件。
2. 从 SD 卡分块读取压缩数据到 `file_buf`。
3. 用 `MP3FindSyncWord()` 找同步头。
4. 调用 `MP3Decode()` 解码一帧。
5. 调用 `MP3GetLastFrameInfo()` 获取帧信息。
6. 根据帧采样率设置 I2S。
7. 调用 `board_audio_play_pcm()` 输出 PCM。
8. 将剩余未解码字节移动到缓冲区开头，继续读下一块。

### 6.3 为什么要用定点解码

STM32F103ZET6 是 Cortex-M3，没有硬件 FPU。浮点 MP3 解码会非常慢，容易导致：

- 音乐像 0.1 倍速。
- 声音卡顿。
- DMA/I2S 缓冲被播空。
- 只有噪声或断断续续的旋律。

Helix 是定点 MP3 解码器，更适合无 FPU MCU。当前工程还做了这些适配：

- Keil 工程加入 Helix 源文件。
- include path 加入 `AUDIO/helix/pub` 和 `AUDIO/helix/real`。
- Keil 优化等级为 O3，对应 uvprojx 中 `<Optim>4</Optim>`。
- Helix 内部 buffer 改为静态分配，避免裸机环境下 malloc/free 问题。
- ARMCC 相关 64 位乘加/移位宏做了兼容。

### 6.4 MP3 帧和实时性

常见 MPEG Layer III 一帧通常包含每声道 1152 个采样点。

以 44.1 kHz 为例：

```text
一帧音频时间 = 1152 / 44100 ≈ 26.1 ms
```

所以系统必须在约 26 ms 内完成：

- SD 读取足够数据。
- MP3 解码一帧。
- PCM 写入 DMA 缓冲。

只要读卡 + 解码 + 写缓冲的平均耗时小于音频真实时长，就能持续播放。否则就会缓冲欠载，出现卡顿、慢速或噪声。

当前调试指标 `gap_pct` 就是用来判断实时性：

```text
gap_pct = (read_time_us + decode_time_us) / audio_time_us * 100%
```

判断原则：

|      gap_pct | 含义                           |
| -----------: | ------------------------------ |
| 明显小于 100 | 解码速度快于播放速度，正常     |
|     接近 100 | 余量很小，可能偶发卡顿         |
|     大于 100 | 软件处理慢于实时播放，必然卡顿 |

## 7. PCM 数据处理

### 7.1 PCM 格式

当前 I2S 输出使用 16 bit PCM。

MP3 解码后 Helix 输出 `short`，也就是有符号 16 bit PCM：

```text
-32768 ... 0 ... +32767
```

PCM 的数值越接近正负满幅，声音越大；长期超出后级承受范围会导致削波、破音或冲击。

### 7.2 声道处理

`board_audio_play_pcm()` 支持输入：

- 单声道 PCM
- 双声道 PCM

当前播放路径为了兼容板上输出链路，将双声道 MP3 做平均混音：

```text
mono = (left + right) / 2
```

然后把同一个样本写到 I2S 左右声道：

```text
I2S left  = mono
I2S right = mono
```

这样做的好处：

- 无论耳机/功放接左声道还是右声道，都能听到完整声音。
- 避免只接单边导致某些歌曲人声或伴奏缺失。
- 硬件验证阶段更容易判断“有无声音”和“音乐是否正常”。

如果后续确认硬件左右声道完整，可以改为真正立体声输出。

### 7.3 软件音量

当前所有 PCM 输出前都会经过：

```text
board_audio_apply_volume()
```

音量系数为：

```text
AUDIO_PLAYBACK_VOLUME_PERCENT = 10
```

这意味着输出约为原始 PCM 的 10%。这是当前验证中比较安全的音量。

## 8. DMA 环形缓冲

### 8.1 为什么必须用 DMA

如果 CPU 用轮询方式等待 I2S TXE 再写数据，会出现两个问题：

1. CPU 大量时间被 I2S 发送占用。
2. MP3 解码和 SD 读取无法稳定跟上。

音乐播放本质上是实时流。I2S 必须按固定采样率持续输出，不能等 CPU 忙完再输出。因此当前工程使用 DMA：

```text
内存 PCM 环形缓冲
  -> DMA2_Channel2
  -> SPI3->DR
  -> I2S3 输出
```

### 8.2 当前 DMA 参数

当前关键配置：

```text
DMA 通道: DMA2_Channel2
方向: Memory -> Peripheral
外设地址: &SPI3->DR
外设数据宽度: HalfWord
内存数据宽度: HalfWord
模式: Circular
优先级: High
缓冲大小: 4096 halfwords
预填充: 1024 halfwords
保护区: 32 halfwords
```

注意 halfword 是 16 bit。I2S 双声道一帧需要两个 halfword：

```text
left halfword + right halfword
```

所以 4096 halfwords 对应 2048 个双声道采样帧。

以 44.1 kHz 估算：

```text
2048 / 44100 ≈ 46.4 ms
```

这个缓冲能提供几十毫秒的抗抖动能力。

### 8.3 环形缓冲读写关系

DMA 是读者，CPU 是写者：

```text
CPU 写 PCM -> g_audio_dma_buffer
DMA 读 PCM -> SPI3->DR
```

代码维护：

- `g_audio_dma_write_index`
- `g_audio_dma_write_total`
- `g_audio_dma_read_total`
- `g_audio_dma_underruns`

当 DMA 播放速度超过 CPU 填充速度，缓冲会被读空。这叫 underrun。出现 underrun 时听感通常是：

- 卡顿
- 断续
- 噪声
- 音乐明显变慢

调试中重点看：

```text
dma_under
dma_used
dma_w
```

判断原则：

| 指标          | 正常情况           |
| ------------- | ------------------ |
| `dma_under` | 播放期间最好不增加 |
| `dma_used`  | 不应长期接近 0     |
| `dma_w`     | 播放时应持续变化   |

### 8.4 预填充的意义

DMA 启动后会立刻按 I2S 时钟消费缓冲。若刚启动时没有一定数据储备，第一帧开始就可能 underrun。

当前用：

```text
AUDIO_DMA_PREROLL_HALFWORDS = 1024
```

表示启动后保留一段静音/预填充空间，让 CPU 有时间继续写入真实 PCM。

## 9. 当前主程序流程

`main.c` 当前播放流程：

```text
delay_Init()
MX_USART1_Init(115200)
打印 boot/build/USART
打开 SD 和音频调试输出
audio_player_init()
dump_board_audio_debug("audio-init")
board_audio_amp_set(1)
audio_player_find_first_mp3()
audio_player_play_from_sd()
打印 play_result
dump_player_stats()
dump_board_audio_debug("playback")
while(1)
```

关键日志含义：

```text
[audio-demo] baremetal boot
```

说明程序进入 main。

```text
[audio-demo] init start
```

说明准备开始音频和 SD 初始化。

```text
[audio-demo] audio_player_init=0 err=ok
```

说明 ES8311、SDIO、FAT、MP3 解码器初始化成功。

```text
[audio-demo] find_mp3=0 err=mp3 file found
```

说明根目录找到 MP3。

```text
[audio-demo] play_result=0 err=mp3 playback done
```

说明播放流程正常结束。

## 10. 播放器软件分层

### 10.1 `main.c`

职责：

- 初始化基础外设。
- 打开调试输出。
- 调用播放初始化。
- 查找 MP3。
- 启动播放。
- 打印最终统计。

它不负责 SDIO 细节、ES8311 寄存器细节和 MP3 解码细节。

### 10.2 `audio_player.c`

职责：

- 组织播放流程。
- 初始化 SD、FAT 和 MP3 解码器。
- 查找 MP3 文件。
- 从文件读取 MP3 压缩数据。
- 调用 Helix 解码。
- 把 PCM 交给 `board_audio`。
- 统计读取、解码、播放耗时。

可以把它理解为“播放器状态机”。

### 10.3 `board_audio.c`

职责：

- GPIO 初始化。
- 软件 I2C。
- ES8311 配置。
- I2S3 配置。
- DMA 环形缓冲。
- PCM 音量和声道处理。
- 功放 PC13 控制。
- 音频硬件调试信息采集。

可以把它理解为“板级音频驱动”。

### 10.4 `sdcard_stub.c`

职责：

- SDIO 初始化。
- SD 命令发送。
- SD 响应检查。
- CMD17 读 512 字节扇区。
- 为 Petit FatFs 提供底层读卡能力。

### 10.5 `pff`

Petit FatFs 负责：

- FAT 文件系统挂载。
- 打开目录。
- 读取目录项。
- 打开文件。
- 顺序读取文件内容。

当前只读，不负责写文件。

### 10.6 `helix`

Helix 负责：

- MP3 帧同步。
- MP3 Layer III 解码。
- 输出 16 bit PCM。

它只处理压缩音频格式，不知道 SD 卡、I2S、ES8311。

## 11. 调试信息体系

### 11.1 播放统计

`audio_player_stats_t` 包含：

| 字段                       | 含义                       |
| -------------------------- | -------------------------- |
| `bytes_read`             | 从 MP3 文件读取的字节数    |
| `decode_calls`           | 调用`MP3Decode()` 的次数 |
| `frames_decoded`         | 成功解码的 MP3 帧数        |
| `frames_skipped`         | 跳过或错误的帧数           |
| `pcm_blocks_sent`        | 送到音频输出的 PCM 块数    |
| `last_sample_rate`       | 最近一帧采样率             |
| `last_channels`          | 最近一帧声道数             |
| `last_layer`             | MP3 layer                  |
| `last_bitrate_kbps`      | 最近一帧码率               |
| `last_frame_bytes`       | 最近一帧消耗的字节数       |
| `last_samples_per_frame` | 最近一帧每声道采样数       |
| `first_frame_offset`     | 第一个有效 MP3 帧偏移      |
| `total_audio_ms`         | 已解码音频时长             |
| `read_time_us`           | 累计读文件耗时             |
| `decode_time_us`         | 累计解码耗时               |
| `pcm_time_us`            | 累计写 PCM 耗时            |
| `max_decode_us`          | 单次最大解码耗时           |
| `last_decode_error`      | 最近一次解码返回码         |

### 11.2 板级音频统计

`board_audio_debug_info_t` 包含：

| 字段                   | 含义                  |
| ---------------------- | --------------------- |
| `tx_frames`          | 已送出的 I2S 音频帧数 |
| `rx_frames`          | 已接收的 I2S 音频帧数 |
| `last_sample_rate`   | 当前/最近采样率       |
| `last_channels`      | 最近输入 PCM 声道数   |
| `codec_addr`         | ES8311 I2C 写地址     |
| `codec_fail_reg`     | I2C 读写失败的寄存器  |
| `codec_regXX`        | ES8311 关键寄存器快照 |
| `spi3_sr`            | SPI3/I2S 状态寄存器   |
| `spi3_i2scfgr`       | I2S 配置寄存器        |
| `spi3_i2spr`         | I2S 分频寄存器        |
| `spi3_cr2`           | DMA 请求相关寄存器    |
| `gpio*_idr`          | GPIO 输入状态         |
| `gpio*_odr`          | GPIO 输出状态         |
| `gpio_amp_state`     | PC13 输出状态         |
| `dma_underruns`      | DMA 欠载次数          |
| `dma_used_halfwords` | DMA 缓冲占用          |
| `dma_write_index`    | DMA 写指针            |
| `last_error`         | 板级音频错误码        |
| `last_mode`          | 当前模式，播放或录音  |

### 11.3 关键日志判断

正常初始化应该看到：

```text
[sd] init ok
[audio-demo] audio_player_init=0 err=ok
[audio-demo] find_mp3=0 err=mp3 file found
[audio-demo] play file=...
```

正常播放完成后应该看到：

```text
[audio-demo] play_result=0 err=mp3 playback done
[mp3] frames=...
[mp3-time] gap_pct=...
[playback] dma_under=...
```

如果只有：

```text
[audio-demo] baremetal boot
[audio-demo] build ...
[audio-demo] USART1 ...
```

说明程序很可能卡在 `audio_player_init()` 之前或其中的早期阶段。曾经遇到过调试函数递归导致卡死的问题，表现就是只打印前三行。

## 12. 典型故障和定位方法

### 12.1 `pf_mount failed`

现象：

```text
audio_player_init=-3 err=pf_mount failed ...
sector0 sig 不是 AA55
```

优先排查：

1. SD 卡是否 FAT/FAT32 格式。
2. sector0 是否有 `AA55` 签名。
3. SDIO 读扇区是否稳定。
4. 是否读到了随机数据或重复旧数据。
5. SD 卡文件是否损坏。

已经验证过：重新整理 SD 卡和 MP3 文件后，挂载问题解决。

### 12.2 找不到 MP3

现象：

```text
find_mp3 != 0
err=no mp3 file found
```

优先排查：

1. MP3 是否放在根目录。
2. 文件扩展名是否为 `.mp3`。
3. 文件名是否太长。
4. 是否用了中文长文件名。
5. SD 卡是否存在隐藏分区或异常目录结构。

建议测试文件名：

```text
TEST.MP3
```

### 12.3 MP3 找到但没有声音

可能原因按链路顺序排查：

1. MP3 文件损坏，无法解码有效帧。
2. `frames_decoded` 为 0。
3. `board_audio_play_pcm()` 没有被调用。
4. I2S 没有输出 BCLK/LRCK/SD。
5. ES8311 I2C 配置失败。
6. ES8311 DAC/输出路径没打开。
7. 耳机接到了错误节点。
8. 功放使能逻辑反了。
9. 模拟输出硬件焊接或电源有问题。

判断软件是否已经把音频送出，重点看：

```text
frames_decoded > 0
pcm_blocks_sent > 0
tx_frames 持续增加
I2S 波形存在
```

如果这些都正常，但耳机仍无声，应优先转向 ES8311 模拟输出、电源、耳机座、耦合电容和焊接问题。

### 12.4 测试音有声音，MP3 没声音

测试音由 MCU 生成 PCM，不经过 MP3 解码和 SD 文件内容判断。

测试音有声说明：

- I2S 输出大概率正常。
- ES8311 播放链路大概率正常。
- 耳机或功放后级大概率正常。

MP3 没声则重点排查：

1. MP3 文件是否损坏。
2. MP3 是否能解码出有效帧。
3. `frames_decoded` 是否增加。
4. `last_sample_rate` 和 `last_channels` 是否合理。
5. 解码速度是否跟得上。

之前曾确认过：损坏 MP3 会导致播放失败，换新的 MP3 后能正常解码。

### 12.5 只有滋滋声，不像音乐

可能原因：

1. 把压缩 MP3 数据误当 PCM 输出。
2. 解码器输出数据格式不匹配。
3. 声道数处理错误。
4. I2S 位宽或左右声道时序错误。
5. 解码速度太慢导致 DMA underrun。
6. 串口调试过多影响实时性。
7. 文件损坏导致大量错误帧。

当前工程最终解决的关键点是：

- 不再使用不适合 Cortex-M3 的慢速浮点解码路径。
- 使用 Helix 定点 MP3 解码。
- 使用 DMA 环形缓冲持续供给 I2S。
- 降低运行期调试打印频率。
- 开启编译器优化。

### 12.6 音乐像 0.1 倍速

这通常不是音频内容问题，而是实时性问题。

重点看：

```text
gap_pct
max_decode_us
dma_under
dma_used
```

如果解码耗时接近或超过音频时长，I2S 会持续等不到新 PCM，听起来就像慢速、卡顿、断续。

解决方向：

1. 使用定点解码器。
2. 开启 O2/O3 优化。
3. 减少串口打印。
4. 使用 DMA，避免 CPU 阻塞写 I2S。
5. 提高 SD 读取效率。
6. 增大缓冲。

### 12.7 成品模块有声音，自研板无声音

如果同一套软件在成品 ES8311 + NS4150B 模块上有声音，而自研板无声音，软件已经具备较强可信度。

此时应重点排查硬件：

1. ES8311 电源是否正确。
2. ES8311 模拟电源和数字电源是否都稳定。
3. MCLK/BCLK/LRCK/SD 是否到达 ES8311 引脚。
4. I2C 是否到达 ES8311，引脚是否上拉。
5. ES8311 地址脚状态是否和软件地址一致。
6. 耳机输出脚是否接对。
7. 耳机座检测脚是否误接导致断开。
8. 耳机输出是否需要隔直电容。
9. 模拟地和数字地连接是否合理。
10. ES8311 输出到功放/耳机路径是否有断点、短路、虚焊。
11. 后级功放 EN 是否被三极管反相。
12. 自研板是否缺少成品模块上的必要偏置、电容或滤波。

判断方法：

- 用逻辑分析仪确认 I2S 到 ES8311。
- 用示波器看 ES8311 模拟输出脚是否有音频波形。
- 成品模块和自研板交叉连接同一组 I2S/I2C。
- 如果 I2S/I2C 正常但 ES8311 输出脚无波形，重点查 ES8311 电源、寄存器、焊接和外围。
- 如果 ES8311 输出脚有波形但耳机无声，重点查耳机座和后级模拟链路。

## 13. 逻辑分析仪检查方法

用户当前逻辑分析仪通道曾配置为：

```text
CH0 -> PC7  MCLK
CH1 -> PB3  BCLK
CH2 -> PA15 LRCK/WS
CH3 -> PB5  SD
```

推荐分析方法：

1. 设置通道名称，避免后续看错信号。
2. 采样率至少高于最高被测信号 5 到 10 倍。
3. 先看 MCLK 是否连续。
4. 再看 LRCK 频率是否接近 44.1 kHz 或 48 kHz。
5. 再看 BCLK 是否和 LRCK 成固定比例。
6. 最后看 SD 是否在播放期间有变化。

正常播放时：

- MCLK 应连续。
- BCLK 应连续。
- LRCK 应稳定翻转。
- SD 应随音乐变化。

异常判断：

| 现象             | 可能原因                                                |
| ---------------- | ------------------------------------------------------- |
| 没有 MCLK        | I2S MCLK 未开、引脚配置错误、外设未启动                 |
| 没有 BCLK/LRCK   | SPI3/I2S 没启动、时钟没使能、引脚复用错误               |
| 有时钟但 SD 全 0 | 没有 PCM 写入、DMA 没启动、解码失败、音量或静音处理错误 |
| SD 有数据但无声  | ES8311 配置或模拟输出硬件问题                           |
| LRCK 频率不对    | I2S 采样率设置或系统时钟配置问题                        |
| SD 数据断续      | DMA underrun、解码太慢、SD 读取太慢                     |

## 14. 当前最终有效配置总结

当前能正常播放音乐的关键配置如下：

| 项目         | 当前值                        |
| ------------ | ----------------------------- |
| MCU          | STM32F103ZET6                 |
| Codec        | ES8311                        |
| 存储         | SD 卡                         |
| 文件系统     | Petit FatFs                   |
| 音频格式     | MP3                           |
| MP3 解码器   | Helix fixed-point MP3 decoder |
| I2S 外设     | SPI3/I2S3                     |
| I2S 模式     | MasterTx                      |
| I2S 标准     | Philips                       |
| I2S 数据宽度 | 16 bit                        |
| I2S MCLK     | Enable                        |
| PCM 输出     | 16 bit                        |
| 声道策略     | 双声道转单声道后左右同发      |
| 软件音量     | 10%                           |
| DMA          | DMA2_Channel2 circular        |
| DMA 缓冲     | 4096 halfwords                |
| 功放使能     | PC13 低电平有效               |
| 编译优化     | O3                            |
| MP3 文件位置 | SD 根目录                     |
| 文件名建议   | 8.3 格式，例如`TEST.MP3`    |

## 15. 为什么当前方案能稳定播放

当前方案稳定的原因不是某一个点，而是整条链路都满足了实时音频播放要求：

1. SD 卡能稳定读出 FAT 文件系统数据。
2. MP3 文件本身有效。
3. Helix 定点解码速度足够 Cortex-M3 实时运行。
4. I2S 根据 MP3 帧采样率选择 44.1 kHz 或 48 kHz。
5. DMA 环形缓冲让 I2S 连续输出，不依赖 CPU 每个采样点轮询。
6. 软件音量降低到 10%，避免后级过载。
7. PC13 功放使能逻辑按硬件反相修正。
8. 调试输出控制在低频，避免串口打印破坏实时性。
9. Keil 开启较高优化等级。

其中最关键的三个点：

```text
Helix 定点解码 + DMA 连续输出 + 正确 I2S/ES8311 配置
```

缺少任意一个，都可能出现“有数据但听不到正常音乐”的问题。

## 16. 后续维护注意事项

### 16.1 不要随意增加高频串口打印

播放循环中频繁 `printf` 会严重影响实时性。尤其是每帧、每次读卡、每次写 PCM 都打印，会导致：

- 解码来不及。
- DMA 缓冲被播空。
- 声音卡顿。
- 音乐变慢。

建议只保留：

- 初始化关键日志。
- 每隔固定帧数的进度日志。
- 出错时的详细日志。
- 播放结束后的统计日志。

### 16.2 修改音量要先确认后级能力

当前 10% 是安全值。调大音量前先确认：

- 耳机输出无明显破音。
- 功放输入不过载。
- 扬声器额定功率足够。
- 电源不会因瞬态电流跌落。

### 16.3 修改 I2S 配置要同步检查 ES8311

I2S 改动会影响：

- MCLK 频率。
- BCLK 频率。
- LRCK 频率。
- 数据位宽。
- 左右声道对齐。

ES8311 的数字音频接口也必须匹配这些设置。

### 16.4 修改 DMA 缓冲要考虑 RAM

增大 DMA 缓冲能提高抗抖动能力，但会占用 RAM。

当前 4096 halfwords：

```text
4096 * 2 bytes = 8192 bytes
```

再加上：

- MP3 stream buffer
- PCM buffer
- Helix 静态解码 buffer
- 栈
- 全局变量

因此修改缓冲大小时要关注 Keil map 文件和实际栈空间。

### 16.5 修改 MP3 文件支持能力

当前只找根目录第一个 `.mp3`。若要产品化，需要考虑：

- 子目录扫描。
- 长文件名。
- 中文文件名。
- 播放列表。
- 上一首/下一首。
- 暂停/继续。
- 音量按键。
- ID3 标签跳过和解析。

## 17. 推荐排查顺序

以后如果再次出现“没声音 / 噪声 / 卡顿”，建议严格按以下顺序排查：

1. 串口是否进入 main，是否打印 boot。
2. `audio_player_init()` 是否返回 0。
3. SD 初始化是否 `init ok`。
4. `pf_mount` 是否成功。
5. `find_mp3` 是否成功。
6. MP3 是否能解码，`frames_decoded` 是否增加。
7. `last_sample_rate`、`last_channels` 是否合理。
8. `tx_frames` 是否增加。
9. `dma_under` 是否增加。
10. 逻辑分析仪看 MCLK/BCLK/LRCK/SD。
11. ES8311 I2C 寄存器是否能读回。
12. 示波器看 ES8311 模拟输出。
13. 检查耳机座、功放、三极管使能、电源和焊接。

这条顺序的原则是：

```text
先证明数据能读到
再证明数据能解码
再证明 PCM 能送出
再证明 I2S 到达 Codec
最后检查模拟输出和功放链路
```

## 18. 当前工程完成程度

以“从 SD 卡播放 MP3 音乐”为目标，当前软件链路已经打通：

- SDIO 读卡：已打通。
- FAT 挂载：已打通。
- MP3 文件查找：已打通。
- MP3 解码：已打通。
- PCM 输出：已打通。
- I2S3 输出：已打通。
- DMA 连续播放：已打通。
- ES8311 播放配置：已验证能出声。
- 音量控制：已配置为 10%。
- 功放使能反相：已修正。

仍可继续优化的方向：

1. SDIO 改为 4-bit + DMA，提高读卡余量。
2. 支持长文件名和子目录扫描。
3. 保留真正立体声输出模式。
4. 增加播放控制状态机。
5. 增加按键或串口命令控制。
6. 进一步整理 ES8311 寄存器表，对照 datasheet 注释每个寄存器含义。
7. 将调试等级分级，区分 bring-up、性能、发布三种日志模式。

## 19. 一句话结论

当前音乐播放成功的本质是：SD 卡提供稳定 MP3 字节流，Helix 在 Cortex-M3 上以足够速度解码成 PCM，DMA 保证 I2S3 连续把 PCM 送入 ES8311，ES8311 再把数字音频转换成耳机或功放可以使用的模拟音频。
