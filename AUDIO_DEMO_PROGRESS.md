# STM32F103ZET6 音频验证 DEMO 进度记录

## 1. 当前目标

本项目的目标是为 `STM32F103ZET6` 开发板准备一个可逐步落地的音频验证 DEMO，最终希望实现：

- 从 SD 卡读取音频文件并播放
- 使用板载麦克风进行录音验证
- 在 MCU 侧完成必要的软件处理
- 通过板载 `ES8311 + NS4150B` 音频链路完成输入输出闭环

## 2. 当前最重要的上板验证方法

### 2.1 MP3 播放怎么验证

当前程序默认会自动播放：

- `test.mp3`

你上板时只需要：

1. 把一个可正常播放的 `test.mp3` 放到 SD 卡根目录
2. 上电运行程序
3. 观察是否能从喇叭听到声音
4. 打开串口观察播放统计信息

### 2.2 录音怎么验证

当前录音 DEMO 不需要按键触发，程序会自动执行。

当前流程是：

1. 先自动播放 `test.mp3`
2. 随后自动切到录音模式
3. 录大约 `1` 秒单声道 PCM 到 RAM
4. 录完后自动切回播放模式
5. 将刚才录到的内容立即回放出来

所以你上板验证录音时的实际感受应该是：

- 先听到 `test.mp3`
- 然后对着麦克风说话或发声约 `1` 秒
- 接着喇叭会把你刚才录下的声音回放出来
- 串口会打印录音结果与收发帧统计

### 2.3 当前录音 DEMO 参数

- 采样率：`8 kHz`
- 声道：`1` 声道
- 录音时长：约 `1` 秒
- 当前用途：验证录音链路是否打通，不追求高音质

## 3. 串口调试信息

### 3.1 当前串口实现

已新增：

- `src/debug_uart.c`
- `src/debug_uart.h`

当前使用：

- `USART1`
- `PA9`：TX
- `PA10`：RX
- 波特率：`115200`
- 格式：`8N1`

### 3.2 当前串口打印内容

上电后会打印：

- boot 信息
- `audio_player_init()` 返回值
- MP3 文件名
- MP3 播放结果
- MP3 播放统计信息
- 板级音频播放调试信息
- 录音 DEMO 开始提示
- 录音 DEMO 结果
- 录音调试信息
- 板级音频录音/回放调试信息

### 3.3 串口里重点看什么

MP3 播放阶段重点看：

- `play_result`
- `err=`
- `bytes_read`
- `frames_decoded`
- `frames_skipped`
- `pcm_blocks`

录音阶段重点看：

- `record_result`
- `samples_per_channel`
- `tx_frames`
- `rx_frames`
- `last_mode`
- `last_error`

## 4. 当前 LED 建议含义

- `1` 次闪烁：上电启动
- `2` 次闪烁：音频初始化完成
- `3` 次闪烁：SD 卡挂载完成
- `4` 次闪烁：MP3 播放阶段完成
- `5` 次闪烁：录音 DEMO 完成
- `6` 次闪烁：严重错误

## 5. 当前软件进度

当前进度约为 `98%`。

### 5.1 已完成

- MP3 自动播放验证链路
- 录音到 RAM 再回放验证链路
- 播放统计信息
- 录音统计信息
- 板级收发调试信息
- 串口打印调试信息
- Host-side 单元测试

## 6. Host-side 单元测试情况

当前执行命令：

```bash
python tests/host/run_host_tests.py
```

当前应通过以下测试：

- `host-audio-player-tests`
- `host-diskio-tests`
- `host-record-tests`

## 7. 当前关键文件

- `src/main.c`
- `src/debug_uart.c`
- `src/debug_uart.h`
- `src/audio_player.c`
- `src/audio_player.h`
- `src/audio_record.c`
- `src/audio_record.h`
- `src/board_audio.c`
- `src/board_audio.h`
- `src/sdcard_stub.c`
- `src/pff/`
- `tests/host/`