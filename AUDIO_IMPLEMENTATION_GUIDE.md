# STM32F103ZET6 音频播放与录音实现说明

## 1. 文档目的

这份文档主要说明当前工程里“音频播放”和“录音验证”两项功能是怎么实现的，包括：

- 用到了哪些知识点
- 涉及哪些模块
- 每个模块做了什么配置
- 模块之间如何配合
- 当前驱动代码各自负责什么
- 后续上板时应该从哪里排查

这份文档不是进度记录，而是偏“实现原理说明”和“维护说明”。

## 2. 当前实现目标

当前工程已经实现或基本实现了以下两项功能：

1. 从 SD 卡根目录读取 `test.mp3`，解码后经 `ES8311 + NS4150B` 播放
2. 使用板载麦克风通过 `ES8311` 采集音频，录到 RAM 后再立即回放

说明：

- 当前播放功能是真正以 `MP3` 为目标格式
- 当前录音功能主要用于验证板载录音链路是否打通
- 当前录音结果不写入 SD 卡文件，只做“录到 RAM 再回放”

## 3. 用到的核心知识

要实现当前这两个功能，主要涉及以下知识点。

### 3.1 MCU 外设基础

- GPIO 配置
- RCC 时钟使能
- SPI/I2S 外设初始化
- USART 串口初始化
- SDIO 外设初始化

### 3.2 音频数字接口

- I2S 主发送模式
- I2S 主接收模式
- 双声道/单声道 PCM 数据组织方式
- 采样率、位宽、左右声道时序

### 3.3 音频 Codec 基础

- Codec 通过 I2C 写寄存器完成初始化
- 播放时使用 DAC 路径
- 录音时使用 ADC 路径
- 模拟输入、模拟输出、数字音频输入输出之间的切换

### 3.4 文件系统与存储

- SD 卡底层扇区读取
- FAT 文件系统挂载
- 从文件中连续读取压缩音频数据

### 3.5 音频解码

- MP3 帧同步
- MP3 帧解码得到 PCM
- PCM 数据推送到 I2S 输出

### 3.6 嵌入式调试

- LED 阶段指示
- 串口日志输出
- 统计信息记录
- Host-side 单元测试

## 4. 板级硬件关系

根据原理图，当前板子的音频硬件链路可以分成两条。

### 4.1 播放链路

`STM32 -> I2S3 -> ES8311 -> NS4150B -> 喇叭`

主要信号：

- `PC7` -> `I2S3_MCK`
- `PB3` -> `I2S3_CK`
- `PA15` -> `I2S3_WS`
- `PB5` -> `SPI3_MOSI / DSDIN`

控制接口：

- `PB6` -> `I2C1_SCL`
- `PB7` -> `I2C1_SDA`

功放控制：

- `PC13` -> `CTRL`

### 4.2 录音链路

`MIC1 -> ES8311 ADC -> ASDOUT -> PB4/SPI3_MISO -> STM32`

主要信号：

- `PB4` -> `SPI3_MISO / ASDOUT`

说明：

- 麦克风模拟信号先进 `ES8311`
- `ES8311` 的 ADC 把模拟音频转换成数字音频
- 数字音频再经 I2S 数据线送回 STM32

## 5. 软件模块划分

当前工程里和音频播放/录音直接相关的模块如下。

### 5.1 [board_audio.c](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/board_audio.c)

这是板级音频驱动入口，负责最底层硬件相关工作。

主要职责：

- GPIO 初始化
- 软件 I2C 驱动
- `ES8311` 寄存器配置
- I2S 播放初始化
- I2S 录音初始化
- PCM 播放接口
- PCM 采集接口
- 板级调试统计

对应头文件：

- [board_audio.h](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/board_audio.h)

### 5.2 [audio_player.c](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/audio_player.c)

这是播放功能的上层控制模块。

主要职责：

- 初始化播放相关子模块
- 打开 SD 卡音频文件
- 连续读取 MP3 数据
- 调用 `minimp3` 解码
- 把 PCM 送给 `board_audio`
- 输出播放统计信息

对应头文件：

- [audio_player.h](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/audio_player.h)

### 5.3 [audio_record.c](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/audio_record.c)

这是录音 DEMO 控制模块。

主要职责：

- 初始化录音模式
- 从 `board_audio` 采 PCM 到 RAM
- 切回播放模式
- 回放刚录到的 PCM
- 输出录音调试信息

对应头文件：

- [audio_record.h](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/audio_record.h)

### 5.4 [sdcard_stub.c](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/sdcard_stub.c)

这是 SD 卡底层访问模块。

主要职责：

- 初始化 SDIO
- 发送 SD 卡命令
- 读取扇区数据

说明：

- 当前仍然是“验证导向”的最小实现
- 重点是先把读卡路径打通

### 5.5 [src/pff/](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/pff)

这是 `Petit FatFs` 只读文件系统。

主要职责：

- FAT 文件系统挂载
- 文件打开
- 文件读取

当前限制：

- 只读
- 不能写文件

### 5.6 [debug_uart.c](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/debug_uart.c)

这是串口调试模块。

主要职责：

- 初始化 `USART1`
- 输出字符串
- 输出格式化调试信息

对应头文件：

- [debug_uart.h](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/debug_uart.h)

### 5.7 [main.c](/D:/Users/Admin/Desktop/STM32F103ZET6/stm32_rtthread_gcc-template/src/main.c)

这是整个 DEMO 的主控制流程。

主要职责：

- LED 初始化
- 串口初始化
- 调用播放初始化
- 自动播放 `test.mp3`
- 自动执行录音 DEMO
- 输出阶段日志与统计信息

## 6. 播放功能是怎么串起来的

### 6.1 初始化顺序

播放功能的初始化顺序是：

1. `main()` 调 `audio_player_init()`
2. `audio_player_init()` 调 `board_audio_init()`
3. `board_audio_init()` 实际调用播放初始化：
   - GPIO 初始化
   - I2S 播放模式初始化
   - `ES8311` 播放寄存器配置
4. `audio_player_init()` 再初始化：
   - `sdcard_stub`
   - `pf_mount`
   - `minimp3`

### 6.2 实际播放流程

播放时序如下：

1. `main()` 调 `audio_player_play_from_sd("test.mp3")`
2. `audio_player_play_from_sd()` 根据扩展名识别为 `MP3`
3. 调 `audio_player_play_mp3_from_sd()`
4. 打开 SD 卡文件
5. 分块读取 MP3 数据到缓冲区
6. 调 `minimp3` 解码得到 PCM
7. 调 `board_audio_play_pcm()` 把 PCM 送入 I2S
8. `ES8311` 将数字音频转模拟
9. `NS4150B` 功放驱动喇叭发声

## 7. 录音功能是怎么串起来的

### 7.1 当前录音 DEMO 思路

当前录音 DEMO 不走文件保存，而是：

1. 录音
2. 存到 RAM
3. 立即回放

这是最适合快速验证硬件链路的方式。

### 7.2 实际录音流程

流程如下：

1. `main()` 调 `audio_record_demo_once()`
2. `audio_record_demo_once()` 调 `board_audio_init_record()`
3. `board_audio_init_record()`：
   - GPIO 初始化
   - I2S 接收模式初始化
   - `ES8311` 录音寄存器配置
4. 调 `board_audio_capture_pcm()` 从 I2S 收 PCM 到 RAM
5. 录完后再调 `board_audio_init_playback()`
6. 再调 `board_audio_play_pcm()` 回放刚才采到的数据

### 7.3 当前录音参数

当前录音 DEMO 参数如下：

- 采样率：`8 kHz`
- 声道：`1`
- 时长：约 `1` 秒

设置原因：

- 减少 RAM 占用
- 更适合当前“验证用”目标
- 更容易上板时肉耳判断是否录到声音

## 8. 当前主要配置点

### 8.1 I2C 配置

在 `board_audio.c` 里，`ES8311` 当前通过软件 I2C 配置：

- `PB6`：SCL
- `PB7`：SDA

通过 `es8311_write_reg()` 向 Codec 写寄存器。

### 8.2 I2S 配置

播放模式：

- `MasterTx`
- `16bit`
- `Philips I2S`
- `48kHz`

录音模式：

- `MasterRx`
- `16bit`
- `Philips I2S`
- 当前软件统计里录音 DEMO 按 `8kHz` 逻辑验证

说明：

- 这里是当前工程的“验证导向实现”
- 后续上板如果发现时钟/采样率不匹配，需要优先检查这里

### 8.3 SD 卡配置

当前通过 `SDIO` 访问 TF 卡。

相关模块：

- `sdcard_stub.c`
- `diskio.c`
- `pff.c`

用途：

- 给 MP3 播放提供只读文件访问

### 8.4 串口调试配置

当前调试串口：

- `USART1`
- `PA9`：TX
- `PA10`：RX
- `115200`
- `8N1`

## 9. 当前有哪些调试信息

### 9.1 LED 调试

- `1` 次闪：启动
- `2` 次闪：音频初始化完成
- `3` 次闪：SD 卡挂载完成
- `4` 次闪：MP3 播放阶段完成
- `5` 次闪：录音 DEMO 完成
- `6` 次闪：严重错误

### 9.2 播放统计

来自 `audio_player_get_stats()`：

- `bytes_read`
- `decode_calls`
- `frames_decoded`
- `frames_skipped`
- `pcm_blocks_sent`
- `last_sample_rate`
- `last_channels`

### 9.3 板级音频统计

来自 `board_audio_get_debug_info()`：

- `tx_frames`
- `rx_frames`
- `last_sample_rate`
- `last_channels`
- `last_error`
- `last_mode`

### 9.4 录音统计

来自 `audio_record_get_debug_info()`：

- `samples_per_channel`
- `channels`
- `sample_rate`
- `last_result`

### 9.5 串口日志

启动后会打印：

- boot
- 音频初始化结果
- 播放结果
- 播放统计
- 录音开始提示
- 录音结果
- 录音统计

## 10. 当前驱动各自负责什么

可以简单理解为：

- `board_audio.*`
  负责“真正碰硬件”

- `audio_player.*`
  负责“读 MP3、解码、播放控制”

- `audio_record.*`
  负责“录音 DEMO 控制”

- `sdcard_stub.*`
  负责“SDIO 读卡”

- `pff/*`
  负责“只读文件系统”

- `debug_uart.*`
  负责“把状态打印出来”

- `main.c`
  负责“把这些模块按顺序串起来”

## 11. 模块之间的关系

可以用下面这个结构理解：

### 11.1 播放关系

`main`
-> `audio_player`
-> `sdcard_stub + pff + minimp3`
-> `board_audio`
-> `ES8311`
-> `NS4150B`
-> `speaker`

### 11.2 录音关系

`main`
-> `audio_record`
-> `board_audio`
-> `ES8311 ADC`
-> `PB4/SPI3_MISO`
-> `RAM buffer`
-> `board_audio`
-> `speaker`

## 12. 当前限制

当前还需要明确这些限制：

- `MP4/M4A` 不作为当前真实播放目标
- 文件系统是只读的，当前不能把录音写成文件
- 录音 DEMO 当前是“验证链路”，不是最终产品级录音方案
- `board_audio` 当前更偏“板级 bring-up 驱动”，不是通用音频框架

## 13. 后续如果要继续演进

如果后面要继续往产品化方向推进，建议顺序如下：

1. 上板验证当前播放和录音链路
2. 确认 `ES8311` 播放/录音寄存器是否完全匹配硬件
3. 确认 I2S 接收/发送实际时序
4. 若要保存录音，再升级文件系统到可写方案
5. 若要做更长录音，再引入更合适的缓冲或 DMA 机制
6. 若要做更完整的音频系统，再把播放/录音状态机进一步拆分

