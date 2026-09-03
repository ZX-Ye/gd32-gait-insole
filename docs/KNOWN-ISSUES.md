# 已知问题

这些问题是我们自己逐文件核查代码后整理出来的，**不是别人挑的刺**。列在这里是因为：
一个还没解决的问题被明确指出，比被含糊带过更有价值。

共 75 条。欢迎提 PR，也欢迎开新 issue 补充。

| 分类 | 含义 |
|---|---|
| 缺陷 | 行为与预期不符，会实际影响使用 |
| 不一致 | 多处实现／注释／文档互相矛盾，需统一口径 |
| 可改进 | 当前能工作，但设计上有更好的做法 |
| 待清理 | 死代码、编码混乱、命名误导等卫生问题 |


---

## 缺陷（33 条）

### 1. 44 字节通知在 Central 未协商 MTU 时会被静默丢弃，一个包都收不到

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`peripheralChar4Notify()` 在 `left/APP/peripheral.c:744-748`（右 :713-717）里判 `if(len > (peripheralMTU - 3)) { PRINT("Too large noti\n"); return; }`。而 `peripheralMTU` 初值是 `ATT_MTU_SIZE`（23，见 :126），并且**每次建链都会被复位回 23**（`Peripheral_LinkEstablished()` :637）。CH583 侧只在收到 `ATT_MTU_UPDATED_EVENT` 时被动记录新值（:612-615），**自己从不发起 MTU 交换**。

影响：44 > 23 - 3 = 20，所以从连接建立那一刻起，除非 GD32VW553 双 Central 主动发 Exchange MTU Request 且把 MTU 抬到 **≥ 47**，否则每 12.5 ms 丢一个包、80 Hz 全丢，串口只会以 80 Hz 刷 `Too large noti`（这行打印本身还会把 UART 塞死）。这是一条跨子系统的隐式契约，必须在 README 里写明，也建议在代码里加一条：MTU 未达标时直接不采样、并降频打印一次告警。

### 2. MacAddr[6] 是死代码，左右脚 MAC 常量从未生效

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`left/APP/peripheral_main.c:32-34`：
```c
#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};  // 右脚末字节 0x03
#endif
```
证据：`readelf -sW left_insole2.0/obj/APP/peripheral_main.o` 的 394 条符号里没有 `MacAddr`（只有 `UART0_Printf`/`Main_Circulation`/`main`/`MEM_BUF`），`obj/Peripheral.map` 全文也搜不到 `MacAddr` —— 说明作者实际使用的 SDK `CONFIG.h` 里 `BLE_MAC` 不是 TRUE，整个条件块被预处理掉了，两只鞋垫跑的是芯片出厂 MAC。

影响：任何"按 MAC 白名单区分/绑定左右脚"的上游逻辑都不会按预期工作；当前左右脚在空中**唯一**的区分依据是扫描响应包里名字的末字节 `'L'`/`'R'`（左 :228 / 右 :215）。修法见 patches 第 2 条（改 SDK 的 `CONFIG.h`），或者干脆把这段死代码删掉、在文档里明确"按广播名区分"。

### 3. 左脚通道映射表 LEFT_FOOT_MAP 疑似从未标定完，作者自己留了 TODO

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`left/APP/peripheral.c:140-143`：
```c
static const uint8_t LEFT_FOOT_MAP[16] = {
    3, 4, 2, 5, 0, 11, 8, 1, 15, 7, 10, 14, 6, 9, 12, 13 
    // TODO: 拿着左脚鞋垫按压测试，把这里的 0-15 替换成你实际测出的通道号
};
```
这张表的作用是抵消硬件走线镜像（注释 :136-138："index 代表标准输出顺序，value 代表对应的 ADC 真实通道"）。作者自己的 TODO 说明这 16 个数字**可能还是占位/待实测**。对比右脚的 `TX_SENSOR_MAP`（右 :678-681，`{1,5,4,0,15,10,11,14,8,7,13,2,6,12,3,9}`）是完全不同的排列且没有 TODO。

影响：如果左脚这张表没标定完，那么左脚 44 字节包里 32 字节压力段的通道顺序就是错的——下游的压力热力图会画错位，更要紧的是喂给 GD32H737 双流 1D-CNN 的特征向量维度顺序与训练时可能不一致。发布前应当用「单点按压 → 看哪个索引跳」的方法把左脚这张表重测一遍并把 TODO 删掉；重测前不要声称压力通道映射是正确的。

### 4. 零载荷底噪硬编码成 1530，16 路共用、左右脚共用，且会溢出成负压力

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`left/APP/peripheral.c:719`：`packet.adc[i] = (int16_t)(current_adc_values[LEFT_FOOT_MAP[i]] - 1530);`（右 :688 同）。1530 是一个拍出来的固定偏置，对全部 16 路、左右两只鞋垫、任意温度和任意电池电压都用同一个值，代码里也没有任何开机置零（tare）或按通道存偏置的机制。

影响：(a) 空载读数低于 1530 的通道会输出负值，下游若把压力当无符号或直接求和会失真；(b) 柔性压阻的零点随温度和长期压缩会漂移，固定偏置意味着漂移全部进了模型输入；(c) 左右脚硬件差异被抹掉。建议改成开机静置若干周期、按通道求均值存进 `int16_t zero_offset[16]`，或至少把 1530 提成可配置宏并在文档里写清它的来历。

### 5. 热路径上遗留了两条 DEBUG 级 dbg_print，会直接压垮 40 Hz 输出节拍

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/ble/app/app_conn_mgr.c:886` 在 `insole_data_recv_cb` 里对**每一条**来自任意一只鞋垫的 BLE Notify 打印一行 `DEBUG: Recv Tag:%c, Len:%d, ConnIdx:%d`；`接收/app/main.c:320` 在 40 Hz 节拍任务里对**每一帧**打印一行 `DEBUG: Sending Packet! LeftConnected:%d, RightConnected:%d`。

证据：作者自己在两处都标了「🚨 调试代码」，说明是临时加的没删。前者的注释写着「查看到底收没收到右脚数据」，是排查期的产物。

影响：两条链路 20 ms 连接间隔 → 每秒最多 100 条 Notify，每条一行日志；再叠上每秒 40 行发帧日志。`dbg_print` 在 SDK 里是走 LOG_UART 的（接收板上是 USART0），一行几十字节的日志在常见 115200 波特率下要 3-6 ms，直接把 `uart_tx_40hz_task` 的 25 ms 预算吃掉一大半，而 `insole_data_recv_cb` 是跑在 BLE 协议栈任务上下文里的，它被日志阻塞会反压整条 GATT 接收链。这基本可以解释「PA5 指示灯闪得不均匀」这类现象。开源前应删掉或用编译开关包起来（如 `#if VW553_VERBOSE_DEBUG`）。

### 6. 上位机把 ADC 当 uint16 解，与固件的 int16 定义不符，负压值会被读成 6 万多

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

固件侧 `接收/app/main.c:205` 和 `:207` 明确定义为 `int16_t left_adc[16]` / `int16_t right_adc[16]`，作者还专门注释「🚨 改为 int16_t，完美容纳 -4096」，说明负值是**预期会出现**的。

但上位机 `4.工程代码/5.上位机代码/smart_insole_display10.py:40` 的 `PACKET_FMT = '<B I 16H 6h 16H 6h B'` 里两段 ADC 用的是 `16H`（**uint16**），只有 IMU 用了 `6h`（int16）。同一脚本 `:28` 和 `:30` 的注释块也把字段写成 `uint16_t left_adc[16]`，即注释和固件也对不上。

影响：任何一路压阻读出负值（作者预期到了 -4096 这一量级），上位机会显示成 61440 附近的巨大正数，热力图和后续判据全部失真。修法二选一：把上位机 `PACKET_FMT` 改成 `'<B I 16h 6h 16h 6h B'`，或者在固件侧确认 ADC 永不为负后改回 uint16 并同步删掉那句注释。因为跨了两个子系统，建议开成一个 issue 由两边一起改。

### 7. g_sync_packet 无任何互斥保护，40 Hz 快照会撕裂出左右脚时间不一致的帧

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/app/main.c:213` 的 `CombinedDataPacket g_sync_packet` 是全局共享的「仪表盘」：`ble_parse_task`（优先级 3）在 `:285-290` 逐个字段写它，`uart_tx_40hz_task`（优先级 2）在 `:317-327` 整体读它并发出去。两个任务之间**没有互斥量、没有临界区、没有双缓冲**。

证据：`:264` 和 `:267` 创建任务时优先级分别是 3 和 2，parse 任务优先级更高，可以在 tx 任务逐字节 `usart_data_transmit` 的 0.6 ms 窗口中间随时抢占并改写 `g_sync_packet`。

影响：发出去的 94 字节里可能前半是第 N 帧的左脚、后半是第 N+1 帧的右脚，而 timestamp 只有一个。对下游做双流 1D-CNN 的 H737 来说，这等于在训练/推理输入里随机注入了左右脚时间错位——这类错位对步态相位特征的破坏是系统性的。修法：加一个 `g_sync_packet` 的影子副本，parse 写影子、tx 在关中断或持 mutex 的极短窗口里 memcpy 一次再慢慢发。

### 8. 单脚掉线后，该脚的数据在包里永久保留最后一帧，下游无法分辨死活

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/ble/app/app_conn_mgr.c:449-457` 处理掉线：把对应的 `g_conn_idx_left`/`g_conn_idx_right` 置回 0xFF，只有**两只都断**才把 `g_is_ble_connected` 清 0。但 `g_sync_packet` 里那只脚的 `left_adc[16]`/`left_imu[6]` 字段**完全不动**。

同时 `接收/app/main.c:315` 的发送条件只看 `if (g_is_ble_connected)`，所以只要还有一只脚在线，94 字节帧就照发 40 Hz，掉线那半边一直重复最后收到的那一帧数值。

影响：下游 GD32F470/H737 看到的是一只脚「静止但有压力读数」，而不是「无数据」。对跌倒检测来说这是最坏的一种失效——一只鞋垫掉线时系统不会报警，反而会拿冻结的数据继续推理，可能推出一个看起来很正常的类别。修法：掉线时把该脚的数组清零或填一个哨兵值（如 INT16_MIN），并把左右脚在线位塞进包里（可以复用 timestamp 的高位或和上一条 issue 的包尾一起重新设计）。

### 9. 左右脚 MAC 硬编码在源码里，且已烧死在 image-all.bin，换鞋垫必须改代码重编

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/ble/app/app_conn_mgr.c:85-86` 把两只 CH583 的 MAC 直接写成 const 数组：`MAC_LEFT_FOOT = {0x75,0x26,0xCD,0x88,0x19,0x70}`、`MAC_RIGHT_FOOT = {0x72,0x26,0xCD,0x88,0x19,0x70}`（小端，即 70:19:88:CD:26:75 / 70:19:88:CD:26:72）。匹配逻辑在 `:110-111` 是纯 `memcmp`，没有任何回退路径：既不按广播名匹配（CH583 侧其实有 `GAPROLE_ADVERT_DATA`/`SCAN_RSP_DATA` 可用）、也不从 Flash 读配置（SDK 明明有 `app_flash.c`）、也不走 AT 命令配。

影响：这是第三方复现时**最容易卡死且最难自查**的一步——现象只是「上电后一直呼吸灯、什么都连不上」，日志里连 `🎯 捕获到智能鞋垫` 都不会出现，看不出是 MAC 不匹配还是射频/供电问题。而且字节序是反的，很容易填成正序而二次翻车。同一个问题让 `接收/image-all.bin` 对任何第三方都毫无用处（MAC 已编进二进制）。建议改进方向：改成按广播名 + 服务 UUID 0xFFE0 过滤，把「先连到的算左脚、后连到的算右脚」换成鞋垫在广播数据里自报左右标志；过渡期至少在 `:85` 上面加一段醒目注释说明字节序和修改方法。

### 10. g_is_connecting 连接锁没有超时看门狗，一次静默失败的握手就永久停止扫描

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/ble/app/app_conn_mgr.c:113` 捕获到目标 MAC 后立刻 `g_is_connecting = true` 并在 `:119` 调 `ble_scan_disable()` 停止扫描。这把锁只有两个解开点：`:466`（`BLE_CONN_STATE_CONNECTED`）和 `:430-434`（`BLE_INIT_STATE_IDLE` 且 `reason != BLE_ERR_NO_ERROR`）。

问题是 `:100-103` 的扫描回调开头就是 `if (g_is_connecting) return;`，而锁本身没有任何定时器兜底。如果 `ble_conn_connect()` 发起后既没连上、也没产生一个带非零 reason 的 IDLE 事件（例如鞋垫在广播和连接请求之间刚好断电、或底层把状态停在 STARTED/DISABLING 上），`g_is_connecting` 就永久为 true，扫描永久关闭，两只脚一只也连不回来，**只能靠断电重启**。

另外 `:430-434` 只在 `reason != BLE_ERR_NO_ERROR` 时才释放锁，reason 为 0 的正常 IDLE 落地时锁不释放（作者删掉了 SDK 原本在 DISABLING 分支里重试 `ble_conn_connect_cancel()` 的整段逻辑，见 diff 中被删的 `:317-336` 原始代码），这条路径同样会卡住。修法：加一个 3-5 秒的软件超时，超时就 `ble_conn_connect_cancel()` + 清锁 + 重开扫描。

### 11. 「发送」板的 MTU 不是协商出来的，是硬编码 509 加一个假的 3 秒定时器

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`发送/app/main.c:158-164`：连接建立后用 `connect_tick++` 计数，注释写「3000 x 1ms = 3秒」，到 3000 次就 `g_mtu_ready = true; g_mtu = 509;`，注释是「手机端协商后通常是 512，有效载荷 = 512-3 = 509」。

两个问题：(1) 这个「3 秒」是假的——主循环末尾是 `sys_ms_sleep(1)`（`:219`），再加上每轮的环形缓冲计算、`dbg_print` 和 `ble_datatrans_srv_tx` 的耗时，实际每轮远大于 1 ms，所以真实等待时间不确定且远超 3 秒。(2) 代码从不读 BLE 栈真实的 MTU 交换结果（SDK 有 `BLE_CONN_EVT_MTU_INFO` 之类的事件，`接收` 侧就是靠 `ble_gattc_mtu_update` 主动谈的），只是「猜」对端会给 512。

影响：如果上位机/手机协商出的 MTU 比假设值小，第一次发送会返回错误码 13，代码在 `:210-213` 把 `g_mtu` 降到 23（载荷 20 字节）并重试——94 字节的帧于是被拆成 5 段发，靠上位机的 0xAA 帧头搜索重组（`smart_insole_display10.py:501-506`）勉强能work，但吞吐骤降且加大丢帧概率。而且 `max_payload` 无论如何都被 `:191` 夹到 244，所以 509 这个数从头到尾没起作用，属于误导性代码。修法：注册 MTU 事件回调，拿真实值。

### 12. 「发送」板环形缓冲无溢出检测，BLE 断开期间 DMA 会静默套圈覆盖未发送数据

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`发送/app/main.c:28-29` 定义 4096 字节的 `ring_buf`，DMA 以循环模式（`:104` `DMA_CIRCULAR_MODE_ENABLE`）不停往里写；软件读指针 `head` 只在 BLE 发送成功时才前进（`:205` `head = temp_head;`）。整个文件里**没有任何一处比较 DMA 写指针是否已经追过 head**。

证据：`:182` 的发送条件是 `if (g_connected && g_mtu_ready)`，BLE 未连接时整个搬运逻辑被跳过，但 DMA 从 `uart_dma_init()` 起就一直在跑（`:111` `dma_channel_enable`）。上游是 1.5 Mbaud 常流，4096 字节只够 **约 27 ms**（4096×10/1500000 s）。

影响：只要上位机断开超过 30 ms（重连、切页、Python 端卡一下），缓冲就被套圈覆盖；重连后 `:183` 重新算出的 `len` 会是一个跨越覆盖点的长度，于是发出一段新旧混杂的字节流。上位机靠搜 0xAA 能自愈（`smart_insole_display10.py:501-518` 会丢弃找不到帧头的数据并计入 `error_count`），但期间会推送若干帧看起来合法、实际字段错位的数据（因为帧内某处正好被新数据覆盖，而头尾恰好还是 0xAA/0x55 的概率不低）。修法：断开时把 head 追平到当前 DMA 位置（丢弃积压），并在 avail 接近 RING_BUF_SIZE 时主动丢一整段。

### 13. insole_data_recv_cb 注释宣称「零栈内存消耗」，实际每条 Notify 在 BLE 栈任务上做 258 字节栈拷贝

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/ble/app/app_conn_mgr.c:872` 的函数头注释写「🚀 数据拦截器：极速模式 (零栈内存消耗，拒绝死机重启！)」，暗示作者曾因为这里爆栈重启过、并认为已经解决。

但它调用的 `user_trans_uart_send`（`接收/app/main.c:223-237`）第一行就是 `UartTxMsg_t msg;`——而 `UartTxMsg_t` 是 `{uint16_t len; uint8_t data[256];}`（`:75-78`），**258 字节的局部变量直接落在调用者的栈上**，紧接着 `:230` 还做一次 `memcpy` 填满它。`insole_data_recv_cb` 是被 BLE 协议栈的任务上下文调用的（通过 `:906` 的 `ble_gattc_svc_reg` 注册），这 258 字节吃的是 SDK BLE 任务的栈，不是作者自己创建的任务栈——作者控制不到它的大小。

影响：注释与实现严重不符，会误导后来的维护者以为这条路径是安全的；实际每条 Notify 都在别人的栈上开 258 字节，叠上同一函数里 `:886` 的 `dbg_print`（vsnprintf 自己也要几百字节栈），这正是「死机重启」的典型成因，问题很可能并未真正解决。修法：把 `UartTxMsg_t msg` 改成 `static`（需配合临界区，因为只有一个生产者上下文其实可行）或改用 `xQueueSendFromISR` 风格的零拷贝队列（队列项存指针 + 长度，数据留在 BLE 栈的缓冲里由消费者拷走）。

### 14. 工程里有两份同名 FreeRTOSConfig.h，改错那份完全不生效（堆 32 KB vs 70 KB）

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`freeRTOS/FreeRTOSConfig.h:110` 是 `configTOTAL_HEAP_SIZE ((size_t)(32*1024))`，`freeRTOS/include/FreeRTOSConfig.h:110` 是 `(70*1024)`；两份文件 diff 只差这一行。真正生效的是 include/ 那份——armcc 对 quoted include 先搜索包含者所在目录，而 `#include "FreeRTOSConfig.h"` 是从 `freeRTOS/include/FreeRTOS.h` 里发出的；实测 `Project/Listings/GD32F450.map:10022` 写着 `ucHeap … Data 71680 heap_4.o(.bss)`，71680 = 70 KB，确凿。雪上加霜的是 uvprojx 的包含路径顺序是 `..\freeRTOS;..\freeRTOS\include;`，根目录那份排在前面，看起来像是它在生效。影响：任何人为了给 LVGL 腾内存去调根目录那份的堆大小，都会得到"改了没反应"，然后在 `xTaskCreate` 返回失败上耗掉半天。建议：删掉 `freeRTOS/FreeRTOSConfig.h`，只留 include/ 那一份。

### 15. 固件英文标签 "BLIND PROBE"（空格）与上位机 "BLIND_PROBE"（下划线）不一致，导致部分高危类别的上位机告警着色不触发

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

固件表：`Hardware/lcd_my_test/lcd_mytest.c:36-39` = ANTALGIC / **BLIND PROBE** / **DOWNSTAIRS** / HEMIPLEGIC / **SITTING** / **STANDING** / **UPSTAIRS** / WALKING。上位机表：`4.工程代码/5.上位机代码/smart_insole_display10.py:683-685` = high_risk `["HEMIPLEGIC", "ANTALGIC", "BLIND_PROBE"]`、warning `["DOWNSTAIR", "UPSTAIR"]`、safe `["WALKING", "STAND", "SIT"]`。上位机是拿 Edge Impulse 返回的类名 `.upper()` 后做字符串 `in` 判断（:679-686），一旦模型标签写成 "blind probe"（与固件表一致的写法），`label_str in high_risk_classes` 直接为假，界面就退回默认样式——高危步态不会变成红底白字。同一处 DOWNSTAIRS/UPSTAIRS 与 DOWNSTAIR/UPSTAIR、SITTING/STANDING 与 SIT/STAND 也都对不上。全工程共有三张互不相同的标签表（固件中文、固件英文、上位机英文），没有任何单一真源。影响：演示时最该报警的类别反而不变色。

### 16. 一分钟风险窗按 30 Hz 数据帧累加，而 AI 结果每 15 帧才更新一次 —— 每个推理结果被重复计 15 次

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`User/main.c:198-215`：只要 `g_ai_result <= 7`，每个 33 ms 转发周期就 `g_1min_frame_counter++`，高危(0/1/3)时 `g_1min_abnormal_counter++`，满 1800 帧结算 `ratio = abnormal*100/1800`。但 H737 侧 `4.工程代码/1.GD32H737VMT6程序/h737vmt6_AI1.0/main.c:36` 是 `#define INFERENCE_STEP 15`，即每 15 帧（500 ms）才产生一个新分类。等于每个推理结论被计入 15 次，1800 帧的窗口实际只承载 **120 个独立推理**——分子分母都被同一个系数放大，比值本身没算错，但"一分钟 1800 帧统计"的口径是假的，统计有效样本量只有 120，单个误判会一次性贡献 0.83% 的风险率。另有两个附带问题：(a) H737 掉线后 `g_ai_result` 保留最后一个值（`bsp_usart.c:286` 只在收到完整包时写），风险窗会拿一个几分钟前的旧结论继续累加，没有任何超时清零；(b) 窗口只在第一个有效结果到达后才开始走，`g_abnormal_ratio` 初值 0xFF 期间屏幕显示 "RISK: CALC..."（`main.c:41`、`lcd_mytest.c:114/137`）。建议：改成"每收到一个新 AI 结果计一次"，窗口长度取 120，并加 1~2 s 的结果超时。

### 17. 压力数组符号不一致：VW553 声明 int16_t，F470 / H737 / 上位机三家都按 uint16_t 解码

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

发送端 `4.工程代码/3.GD32VW553程序/接收/app/main.c:205` 写的是 `int16_t left_adc[16];  // 🚨 改为 int16_t，完美容纳 -4096`；接收端 `4.工程代码/2.GD32F470ZGT6程序/final_wireless_v16.0/User/main.c:52-54` 是 `uint16_t left_adc[16]` / `uint16_t right_adc[16]`，H737 侧 `1.GD32H737VMT6程序/h737vmt6_AI1.0/main.c:54` 同样是 `uint16_t`，上位机也按无符号解。字节宽度一致所以链路不会错位，但只要 VW553 真的送出负值（它的注释明确说是为了容纳 -4096），F470 屏上和 H737 输入端就会读成 61440 附近的巨大正数，直接污染推理输入的量纲。三家必须统一（要么全 int16_t，要么在 VW553 侧做钳位后再发 uint16_t）。注意 IMU 六轴三家都是 int16_t，只有压力数组不一致。

### 18. 转发给上位机的包尾恒为 0x55 —— H737 的推理结果并没有回写进转发流

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`User/main.c:143` 把 `latest_packet.tail = 0x55` 设一次；:171 的 `memcpy(&latest_packet, p_data, 94)` 又把 VW553 的原始包（尾字节也是 0x55）整份覆盖回来；:193 直接把 `latest_packet` 发给 UART3(PC) 和 USART1(H737)。而 H737 回传的分类结果只被 `bsp_usart.c:286` 取进 `g_ai_result` 给屏幕用，**从未写回 `latest_packet.tail`**（grep `tail` 在 main.c 只有 :56 和 :143 两处）。结果：PC 上位机收到的永远是尾字节 0x55 的"生数据"，拿不到端侧推理结论，只能自己再调一遍本地 Node.js + Edge Impulse（`smart_insole_display10.py:673-686`）——这也正是上位机存在第二张标签表、进而产生上面那个标签不匹配 bug 的根源。这与"推理结果覆写包尾沿原路回传"的架构描述不一致：回传只到了 F470 的屏幕，没有再往 PC 走一跳。修法：发送前插一行 `latest_packet.tail = (g_ai_result <= 7) ? g_ai_result : 0x55;`。

### 19. ready_buffer_id 与 ready_rx_len 之间没有原子性，长度可能配错缓冲区

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

消费侧 `User/main.c:156-160`：先判 `ready_buffer_id != 0` 并据此取 `rx_buffer_A/B` 的指针，再单独读 `ready_rx_len`，然后才把 `ready_buffer_id` 清零。生产侧 `Hardware/usart/bsp_usart.c:161-174` 在 IDLE 中断里成对更新这两个变量。UART4 的 ISR 抢占优先级是 3（`bsp_usart.c:71`），高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`（`FreeRTOSConfig.h:184`），也就是 `taskENTER_CRITICAL()` 屏蔽不了它——它可以精确落在 main.c:158 和 :159 之间，于是 task2 拿着 A 缓冲的指针配上了 B 缓冲的长度。另外乒乓只有两级：task2 若被 UI 任务耽误超过两个 IDLE 周期，正在解析的缓冲会被 DMA 就地改写（无"正在使用"标记）。1.5 Mbps 下 94 字节包间隔约 33 ms，平时撞不上，但表现出来就是偶发的丢帧/花帧，且无法复现。建议：用一个 `struct {char *buf; uint16_t len;}` 的两槽队列或 `xQueueSendFromISR` 一次性交接（后者需先把 ISR 优先级降到 5 以下）。

### 20. 训练脚本与部署固件版本漂移：仓库里的训练脚本是 21 帧/924 维，板上跑的是 45 帧/1980 维，45 帧那一版训练脚本不存在

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：
- 部署固件：`h737vmt6_AI1.0/main.c:33` `WINDOW_FRAMES 45`、`:34` `FEATURES_PER_FRAME 44`、`:35` `TOTAL_FEATURES = 1980`、`:36` `INFERENCE_STEP 15`、`:139` `user_output_size = 8*4`（8 类）；模型侧 `GD32H7_study/GD32_H7_AI/1.0/nn_model_configure.h:30` `INPUT_SIZE 1980`、`:35` `OUTPUT_SIZE 8`。
- 仓库里唯一的训练脚本：`4.工程代码/6.模型训练代码/Neural_network_architecture.py:20` 注释 `接收展平的特征输入 (Batch_size, 924)`、`:26-27` 注释 `模块1(ADC): 32轴 * 21帧 = 672` / `模块2(IMU): 12轴 * 21帧 = 252`、`:30` `Lambda(lambda t: t[:, 672:924])`、`:33-34` `Reshape((21, 32))` / `Reshape((21, 12))`。全是 **21 帧 / 924 维**。
- 只有被排除的备份文件 `h737vmt6_AI1.0/main - 副本.c`（6月2 11:00）与这份脚本对得上：它写的是 `WINDOW_FRAMES 21`、`INFERENCE_STEP 7`、`user_output_size = 6*4`（**6 类**，还不是 8 类）。

**影响**：第三方拿到本仓库无法复现板上跑的那个模型。要复现必须把脚本里所有出现 21 / 672 / 924 的地方改成 45 / 1440 / 1980，且脚本里的 `classes` 数要从当年的 6 类调到 8 类——但改完是否等价于作者当时真正训练的那一版，无从验证。

**建议**：把 45 帧版训练脚本补进仓库；补不出来就在 README 里明写"随仓库提供的训练脚本是 21 帧原型版，与固件不匹配，仅作结构参考"。

### 21. IMU 缩放系数不一致：训练说明写 0.000066，固件写 0.00024414f，差 3.7 倍

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：
- 训练侧：`4.工程代码/6.模型训练代码/使用说明.txt:2` —「前面是两个 rawdata，adc 的 Scale axes 是 0.002，imu 的 Scale axes 是 **0.000066**」。
- 固件侧：`h737vmt6_AI1.0/main.c:183` `rx_packet->right_imu[i] * 0.00024414f`、`:184` `rx_packet->left_imu[i] * 0.00024414f`。
- ADC 侧是对得上的：说明写 0.002，`main.c:181-182` 也是 `* 0.002f`。

**影响**：0.00024414 ≈ 1/4096（16 位 ±8g 的物理换算常数），0.000066 不是任何常见量程的换算值。若训练时真的用了 0.000066，那么板上送进网络的 12 个 IMU 通道被整体放大了约 **3.70 倍**，前 6 个卷积核看到的 IMU 分支输入分布与训练时完全不同——8 类里最依赖 IMU 的偏瘫/痛性跛行/盲态探步三类首先受影响。

**注意**：无法从仓库判定哪一侧是错的（上游模型文件 `.tflite/.h5/.eim` 不在仓库，无法回查 EI 项目的实际 Scale axes 设置）。**必须由作者去 Edge Impulse 项目里核对 IMU Raw Data block 的 Scale axes 实际填了什么**，然后把另一侧改正。

**附带**：两侧都是给 6 个 IMU 轴（3 轴加速度 + 3 轴角速度）套同一个缩放系数，加速度和角速度的量纲/量程根本不同，这本身也值得复核。

### 22. 左右脚通道顺序颠倒：训练数据 CSV 是「左脚在前」，固件装箱是「右脚在前」

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：
- 训练数据列序（左在前）：`4.工程代码/6.模型训练代码/采集的数据/downstair1.csv` 第 1 行列头 = `timestamp, L_ADC_1..L_ADC_16, R_ADC_1..R_ADC_16, L_IMU_AX..L_IMU_GZ, R_IMU_AX..R_IMU_GZ`；生成这些 CSV 的代码在 `4.工程代码/5.上位机代码/smart_insole_display10.py:596`：`current_frame_features = list(left_mapped) + list(right_mapped) + list(self.left_imu) + list(self.right_imu)`。
- 固件写帧顺序（右在前）：`h737vmt6_AI1.0/main.c:181` 先写 `right_adc[0..15]`、`:182` 再写 `left_adc[0..15]`、`:183` 先写 `right_imu[0..5]`、`:184` 再写 `left_imu[0..5]`。

**影响**：ADC 的 32 个通道被整体左右对调（通道 0-15 与 16-31 互换），IMU 的 12 个通道同样左右对调。也就是板上推理时，网络的"左脚"输入实际来自右脚。对左右对称的动作（STANDING、SITTING、WALKING）几乎无感，但**恰好会破坏本项目最看重的三类单侧异常步态**：ANTALGIC（痛性跛行，单侧承重减少）、HEMIPLEGIC（偏瘫，采集文件名 `band*` = 单腿绑负重）、BLIND PROBE（探步），它们的判别特征正是左右不对称。

**修复**：把 `main.c:181-184` 四行改成 left→right 顺序（与 CSV 一致），或反过来重训。改哪一侧要看 Edge Impulse 项目里 axis 的绑定顺序，建议以 CSV 列头为准改固件。

**附带核对**：上位机的 `remap_sensor_data()`（`smart_insole_display10.py:444-451`）目前是恒等映射（`:263-264` 两个 mapping 都是 0..15 顺序），所以 ADC 通道内部顺序两侧一致，问题只在左右整块的先后。上位机的 `window_size = 45` / `inference_step = 15`（`:335-336`）与固件一致，这部分没问题。

### 23. DMA 环形接收没有重同步机制，错帧仍会被当正常帧转发出去

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：`main.c:88` DMA 固定 94 字节、`:94` 循环模式，`it.c:18-22` 每满 94 字节就置 `data_ready`，帧边界完全靠上电时那次静默期对齐（`main.c:148-157`）。`main.c:160-168` 的恢复逻辑**只覆盖 USART 溢出（ORERR）**：停 DMA、重置传输数 94、重开。

**影响 1（丢帧后永久错位）**：如果链路上少了/多了单个字节而没有触发 ORERR（例如上游 F470 那边发送被打断），DMA 的 94 字节切分就永久偏移，`main.c:177` 的 `header == 0xAA` 从此帧帧不成立，推理彻底停摆，且代码里**没有任何重新对齐的路径**——只能断电重启。建议改用 USART 空闲线（IDLE）中断或 DMA 半传输+头字节搜索做重同步。

**影响 2（错帧照发）**：`main.c:177` 的 header 校验只包住写帧池那一段（`:178-228`）。校验失败时代码直接跳到 `:232`，把**没校验过的 rx_buffer 原样 memcpy 到 tx_buffer**，然后 `:235` 强行把首字节改成 `0xAA`、`:236` 覆写时间戳、`:237-241` 盖上包尾，再发出去。下游 F470 收到的是一个头尾都合法、载荷全是错位垃圾的包，无法分辨。建议 header 校验失败时直接 `continue`，不要转发。

**影响 3（无长度/校验保护）**：包结构里没有长度字段也没有 CRC/校验和（`main.c:51-59`），只有 1 字节固定头。单字节头在 1.5 Mbps 随机数据里约 1/256 概率误命中。

### 24. USART1_IRQHandler 已声明但从未实现；SysTick_Handler 也只落在 startup 的死循环桩上

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据 1**：`h737vmt6_AI1.0/gd32h7xx_it.h:8` 声明 `void USART1_IRQHandler(void); // 我们需要的串口桥接中断`，但全工程 grep 只有这一处命中，`gd32h7xx_it.c`（24 行，`:6-23`）里只实现了 NMI / HardFault / MemManage / BusFault / UsageFault / DMA0_Channel0_IRQHandler。这是上一代中断驱动版本留下的死声明。`main.c:120` 只使能了 `DMA0_Channel0_IRQn`，没使能 USART1 中断，所以目前无害——但把注释留在那儿会误导读者以为串口是中断驱动的（实际是 DMA + 主循环轮询）。

**证据 2（更值得留意）**：`GD32H7_study/GD32_H7_AI/1.0/systick.c` 被 uvprojx 编进了工程（`Objects/systick.o` 存在），它的 `systick_config()`（`systick.c:46-55`）和 `delay_1ms()`（`:64`）依赖 `SysTick_Handler` 去调 `delay_decrement()`（`:78`）。但 `gd32h7xx_it.c` 没有实现 `SysTick_Handler`，链接结果落到了 startup 的弱定义上——`Listings/GD32H737_Base.map:2242` 显示 `SysTick_Handler 0x08000493 Thumb Code 2 startup_gd32h7xx.o(.text)`，**2 字节，就是 `B .` 死循环**。

**影响**：目前 `main.c` 从不调用 `systick_config()` / `delay_1ms()`（全文件 grep 零命中，延时全用 `for(volatile int ...)` 空转，见 `main.c:147`、`:243`、`:248`），所以不会触发。但只要后来有人加一句 `delay_1ms()`，固件会在第一次 SysTick 中断时永久卡死在 startup 的死循环里，且现场极难定位。

**建议**：删掉 `gd32h7xx_it.h:8` 那行死声明；要么在 `gd32h7xx_it.c` 里补上 `void SysTick_Handler(void){ delay_decrement(); }`，要么把 systick.c 从工程里移除。

### 25. AI 推理链的特征展平顺序与模型期望不符：按帧交错 vs 按通道分块

**位置**：[`host-app`](../host-app)

这是这条 PC 推理链最根本的缺陷，且不会报任何错，只会静默输出垃圾分类。

上位机侧（smart_insole_display10.py:596）每一帧组装成 44 维向量 `[L_ADC×16, R_ADC×16, L_IMU×6, R_IMU×6]`，然后 :618 做 `[val for frame in self.feature_window for val in frame]` —— 帧优先展平，得到的排列是 `[帧0的32路ADC, 帧0的12路IMU, 帧1的32路ADC, 帧1的12路IMU, ...]`，ADC 与 IMU **逐帧交错**。

模型侧（Neural_network_architecture.py:24-30）的注释写得很清楚：「Edge Impulse 是把两个 Raw Data 模块首尾拼接的」，所以它做 `t[:, 0:672]` 取 ADC、`t[:, 672:924]` 取 IMU，期望的排列是 `[全部帧的ADC块 | 全部帧的IMU块]` —— **按通道分块**。

两种布局完全不同。切片 `[0:672]` 在上位机送出的向量里取到的是「前 15.27 帧的 ADC+IMU 混合体」，再 `Reshape((21,32))` 硬掰成 21×32 的矩阵，每一行都横跨了不同帧、不同模态的数值。卷积核学到的时序结构与推理时喂的东西毫无关系。

关键点：**即使把长度对齐了（见下一条 issue），这个错误依然存在**。它不是长度问题，是内存布局问题。修复需要把 :618 改成先按通道分块再展平：`[f[i] for i in range(32) for f in window] + [f[i] for i in range(32,44) for f in window]`——但要先确认 EI Raw Data 块内部到底是 frame-major 还是 channel-major，用一段已知 CSV 分别跑 EI 网页版 Live classification 与本地服务比对结果来验证，不要凭猜。

### 26. 上位机滑窗 45 帧（1980 维）与模型输入 924 维（21 帧）不匹配，注释还留着旧的 21/7

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:335-336 设 `window_size = 45`、`inference_step = 15`，:334 的注释却写着「核心修改 1：同步 WASM 模型参数」。:618 展平后送出 45 × 44 = **1980** 维。

Neural_network_architecture.py:21 的 `Input(shape=(input_length,))` 配合 :29-30 的 `0:672` / `672:924` 切片，硬编码 `input_length = 924`，即 **21 帧**（21×32=672，21×12=252）。

1980 ≠ 924。POST 过去后要么被推理服务按长度直接拒掉，要么被静默截断成前 924 个数（相当于只看前 21 帧的一半，且还是错位的）。

同一段代码里的注释还留着更早版本的参数，与代码完全脱节：
- :611「当队列刚满 21 帧时 (1500ms 窗口成型)」—— 代码是 45 帧；45×33 ms = 1485 ms，21 帧只有 693 ms
- :615「每滑动 7 帧触发一次 WebAssembly 预测」—— 代码是 15 帧

看起来是作者先按 21 帧/700 ms 训了模型，后来在上位机把窗口改成 45 帧/1500 ms 但没回头重训、也没改注释。要修就必须二选一：把上位机改回 21/7，或者在 EI 里把窗口改成 1500 ms 重训（同时按 patches 里说的把 672/924 改成从 input_length 反推）。

### 27. 上位机的类名与固件、与 Edge Impulse 类名三方不一致，UI 分级会全部落到默认分支

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:683-685 定义了三档风险的类名列表：
```python
high_risk_classes = ["HEMIPLEGIC", "ANTALGIC", "BLIND_PROBE"]
warning_classes = ["DOWNSTAIR", "UPSTAIR"]
safe_classes = ["WALKING", "STAND", "SIT"]
```
:680 用 `smoothed_class.upper()` 把 EI 返回的类名转大写后与这三张表比对。

固件侧（2.GD32F470ZGT6程序/final_wireless_v16.0/Hardware/lcd_my_test/lcd_mytest.c:37-38）的权威 8 类英文表是：
```c
"ANTALGIC", "BLIND PROBE", "DOWNSTAIRS", "HEMIPLEGIC",
"SITTING", "STANDING", "UPSTAIRS", "WALKING"
```

逐条对比，8 类里有 5 类对不上：
| 上位机 | 固件 | 差异 |
|---|---|---|
| BLIND_PROBE | BLIND PROBE | 下划线 vs 空格 |
| DOWNSTAIR | DOWNSTAIRS | 少词尾 S |
| UPSTAIR | UPSTAIRS | 少词尾 S |
| STAND | STANDING | 少 ING |
| SIT | SITTING | 少 TING |

更麻烦的是第三方：如果 EI 项目里的标签直接沿用了 CSV 文件名前缀（band / eye / normal_walk / unnormal_singol / sit / stand / upstair / downstair），那么 `.upper()` 之后得到的是 `BAND` / `EYE` / `NORMAL_WALK` / `UNNORMAL_SINGOL`，与上位机三张表**没有一个能匹配**，全部落到 :704-707 的默认托底分支——红底高危告警永远不会触发，AI 预测标签只会显示灰色的原始类名。

修复：以固件的 8 个字母序类名为唯一权威（因为字母序索引 = 传给 F470 的类别索引），EI 标签和上位机三张表都对齐到它。

### 28. 扫描名 VW553_Gateway 在固件里根本不存在，开箱即用一定连不上

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:244 `self.device_name = "VW553_Gateway"`，:463 用 `BleakScanner.find_device_by_name(self.device_name, timeout=5.0)` 做**完整名字精确匹配**。

全仓库搜索 `grep -rIl "VW553_Gateway"` 只命中这一个文件——固件里没有任何地方设置过这个名字。

VW553 发送端实际广播的名字来自 3.GD32VW553程序/发送/ble/app/app_adapter_mgr.c:79 的 `#define APP_DFLT_DEVICE_NAME ("GD-BLE")`，经 :115 的 snprintf 拼上 6 字节 MAC，形如 `GD-BLE-a1:b2:c3:d4:e5:f6`。而且 发送/app/app_cfg.h:150 里 `FEAT_SUPPORT_SAVE_DEV_NAME = 0`，:111 的 `#if` 分支被关掉，设备名不从 flash 读、每次上电都回默认值——即使用 AT+BLENAME 改过也不持久。

后果：别人克隆仓库、装好依赖、点开上位机，会一直卡在「🔴 未找到设备重试中」5 秒一轮无限循环，而代码里没有任何提示告诉他名字是错的。这是开源后第一个必然被提的 issue。

建议的修法不只是改字符串：改用 `BleakScanner.discover()` 后按 service UUID 0x0103 或用户配置的 MAC 匹配，并把设备名/MAC 提到命令行参数或配置文件里。

### 29. 显示的类名是平滑后的，显示的置信度却是平滑前的——两者可能属于不同类别

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:673-681：
```python
best_class = max(classifications, key=classifications.get)
score = classifications[best_class]        # ← score 属于 best_class
self.history_labels.append(best_class)
if len(self.history_labels) >= 2:
    smoothed_class = max(set(self.history_labels), key=self.history_labels.count)
else:
    smoothed_class = best_class
label_str = smoothed_class.upper()          # ← 显示的是 smoothed_class
self.predict_label.setText(f"🤖 预测: {label_str} ({score:.2f})")
```
`score` 取自本次单帧最高分类 `best_class`，但显示的标签 `label_str` 来自最近 3 次的多数投票 `smoothed_class`。当投票结果与本次最高分不一致时（这恰恰是平滑起作用的时刻），界面会显示「类别 A（属于类别 B 的置信度）」。

典型场景：连续三次结果是 WALKING / WALKING / HEMIPLEGIC，第三次触发时投票给 WALKING，但 score 是 HEMIPLEGIC 的 0.61 —— 屏幕显示「🚶 日常: WALKING (0.61)」，那个 0.61 其实是高危步态的分数。

修复：`score = classifications.get(smoothed_class, 0.0)`，或者干脆显示投票票数而不是分数。

另外 `history_labels` 的 maxlen 是 3（:339），`max(set(...), key=count)` 在 3 票分属 3 个不同类别时，选出哪个取决于 set 的迭代顺序——不确定行为。

### 30. 硬件时间戳被解析后丢弃，CSV 时间戳是 int(n×33.333) 合成的，掩盖了 15.19% 的零阶保持假帧

**位置**：[`host-app`](../host-app)

94 字节包里有 4 字节的硬件 `timestamp`，`parse_combined_packet` 也确实把它解出来放进了返回字典（smart_insole_display10.py:51 `"timestamp": unpacked[1]`），但 `handle_ble_notification`（:530-538）只取 `l_adc`/`r_adc`/`l_imu`/`r_imu` 四个字段，**timestamp 从此再没被任何代码引用过**。

录制时写进 CSV 的时间戳来自 :601：
```python
elapsed_ms = int(self.record_frame_count * 33.333)
```
是一个由本地帧计数器乘出来的**合成理想时间轴**，与数据实际到达时刻无关（实测相邻差在 33 / 34 ms 之间交替，就是 int() 截断的痕迹）。

真正的问题在于这条时间轴掩盖了采样机制：`process_uniform_frame` 每 33 ms 无条件抓一份内存快照（:589-605），如果这 33 ms 内 BLE 没送来新帧，就把上一帧原封不动再写一行。全数据集实测 **4517 / 29741 = 15.19% 的相邻帧 44 列数值完全相同**（band.csv 最严重 21.3%，normal_walk.csv 17.8%，eye.csv 16.3%；最好的 upstair4.csv 也有 5.5%）。

看 CSV 完全发现不了这件事——时间戳整整齐齐一路 33 ms，看不出哪一帧是真数据、哪一帧是保持出来的。这对训练的直接影响：约六分之一的样本携带零导数的伪静止信息，会让模型对「静止」类别和低频分量产生偏置。

修复方向：把硬件 timestamp 原样写进 CSV 再加一列 `is_held` 标记（或者写成 `n_repeats`），让下游能自己决定是否剔除。`self.record_start_time`（:262、:636 赋值）也是定义后从未使用，本来应该是用来算真实墙钟时间的。

### 31. 数据集有 2 路完全死掉的通道：L_ADC_2 和 L_ADC_14

**位置**：[`host-app`](../host-app)

对全部 57 个 CSV / 29 798 帧做逐通道统计，32 路 ADC 里有 2 路的标准差接近零：

| 通道 | min | max | mean | std |
|---|---|---|---|---|
| L_ADC_2 | 2 | 5 | 3.2 | **0.7** |
| L_ADC_14 | 2 | 5 | 3.2 | **0.7** |

对照其他通道：正常通道 std 在 10.8（L_ADC_9）到 172.5（R_ADC_5）之间，max 普遍在 366–526。这两路整个采集日全程只在 2–5 之间抖动，等于恒定的基线噪声——传感器或走线断了，或者 MUX 通道坏了。

把阈值放宽到 std < 3.0，命中的仍然只有这两条，说明是干净的二值故障，不是渐变劣化。

影响：
1. 32 路 ADC 实际只有 30 路有效，模型的 ADC 分支有 2/32 = 6.25% 的输入是常量。Conv1D 会学会忽略它们，但这两路对应的足底区域在训练数据里**没有任何压力信息**。
2. 别人如果修好硬件后重新采数据，新数据在这两路上会有真实信号，与旧数据分布不一致，模型直接迁移会退化。

开源数据集时必须在 README 的显著位置列出这两路，并建议下游使用者要么整列丢弃、要么在两个数据集混用时把新数据的这两路也置零。

### 32. 左右脚 ADC 基线相差 2.13 倍，数据集存在系统性左右不对称

**位置**：[`host-app`](../host-app)

全集 29 798 帧统计：
- 左脚 16 路 ADC 总体均值 **51.29**
- 右脚 16 路 ADC 总体均值 **109.26**
- 比值 R/L = **2.13**

逐通道看差异更极端（左 vs 右同序号）：
| 通道 | 左 mean | 右 mean | 倍数 |
|---|---|---|---|
| ADC_6 | 8.4 | 77.1 | 9.2× |
| ADC_7 | 24.5 | 111.9 | 4.6× |
| ADC_3 | 74.7 | 152.4 | 2.0× |
| ADC_13 | 104.8 | 145.6 | 1.4× |

峰值范围两侧接近（左 max 526、右 max 505），说明不是量程或增益差了一倍，更像是**静态基线偏置 + 部分通道的贴合/预压差异**：右脚鞋垫压得更实，左脚多路传感器长期处于低响应区。

影响很直接：模型学到的「左右不对称」特征里，有一部分是硬件偏置而非步态特征。而 unnormal_singol（推断为 ANTALGIC 痛性跛行）这个类**恰恰就是靠左右支撑相不对称来区分的**（实测 L0.63 / R0.53）——硬件基线偏置与病理特征混在同一个维度上，无法从现有数据里分离。这是 95.5% 这个准确率数字最需要打折的地方。

开源时应在数据集 README 里给出这张表，并建议下游做逐通道 z-score 或减去各自静止基线后再训练。

### 33. 色标图例的中间刻度标成 750，实际量程只有 0–300

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:180-182 画色标的三个刻度文字：
```python
painter.drawText(..., int(legend_y + 8), "300")                        # 顶部
painter.drawText(..., int(legend_y + legend_height/2 + 5), "750")      # 中部
painter.drawText(..., int(legend_y + legend_height), "0")              # 底部
```
顶 300、底 0 是对的——`generate_heatmap` 的归一化就是 `np.clip(heatmap_vals / 300.0, 0, 1)`（:146），`get_color_for_value` 也是 `value / 300.0`（:222）。

但中间标的是 **750**，一个超过量程上限两倍半的数。线性色标的中点应该是 **150**。看起来是从某个 0–1500 量程的旧版本改过来时漏了这一行。

后果：任何看着这张热力图读数的人，对中低压力区域的量级判断会偏差 5 倍。答辩演示、论文配图、README 截图，只要出现这张色标就是错的。

顺带一个相关问题：归一化上限 300 本身也偏低。全集实测 ADC 最大值 526（L_ADC_5），16 路里有 12 路的 max 都超过 430。也就是说热力图和柱状图（Y 轴同为 0–300，:391）在正常行走的着地峰值时会**大面积饱和顶格**，看不出压力分布的强弱差别。建议把上限提到 550 或做成可调。


---

## 不一致（18 条）

### 34. 右脚采样周期注释写成 40 Hz，与实际的 80 Hz 不符

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

右脚 `peripheral.c:94`：`#define SBP_PERIODIC_EVT_PERIOD 20  // 40Hz (25ms)`；左脚 `:95` 同一行写的是 `// 80Hz (12.5ms)`。宏值两边都是 **20**，TMOS tick = 0.625 ms（由 `:483`/`右 :441` 呼吸灯注释「32 tick × 0.625 ms = 20 ms」反推确认），所以两只脚实际都是 12.5 ms / **80 Hz**，右脚的注释是错的。同一文件 `右 :702` 的打印注释又写「80Hz 下每秒只打印一次」，与 :94 自相矛盾；另外 `左 :300`/`右 :287` 还留着「100Hz 足以覆盖你的 40Hz 发送」的旧注释。

影响：只是注释，不影响行为，但会误导任何按注释推算数据率、缓冲区大小或滑窗长度的人（下游 1D-CNN 的窗口长度是按采样率算的）。建议全部统一成 80 Hz / 12.5 ms，并在合并后的头文件里写清 TMOS tick = 0.625 ms 这个换算关系。

### 35. GAP 设备名属性仍是 WCH 默认的 "Simple Peripheral"，与广播名不一致

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`left/APP/peripheral.c:240`（右 :227）：`static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "Simple Peripheral";`，并在 :458 被写进 `GGS_DEVICE_NAME_ATT`。而扫描响应包里广播的是「智能鞋垫L」/「智能鞋垫R」（左 :222-233 / 右 :209-220）。

影响：手机 App 或 Central 如果连上后去读 GAP 服务的 Device Name 特征（0x2A00），拿到的是 "Simple Peripheral"，两只脚一模一样、也认不出是哪只——排障时容易被带偏。建议改成与广播一致的 `"Insole-L"` / `"Insole-R"`（ASCII，避免又踩一次编码坑）。

### 36. PA10/PA11 既当数字选通脚又疑似撞 ADC 通道，PB0 同理，需对原理图核实

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

代码把 **PA11**（`PIN_MUX_1314`）和 **PA10**（`PIN_MUX_1516`）配成推挽输出当模拟开关选通脚（`left/APP/peripheral.c:109-110`、:264），同时采样循环又在读 `CH_EXTIN_10` 和 `CH_EXTIN_11`（:112-116、:381-383）。同样地 **PB0** 被 PWM6 呼吸灯占用（:414-418），而 `MUX_COM1_CH = CH_EXTIN_12`、`MUX_COM2_CH = CH_EXTIN_13`（:118-119）。

`CH_EXTIN_n` → 物理引脚的对照表在 SDK 的 `StdPeriphDriver/inc/CH58x_adc.h` 和 CH583 数据手册里，**两者都不在本仓库**（SDK 整个缺失），所以无法从仓库内部判定是否真的撞脚。逻辑上只有两种可能：要么 AIN10/AIN11/AIN12/AIN13 并不落在 PA10/PA11/PB0 上（那就没事，只是命名巧合让人虚惊）；要么确实撞了，那是硬件级 bug。

请拿到原理图和数据手册的人核对一遍，把确定的 `AINx ↔ 引脚` 对照表补进 README 的引脚表——这是外人复现这块板子的第一道门槛。

### 37. Bosch BMI270 驱动是跨版本混搭：bmi2.* v2.113.0 配 bmi270.* v2.86.1

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

文件头声明的版本对不上：
- `APP/bmi2.c`、`bmi2.h`、`bmi2_defs.h` → `@date 2025-04-22 / @version v2.113.0`，版权年 2025
- `APP/bmi270.c`、`bmi270.h` → `@date 2023-05-03 / @version v2.86.1`，版权年 2023

（左右两份 md5 逐一相同，作者没改过这些文件；文件系统时间戳全部是 2025-06-18 17:02:22 的同一批拷贝。）

`bmi270.c` 里那块约 8 KB 的配置微码数组是与特定驱动核心版本配套发布的，跨两年的版本混搭理论上存在寄存器/结构体语义不匹配的风险。当前固件跑得通（串口能打 `BMI270 Driver Load SUCCESS!`），所以不必急着换；但必须在 README 里把这两个版本号钉死，并且**不要**让复现者"自己去 GitHub 拉一份最新的"——上游不以这个组合成对发布，拉到别的组合可能反而跑不起来。这也是本清单主张把这 5 个文件随仓库收录（BSD-3-Clause 允许）而不是只写个链接的原因。

### 38. 包尾字节的语义三方矛盾：接收板写死 0x55、发送板透明转发、上位机硬性校验 0x55 且从不读推理结果

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

项目设计说明是「H737 的推理结果覆写 94 字节包的包尾字节沿原路回传」。但代码里三处对不上：

1. `接收/app/main.c:308` 在任务启动时把 `g_sync_packet.tail = 0x55` 写死一次，之后**再也不动**——接收板这一侧包尾永远是 0x55。
2. 「发送」板是纯字节管道（`发送/app/main.c:198-203` 只做环形缓冲搬运，不解析包结构），包尾被谁改过它一律照转，这一侧没问题。
3. 上位机 `4.工程代码/5.上位机代码/smart_insole_display10.py:524` 在取帧时硬性要求 `self.data_buffer[PACKET_SIZE-1] == PACKET_TAIL`（0x55），`:45` 的 `parse_combined_packet` 里又查一遍 `data[-1] != PACKET_TAIL` 就返回 None；而 `PACKET_FMT` 解出来的字段里**根本没有把包尾取出来用**（`:49-55` 只取了 timestamp 和四组数组）。

影响：如果 H737 真的把类别（0-7）写进了包尾，那么这份上位机脚本会把**每一帧**都当成坏帧丢掉，界面全黑；反过来如果链路上包尾一直是 0x55，那「包尾承载推理结果」这条设计在这一版代码里就是没落地的。开源前必须查清 GD32F470/H737 那一段到底改不改包尾，然后统一：要么给推理结果单开一个字节（把 timestamp 压到 3 字节或包长改成 95），要么上位机改成不校验包尾、直接把它当类别读。

### 39. 随工程拷出的 app_cfg.h 里 CONFIG_BLE_LIB = BLE_LIB_MIN，与实际烧成功的固件矛盾，照抄必定编不出可用的接收板

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/app/app_cfg.h:134` 是 `#define CONFIG_BLE_LIB  BLE_LIB_MIN`，而同文件 `:131` 自注 `BLE_LIB_MIN` = only peripheral and server。「接收」板要做双 Central + GATT Client，这个值必须是 `BLE_LIB_MAX`。

交叉验证：(1)「发送」和「接收」两份 `app_cfg.h` **逐字节相同**（`diff -rq` 只报了 4 个文件不同，app_cfg.h 不在其中），说明作者没在这份拷贝里区分两块板的配置；(2) 但 `接收/image-all.bin` 里确实存在 `🚀 [SYSTEM] 服务搜索完成，准备执行 MTU 交换和开阀...` 等字符串，而这些字符串所在的 `ble_app_conn_gatt_discovery_callback` 是个 `static` 函数、唯一调用点在 `app_conn_mgr.c:487-492` 的 `#if (BLE_APP_GATT_CLIENT_SUPPORT)` 块里——如果这个宏是 0，该函数会变成未被引用的 static 而被编译器丢掉。它出现在镜像里，反证实际编译时 `BLE_APP_GATT_CLIENT_SUPPORT == 1`，也就是当时用的确实是 BLE_LIB_MAX。

结论：作者是在 SDK 那侧改的 `app_cfg.h`，忘了同步回这份拷贝目录。影响：第三方照着仓库里的 app_cfg.h 编，会连上鞋垫但永远不做服务发现、不开 Notify，零数据，且日志里看不到任何错误。已作为 patch 第 2 条写进 build_instructions。

### 40. image-all.bin 与源码双向不一致：镜像里有源码不存在的逻辑，源码又比镜像新

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/image-all.bin` 内嵌构建时间戳 `2026/06/13 19:32:57`（`strings` 可见），文件 mtime 2026-06-13 19:33:08。但 `接收/ble/app/app_conn_mgr.c` 的 mtime 是 **2026-06-16 01:28:22**，比镜像晚 2 天多——源码在这份固件烧完之后又被改过，仓库里的源码编不出这个 bin。

反方向也不一致：镜像里存在字符串 `⚠️ 垃圾包拦截！期望 %d 字节，实际收到 %d 字节`，而 `grep -rn 垃圾包 接收 发送` 在两个工程里**零命中**。说明烧进去的那一版有一段「按预期长度校验 Notify 载荷」的逻辑，后来从源码里被删掉了——而这段校验恰恰是防上一条 issue（错位/垃圾帧）的，删掉它可能是一次功能退化。

影响：(1) 任何人拿这个 bin 对照源码阅读都会被误导；(2) 那段被删掉的长度校验值得找回来（可能在作者的本地历史或备份里），因为 `insole_data_recv_cb`（app_conn_mgr.c:874-892）现在对 `len` 完全不做检查就整段转发，而 `user_trans_uart_send`（main.c:225）只有一个 `len > 250` 的上界。建议开成 issue 让作者回忆/找回那段逻辑，并把 bin 移出 git（见 build_instructions 末尾）。

### 41. IPA 中断服务程序是死代码：g_gpu_state 永远不会被置 1

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`User/gd32f4xx_it.c:170-182` 的 `IPA_IRQHandler` 在 `g_gpu_state == 1` 时才调 `lv_disp_flush_ready(g_disp_drv)`，而全工程只有三处引用 `g_gpu_state`——定义(it.c:65) 和这个 if 里的读/写(:174/:176)，**没有任何地方把它写成 1**（`lv_port_disp_template.c:31` 那行 extern 已被注释掉）。同时 disp_flush 走的是纯同步路径：:118 `IPA_CTL = 0x00000000UL` 明确关掉了 FTFIE 完成中断，:144 `while(IPA_CTL & IPA_CTL_TEN)` 忙等，:149 自己调 `lv_disp_flush_ready`。所以 `lcd.c:115` 那句 `nvic_irq_enable(IPA_IRQn, 0, 2)` 也是空挂。这是一次"异步 IPA 改回同步忙等"重构留下的残骸：既浪费了 IPA 与 CPU 并行的机会（每次 flush 都在 UI 任务里干等搬运完成），又留了一段看起来在工作、实际永不执行的中断代码给读者误导。要么把异步路径接回去（重新开 FTFIE、置 g_gpu_state=1、去掉忙等和同步 flush_ready），要么把 IRQ 和 g_gpu_state 一起删掉。

### 42. bsp_usart.h 里 "GD32 的 USART2 就是 UART3" 的宏定义是错的，usart3_config() 一旦被调用会把 PC10/PC11 复用错

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`Hardware/usart/bsp_usart.h:41-52` 定义 `BSP_UART3_RCU = RCU_USART2`、`BSP_UART3 = USART2`、`BSP_UART3_AF = GPIO_AF_7`，注释写着"注意：GD32的USART2就是UART3"——这句话不成立，GD32F4 的 USART2 和 UART3 是两个独立外设。真正在用的是 `bsp_usart.c:194-204`：`RCU_UART3` + `GPIO_AF_8` + `usart_baudrate_set(UART3, ...)`。PC10/PC11 这对引脚恰好同时是 USART2(AF7) 和 UART3(AF8) 的复用脚，所以两套配置"都编得过"，只是 AF 号不同。`usart3_config()`（bsp_usart.c:86-103）和 `usart3_send_data()`（:105-109）目前是死函数（main.c 未调用），但谁要是照着头文件把它调起来，就会把 PC10/PC11 切到 USART2，上位机链路当场哑掉，而且波特率寄存器改的是另一个外设，极难查。建议：删掉这组宏和两个死函数，或把它们改名成 `BSP_PC_UART_*` 并统一到 UART3+AF8。

### 43. Keil 器件选型与存储器配置和实物、和分散加载文件三者互相矛盾（靠 umfTarg=0 才没炸）

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`Project/15_lvgl_freertos_test5.uvprojx` 里：`<Device>GD32F470VE`（V=100 引脚、E=512 KB Flash）、`<TargetName>GD32F450`、`Cpu = IRAM(0x20000000,0x030000) IRAM2(0x10000000,0x010000) IROM(0x08000000,0x080000)`（192 KB + 64 KB TCM + 512 KB Flash）、烧写算法 `GD32F4xx_512KB.FLM`。而实物是 **GD32F470ZGT6**（Z=144 引脚、G=1 MB），并且 TLI 用到了 PG6/PG7/PG10/PG11/PG12（`Hardware/RGB/lcd.c:150-155`）——GPIOG 在 100 引脚的 VE 封装上根本引不出来，选 VE 在物理上就是错的。真正决定链接的是 `Project/Objects/GD32F450.sct`：Flash 1 MB、RAM 0x20000000 起连续 256 KB，只因为 `<umfTarg>0`（未勾选 Use Memory Layout from Target Dialog）才轮到它说话。影响：谁在 Options 里手滑勾回 "Use Memory Layout from Target Dialog"，128 000 B 绘制缓冲 + 71 680 B FreeRTOS 堆 + 48 KB LVGL 池就塞不进 192 KB，链接直接失败；换 1 MB 烧写算法之前也没法用满 Flash。建议：器件改选 GD32F470ZG，Target 对话框的 IROM/IRAM 与 .sct 对齐，Target 名字也从 GD32F450 改掉。

### 44. 8 类的索引→标签映射只存在于 GD32F470 的字符串表里，推理端没有任何标签记录

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：`main.c:216-221` 只做 argmax 拿到 `0..7`，`:238` 把裸索引写进包尾，H737 侧全无标签表。唯一的映射在下游：`4.工程代码/2.GD32F470ZGT6程序/final_wireless_v16.0/Hardware/lcd_my_test/lcd_mytest.c:36-39` — `{"ANTALGIC", "BLIND PROBE", "DOWNSTAIRS", "HEMIPLEGIC", "SITTING", "STANDING", "UPSTAIRS", "WALKING"}`（中文表在 `:31-34`）。另一处不同拼写在上位机 `4.工程代码/5.上位机代码/smart_insole_display10.py:685` — `["WALKING", "STAND", "SIT"]`、`:684` `["DOWNSTAIR", "UPSTAIR"]`、`:683` `["HEMIPLEGIC", "ANTALGIC", "BLIND_PROBE"]`（用的是名字而不是索引，因为它走的是 EI 的 HTTP 接口）。

**影响**：索引顺序的正确性完全依赖"Edge Impulse 按标签字母序排类别"这个隐含约定。数据集文件名（`4.工程代码/6.模型训练代码/采集的数据/`）用的却是另一套名字：`unnormal_singol`、`eye`、`downstair`、`band`、`sit`、`stand`、`upstair`、`normal_walk`——`band` 对应 HEMIPLEGIC、`eye` 对应 BLIND PROBE、`unnormal_singol` 对应 ANTALGIC，这层对应关系**在代码和文档里都没有任何地方记录过**，是靠推断得出的。一旦重训时标签改名，字母序变了，索引就会静默错位，8 类会整体串号且没有任何报错。

**建议**：在 `nn_model_configure.h` 旁边加一个 `labels.h`（或一份 `model/labels.txt`），把索引、EI 标签名、采集文件名前缀、中文名四列固定下来，H737 与 F470 共用。

### 45. nn_model_configure.c 里的 platform_name 写着 GD32H759I_EVAL，实际板子是自制的 GD32H737VMT6

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：`GD32H7_study/GD32_H7_AI/1.0/nn_model_configure.c:38` `char* platform_name = "GD32H759I_EVAL";`，而 uvprojx 的 `<Device>` 是 `GD32H737VM`，`system_gd32h7xx.c` 还专门打了 IRC64M + LDO 两个补丁（见 patches）才能在自制板上跑起来——恰恰说明目标板不是官方 EVAL 板。

**影响**：只是 GD_LIB 报告通路用的字符串（配合 `nn.h:41` 的 `BENCHMARK` 输出），不影响算子行为。但它会误导读者以为固件是给 GD32H759I-EVAL 评估板写的，从而不去打那两个补丁，结果板子起不来。同一目录 `alll/tinyml_test3.0/main.cpp:6` `#include "gd32h759i_start.h"` 也是评估板遗留，加深这个误解。

**建议**：改成 `"GD32H737VMT6_CUSTOM"`，或在 README 里注明这个字符串是 AI 转换工具生成时选的平台名、与实际硬件无关。

### 46. 仓库里有两组权重（finnal4 / finnal3），没有任何说明哪个是最终版、差别是什么

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：`GD32H7_study/GD32_H7_AI/1.0/nn_model_configure.c`（6月15 14:11）与 `GD32H7_study/GD32_H7_AI/1.0/成功版本1/nn_model_configure.c`（同为 3835 行）。逐行 diff 结果：**只差第 37 行 `model_name`（`"finnal4"` vs `"finnal3"`）和第 183–1778 行的权重字节**，其余（算子表、buffer 尺寸、宏）完全一致。

**哪个在用**：`h737vmt6_AI1.0/GD32H737_Base.uvprojx` 的 `<FilePath>` 指向 `..\GD32H7_study\GD32_H7_AI\1.0\nn_model_configure.c`，也就是 **finnal4**；`成功版本1/` 那份没有被任何工程引用。

**影响**：目录名叫"成功版本1"，读者会以为那才是能用的；而实际编进固件的是外面那份。两份都没有精度记录、没有对应的训练脚本、没有类别顺序说明。此外 `main.c:72` 的模型实例变量叫 `finnal1`，与 `model_name` 的 finnal4/finnal3 又是第三套命名，进一步加剧混乱。

**处理**：本次开源只收录 finnal4 那一份（uvprojx 实际引用的），`成功版本1/` 整个排除。建议作者补一句说明，或干脆删掉旧权重。

### 47. 转发包的时间戳字段被本地帧计数覆写，采集端的原始时间戳在链路末端丢失

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：包结构 `main.c:53` 有 4 字节 `uint32_t timestamp`（由采集端 CH583M 打的），H737 在 `main.c:232` 把整包 memcpy 过来后，`main.c:236` 写成 `packet_to_send->timestamp = debug_frame_count;` —— `debug_frame_count`（`main.c:45` 定义，`:172` 每收一帧 +1）是 H737 自己的收帧序号。

**影响**：GD32F470 和上位机看到的"时间戳"其实是 H737 的帧序号，不是采集时刻。于是链路上**再也无法测量端到端延迟、也无法发现上游丢帧**（帧序号是本地连续自增的，上游丢了帧这个数字照样连续）。这也是仓库里拿不出任何端到端时延数字的原因之一（另一个原因是 `main.c:74-76` 把 GD_LIB 的计时函数打成了恒 0 桩）。

**建议**：要么保留原始 timestamp 不动、把帧序号塞进包里别的保留位；要么明确文档化"timestamp 字段在 H737 之后语义变为 H737 收帧序号"。当前两边都没写。

### 48. error_count 把三种完全不同的口径加在一起，UI 却标成「丢包」

**位置**：[`host-app`](../host-app)

smart_insole_display10.py 里 `self.error_count` 在三个地方自增，每次的单位都不一样：

1. **:518** `self.error_count += header_idx` —— 加的是「帧头前被丢弃的**字节数**」。一次可能加 1，也可能加 93。
2. **:542** `self.error_count += 1` —— 加的是「一个通过了头尾校验但 struct.unpack 失败的**完整包**」。
3. **:546** `self.error_count += 1` —— 加的是「一次帧尾校验失败、指针前移 1 字节的**搜索事件**」。一个 94 字节的坏包在最坏情况下会在这里累加 94 次。

然后 :559 把这个数显示成 `f"⚠️ 丢包: {self.error_count}"`。字节数 + 包数 + 搜索次数三者相加得到的数字，无论叫「丢包」还是叫别的都没有物理意义，也无法用来判断链路质量——它可能因为一次 BLE 抖动就跳几十，也可能真丢了几十个包只加了几十。

修复：拆成 `dropped_bytes` / `bad_packets` / `resync_events` 三个独立计数器分别显示，或至少只把 :542 那一处叫「丢包」。

附带：`self.frame_count`（:250、:537 自增、:633 归零）定义了却从不显示——UI 上的「⚡ 帧率」用的是另一个变量 `fps_frame_count`（:296、:538、:551-554）。

### 49. 导出 CSV 的 L_* 列装的其实是报文 right_* 半区的数据

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:531-536 的左右交叉赋值：
```python
self.left_adc  = parsed["r_adc"]   # 拿后半截的 ADC 给左脚
self.right_adc = parsed["l_adc"]  # 拿前半截的 ADC 给右脚
self.left_imu  = parsed["r_imu"]
self.right_imu = parsed["l_imu"]
```
这大概是为了纠正硬件接线或鞋垫左右装反，是有意为之（注释也写明了）。但 :721-726 写表头时用的是 `L_ADC_1..16` / `R_ADC_1..16`，:596 组装特征时也是先 left 后 right ——**CSV 里所有 `L_` 前缀的列，物理来源是 94 字节包结构体里的 `right_adc[]` / `right_imu[]` 字段**。

这带来两个具体麻烦：
1. 数据集里检出的两条死通道叫 `L_ADC_2` / `L_ADC_14`，但它们物理上位于占据报文后半段的那只鞋垫上。别人拿着这个信息去查硬件会找错脚。
2. 任何试图把 CSV 与固件端 CH583 通道编号对应起来的人，都会差一个左右翻转。

仓库里没有任何文档说明这次交叉，也没有说明哪只物理鞋垫对应报文的前/后半段。开源时必须在 `dataset/README.md` 里写清楚：`L_*` = 报文后半段，`R_*` = 报文前半段，且哪只脚在哪半段无法从代码确定（需要作者补充硬件记录）。

### 50. CSV 文件名拼写错误：downstair15csv.csv

**位置**：[`host-app`](../host-app)

`4.工程代码/6.模型训练代码/采集的数据/downstair15csv.csv`（23 972 B，mtime 2026-06-14 14:55）—— 在序号和扩展名之间多打了一个 `csv`。同批的其他 24 个文件都是规范的 `downstairN.csv`。

文件内容本身完全正常：45 列表头、151 帧数据、时间戳 0 起步长 33/34 ms，与同批文件无差异。

影响：任何按 `downstair(\d+)\.csv` 正则批量加载的脚本都会漏掉这一段（3698 帧里的 151 帧，约 4%）；Edge Impulse 按文件名前缀自动打标签时也可能把它归错类或单独建一个类。

开源时建议直接重命名为 `downstair15.csv`，并在提交信息或 CHANGELOG 里记一句，方便对照原始采集记录。

### 51. 8 个类别与 CSV 文件名前缀的映射在仓库里没有任何文字依据，band/eye/unnormal_singol 三条是推断

**位置**：[`host-app`](../host-app)

这不是代码 bug，而是一处必须在开源文档里明确标注的**证据缺口**。

仓库里能确证的只有一件事：固件侧存在一张 8 元素的双语字典表（2.GD32F470ZGT6程序/final_wireless_v16.0/Hardware/lcd_my_test/lcd_mytest.c:32-40），按字母序排列，索引 0–7 对应
`ANTALGIC(痛性跛行) / BLIND PROBE(盲态探步) / DOWNSTAIRS(正在下楼) / HEMIPLEGIC(偏瘫步态) / SITTING(静止坐立) / STANDING(静止站立) / UPSTAIRS(正在上楼) / WALKING(正常走路)`。

数据集侧有 8 个文件名前缀：`upstair / downstair / sit / stand / normal_walk / band / eye / unnormal_singol`。

**仓库里没有任何文件把这两组名字对应起来**——没有标注文件、没有采集日志、没有 EI 项目导出、`使用说明.txt` 只有 3 行讲 Scale axes。

其中 5 条靠词义直接确定：upstair→UPSTAIRS、downstair→DOWNSTAIRS、sit→SITTING、stand→STANDING、normal_walk→WALKING。

剩下 3 条（band / eye / unnormal_singol → HEMIPLEGIC / BLIND_PROBE / ANTALGIC 的某个排列）**只能从步态量化特征反推**。我实测的依据（合力 = 16 路 ADC 求和，接触阈值取该文件峰值的 15%，步数按上升沿计）：

| 文件 | 步频(步/min) | 双支撑相 | 双脚同时离地 | 支撑相 L/R | 角速度能量 L/R |
|---|---|---|---|---|---|
| normal_walk（基线） | 136.0 | 0.18 | 0.05 | 0.55 / 0.57 | 1249 / 1110 |
| eye | **90.7** | **0.36** | **0.00** | 0.62 / 0.73 | 542 / 452 |
| unnormal_singol | 128.2 | 0.18 | 0.03 | **0.63 / 0.53** | 976 / 729 |
| band | 133.7 | 0.21 | 0.05 | 0.57 / 0.59 | **969 / 875** |

推断链条：
- **eye → BLIND_PROBE**：步频掉到基线的 67%、双支撑相翻倍、摆动期归零，是典型的试探性缓行；文件名 eye 也指向「蒙眼」。这条最可靠。
- **unnormal_singol → ANTALGIC**：唯一出现支撑相左右反转不对称（0.63 vs 0.53）的一类，符合痛侧缩短承重时间；`singol` 疑为 `single`（单侧）的拼写错误。较可靠。
- **band → HEMIPLEGIC**：步频与基线几乎相同但角速度能量降到 78%，符合绑带限制单腿屈曲、踝/胫段转动受限；文件名 band 指向「绑带」。**这条最弱**——它主要是排除法的结果（前两条定下后只剩这一个），量化区分度也最小。

开源文档里必须原话写明：「以上三条为根据步态量化特征反推的推断，仓库内无原始标注依据。band 一条置信度最低。使用者若需确证请联系作者核对采集记录。」不要写成事实。

还有一个必须一起写的事实：**band / eye / unnormal_singol 三类病理步态是健康被试模拟的，不是真实患者数据**。这直接决定了 95.5% 这个准确率的适用边界。


---

## 可改进（5 条）

### 52. performPeriodicTask 每 12.5 ms 阻塞 BLE 协议栈至少 1.34 ms 的忙等

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

把 `FSR_Sample_All()` 里的 `mDelay` 字面量加起来（`left/APP/peripheral.c:364-397`）：Vref 暖机 1000 µs（:373）+ 12 路 × 15 µs 通道建立（:348，每路一次）= 180 µs + 2 次 mux 切换 × 50 µs（:387、:392）= 100 µs + 4 个复用通道 × 15 µs = 60 µs，合计 **1340 µs 纯忙等**；此外还有 (12+4) × 5 = **80 次阻塞式 `ADC_ExcutSingleConver()`**（每通道 1 次假读 + 4 次过采样，:353-359）。这一整套都跑在 TMOS 事件回调里（:513-518 → :707），期间协议栈拿不到 CPU。

注意：以上 1340 µs 是把代码里的常量相加得到的，**不是实测的端到端延迟**——ADC 转换本身、I2C 读 BMI270、以及 `bmi2_get_sensor_data()` 的耗时都没有任何实测数据。

影响：12.5 ms 的周期里至少 11% 时间协议栈被堵住，而期望连接间隔只有 10–15 ms（:101-102），又叠加了 Coded PHY（:529-537，空中包更长）；这类配置容易出现偶发丢包或连接参数协商不上。可考虑的改进：改用 `ADC_DMACfg()`（SDK 里有这个函数）做批量搬运、把 16 路扫描拆到多个 tick 里分摊、或把 Vref 暖机改成"常供电 + 只在长时间空闲后暖机"。

### 53. 1.5 Mbps 在 APB1=50 MHz 下分频不整，实际约 1.515 Mbps（+1.0%），而两个 ISR 把帧/溢出错误直接吞掉，链路降级不可观测

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

UART4/UART3/USART1 都挂在 APB1 = 50 MHz（`system_gd32f4xx.c` 的 `RCU_APB1_CKAHB_DIV4`，SYSCLK 200 MHz）。USARTDIV = 50e6/(16×1.5e6) = 2.0833，寄存器只能取整数 2 + 小数 1/16 = 2.0625，实际波特率 50e6/(16×2.0625) ≈ 1 515 152 bps，偏差 **+1.0%**。单端 1% 尚在容限内，但对端（H737/VW553）若也有同向偏差就会逼近极限。而 `bsp_usart.c:131-138`（UART4）和 :262-269（USART1）都是读 STAT0+DATA 静默清错，不计数、不上报——`ORERR`/`FERR` 的发生频率完全不可见，链路劣化只会表现为屏幕数字偶尔跳一下。建议：(a) 给两个 ISR 各加一个错误计数器并显示在屏幕上（旁边就有 FM 帧号可以借位）；(b) 或把波特率改成 APB1 能整分的值（如 1 562 500 = 50 MHz/32），三家一起改。这条是基于分频器数学的推断，未做实测误码率测量。

### 54. 94 字节双发在临界区里逐字节忙等，约 1.25 ms；两个串口 ISR 的优先级高于内核 syscall 上限，里面永远不能调用 FreeRTOS API

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`Hardware/usart/bsp_usart.c:230-247`：`taskENTER_CRITICAL()` 之后对 UART3 和 USART1 逐字节 `usart_data_transmit` + `while(TBE)`，两路串行发完 94 字节 ≈ 94×10/1.5e6×2 ≈ 1.25 ms，每 33 ms 一次（占 3.8% 的时间完全不调度）。这段时间里 UI 任务和 idle 全被挡住。同时要注意一个隐性约束：UART4 的 NVIC 抢占优先级是 3（:71）、USART1 是 4（:223），而 `FreeRTOSConfig.h:184` 的 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`（configPRIO_BITS=4，:172），数值更小=优先级更高，所以这两个 ISR 位于内核**不可屏蔽**区间——好处是临界区挡不住它们、高速接收不丢字节；代价是这两个 ISR 里绝对不能出现任何 `*FromISR` 调用，否则会踩 port.c 的断言或直接破坏内核。现在的代码恰好没有调用（全靠全局变量交接），但没有任何注释提醒后来者。建议：转发改用 DMA 发送 + 信号量，或至少把这条优先级约束写成注释；顺便 `main.c:186` 的手工 `(xTaskGetTickCount() - xLastWakeTime) >= xFrequency` 应换成 `vTaskDelayUntil()` 以消除累积漂移。

### 55. 48 号中文字体由 simhei.ttf 生成，字形数据版权不明，不宜随开源仓库分发

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`LVGL/porting/ui_font_cn_48.c`（3385 行、156 KB）文件头第 3 行完整记录了生成命令：`--bpp 4 --size 48 --no-compress --stride 1 --align 1 --font simhei.ttf --symbols 痛性跛行盲态探步...等待同步 --format lvgl -o ui_font_cn_48.c`。SimHei（中易黑体）是随 Windows 分发的商业字体，把它的字形位图嵌进开源仓库属于再分发字体轮廓数据，风险明确。因此本次不收录该文件，改为在 build_instructions 里给出同参数的重新生成命令，并建议换成思源黑体（SIL OFL）或文泉驿等可自由分发的字体。附带一个技术细节：该文件末尾 `.fallback = &lv_font_montserrat_48`，而 `lv_conf.h:368-388` 并没有打开 montserrat_48——换字体重生成时要把这行 fallback 改成已启用的字号（如 montserrat_32），否则链接会缺符号。

### 56. 权重的上游模型文件（.tflite / .h5 / .eim）不在仓库，转换流程无法复现

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：对整个仓库执行 `find -iname '*.tflite' -o -iname '*.h5' -o -iname '*.onnx' -o -iname '*.keras' -o -iname '*.eim' -o -iname '*.pb'`，**零命中**。板上能拿到的只有已经转成 GD_LIB 私有格式的字节数组：`nn_model_configure.c:79` `model_paras_arr[33960]` + `:1784` `model_paras_data[40960]`。

**影响**：
1. 第三方只能原样使用这 74 920 字节，无法反解、无法微调、无法换推理框架（比如换成 TFLite Micro 或 CMSIS-NN 自己搭）。
2. 也就无法独立验证 IMU 缩放系数到底该是 0.000066 还是 0.00024414（第二条 issue 卡在这里）。
3. 也无法核对 8 类的实际输出顺序（第六条 issue 卡在这里）。
4. 95.5% 这个准确率（答辩 PPT 自述、Edge Impulse 验证集、单被试单日数据）无法被任何人重新验证。

**建议**：把 Edge Impulse 项目导出的 `.tflite`（或 Keras `.h5`）连同 EI 项目 ID、Impulse 配置截图（两个 Raw Data block 的 axis 绑定顺序和 Scale axes 值）一起放进 `model/` 目录。这几个文件加起来不到几百 KB，是把"一堆权重字节"变成"可复现的模型"的关键，也顺手把上面三条 issue 都解掉了。


---

## 待清理（19 条）

### 57. 右脚 peripheral.c 的中文注释已被有损编码转换破坏，约 20 行变成一串 '?'

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

现状：左脚 `peripheral.c` / `peripheral_main.c` / `include/peripheral.h` 都是 **GBK + CRLF**，注释完好；右脚 `peripheral.c` 也是 GBK + CRLF，但其中约 20 行注释的中文**已经被一次有损码页往返彻底毁掉**——中文变成字面量 `?` 并夹着零星孤立 GBK 字节。逐行验证（734 行里 695 行纯 ASCII、39 行含非 ASCII、0 行能按 UTF-8 解码）后的对照例子：

| 行 | 右脚现状（字节）| 左脚同一行 |
|---|---|---|
| 12 | `// ?????????? main ??\xa7\xd5????????` | `// 声明我们在 main 里写的打印函数` |
| 107-109 | `// PIN_FSR_POWER ... // ??????????????` | `// 传感器动态供电脚` |
| 516-517 | `// --- \xa6\xc4?????????????? 1.1V ---` | `// --- 未连接：固定低亮度 1.1V ---` |

未受损的是后期新写/改过的那些块（右 :27,:31-32、:487-495 Coded PHY 段、:601、:666-702 performPeriodicTask 段）。另外**左右两份的 emoji 全部丢失**：源码里那些孤零零的 `? ` 前缀（左 :27,:37,:308,:311,:325,:335,:348,:351,:355,:365,:371,:375,:412,:441,:482,:528,:548 等）原本是作者写的 emoji，GBK 表示不了就退化成了 `?`。

处理方案（这些行都在注释里，源码本体是纯 ASCII，改完不影响编译产物）：
1. 先把右脚被毁的注释从左脚同名行抄回来——这两个文件在受损区域是逐行同构的，可以机械对照；
2. 六个作者文件统一 `iconv -f GBK -t UTF-8 <in> -o <out>`，顺手 `tr -d '\r'` 转成 LF；
3. 仓库根加 `.gitattributes`（`*.c text working-tree-encoding=UTF-8`）和 `.editorconfig`（`charset = utf-8`），防止下一个 Windows 编辑器再来一遍；
4. 丢掉的 emoji 不必强行还原，但那些光秃秃的 `?` 前缀建议改成 `[优化]`/`[修复]`/`[新增]` 之类的纯文本标记，否则读起来像乱码。

### 58. 右脚固件里整套软 I2C ping 引擎无人调用，另有 256 字节 .bss 白占

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

(a) 右脚 `peripheral.c:135-203` 完整保留了 `IIC_SCL_PIN`/`IIC_SDA_PIN` 宏、`SDA_OUT/SDA_IN/SDA_HIGH/SDA_LOW/SDA_READ/SCL_OUT/SCL_HIGH/SCL_LOW` 八个宏，以及 `IIC_Delay`/`IIC_Init`/`IIC_Start`/`IIC_Stop`/`IIC_Wait_Ack`/`IIC_Send_Byte`/`IIC_Read_Byte` 七个 static 函数，但唯一的调用者 `BMI270_Soft_Ping_Test()` 已经被删掉了（左脚 :317-342 有，右脚没有），grep 全文确认零调用点 → 一堆 `-Wunused-function`。

(b) 左脚也有半个同类问题：`IIC_Read_Byte`（左 :202）从来没被调用过（ping 测试只发地址、等 ACK，不读数据），`BMI270_Soft_Ping_Test` 也只在被注释掉的 :407 出现。

(c) `static char ble_tx_str_buf[256];`（左 :133 / 右 :129）声明后全文零引用 —— 是早期"发 ASCII 字符串"版本的残留，白占 256 字节 .bss（这颗芯片总共 32 KB SRAM，协议栈堆已经吃掉 6144 字节）。

(d) `Peripheral_Init()` 里 `tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT)` 连着写了两遍（左 :477 和 :480，右 :435 和 :438），第二次是加呼吸灯时复制粘贴带进去的。

建议：合并左右固件时把 ping 引擎整块保留（它是调 BMI270 时真正有用的排障工具，`peripheral.c:317-342`），删掉 `ble_tx_str_buf` 和重复的 `tmos_set_event`，`IIC_Read_Byte` 要么用起来（ping 完顺手读 CHIP_ID 0x00 寄存器校验 0x24）要么删掉。

### 59. ADC 输入引脚从未配成模拟/浮空输入，靠复位默认值工作

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`FSR_Hardware_Init()`（`left/APP/peripheral.c:262-268`）只配了 PB18（供电脚）和 PA10/PA11（选通脚），**14 个 ADC 输入脚一个都没调用过 `GPIOA_ModeCfg(..., GPIO_ModeIN_Floating)`**；全文 grep `GPIOA_ModeCfg`/`GPIOB_ModeCfg` 只有 :151/:152/:157（软 I2C 宏）、:263、:264、:275、:276、:414 这几处。而 `ADC_ChannelCfg()` 并不会替你配 GPIO——它在 SDK 头文件里是内联的寄存器写操作，`readelf -sW obj/StdPeriphDriver/CH58x_adc.o` 的函数符号表里只有 `ADC_DataCalib_Rough`/`ADC_ExtSingleChSampInit`/`ADC_ExcutSingleConver`/`ADC_DMACfg` 等 11 个，**没有** `ADC_ChannelCfg`。

影响：现在能跑是因为 GPIO 复位后默认就是浮空输入。但这是隐式依赖——哪天有人在 `main()` 里加一句 `GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU)`（`peripheral_main.c:69-70` 就有这么一行被注释掉的代码，HAL_SLEEP 场景下 WCH 建议打开！），16 路压力立刻全废。WCH 官方 ADC 例程是先显式 `GPIO_ModeIN_Floating` 再初始化 ADC 的。建议在 `FSR_Hardware_Init()` 里补上显式配置。

### 60. sample_channel_robust 的返回值在粗校准偏置为负时会无符号回绕

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`left/APP/peripheral.c:361`：`return (uint16_t)((sum >> 2) + RoughCalib_Value);`，其中 `RoughCalib_Value` 是 `static signed short`（:134），由 `ADC_DataCalib_Rough()` 在 :267 赋值，**可以是负数**。当某通道读数接近 0 而校准偏置为负时，和为负、被强转成 `uint16_t` 会回绕到 6 万多；这个值存进 `current_adc_values[]`（`uint16_t`，:132），随后在 :719 减 1530 再转 `int16_t`，输出一个毫无意义的大幅跳变。

虽然实际工况下压阻读数通常远离 0（底噪约 1530，见另一条 issue），触发概率不高，但断线/开路的通道恰好就落在这个危险区。建议中间量用 `int32_t`，并在返回前 clamp 到 `[0, 4095]`。

### 61. MRS 工程排除了 12 个 CH57x_*.c —— CH583 SDK 里根本没有这些文件

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

`mrs-project/Peripheral.wvproj` 的 `buildConfig/configurations[0]/excludeResources` 列了 16 项，其中 `[3]`–`[15]` 共 13 项是 `${project}/StdPeriphDriver/CH57x_pwm.c`、`CH57x_adc.c`、`CH57x_usbdev.c`、`CH57x_usbhostClass.c`、`CH57x_usbhostBase.c`、`CH57x_spi0.c`、`CH57x_timer0.c`~`timer3.c`、`CH57x_uart0.c`、`CH57x_uart2.c`、`CH57x_uart3.c`。CH583 的 SDK 里这些文件叫 `CH58x_*.c`（`obj/StdPeriphDriver/` 下编出来的 49 个 .o 全是 `CH58x_` 前缀），所以这 13 条排除项**指向不存在的路径**，是从 CH57x 工程模板复制过来的残留。有效的只有 `[0]`–`[2]`：`HAL/Profile`、`HAL/KEY.c`、`HAL/LED.c`。

影响：不影响构建（排除不存在的文件是无操作），但会让照着这份工程文件复现的人困惑，也说明 `StdPeriphDriver` 里那 49 个 CH58x 源文件其实全都参与了编译（USB host/device、四个 timer、三个 UART 都编进去了，靠 `--gc-sections` 收尾）。建议把这 13 条删掉，若确实想缩体积，改成排除对应的 `CH58x_usbhost*.c` / `CH58x_usb2*.c`。

### 62. 左右两份固件近乎重复：实质差异只有 5 处，其余 190 行 diff 全是编码损坏

**位置**：[`firmware/ch583-insole`](../firmware/ch583-insole)

把左右 `peripheral.c` 都转成 UTF-8 + LF 后 `diff` 出 195 行改动，逐条看下来实质差异只有 5 处：
1. 扫描响应包名字末字节 `'L'`（左 :228）vs `'R'`（右 :215）；
2. 通道映射表 —— 左 `LEFT_FOOT_MAP`（文件作用域 static const，:140-143，带 TODO）vs 右 `TX_SENSOR_MAP`（塞在 `performPeriodicTask` 里当函数局部 static，:678-681），两张表排列完全不同；
3. 右脚删掉了 `BMI270_Soft_Ping_Test()` 整个函数（左 :317-342）及其被注释的调用（左 :407），但把它依赖的软 I2C 引擎留在了原地（详见"无人调用"那条 issue）；
4. 串口打印串 `"Sent 80 Binary Pkts! L-Foot ACC_X: %d"`（左 :736）vs `"Sent 80 Binary Pkts (Right Foot)! ACC_X: %d"`（右 :703）；
5. `SBP_PERIODIC_EVT_PERIOD` 的注释（左 80 Hz / 右 40 Hz，值都是 20）。

`peripheral_main.c` 92 行，实质差异只有 1 处：`MacAddr[6]` 末字节 0x02 / 0x03（:33，而且当前不生效）。`peripheral.h` 实质差异 0 处（只差一个空行）。`.cproject` / `.project` / `.template` / `Peripheral.wvproj` / `Peripheral.launch` 左右**逐字节相同**（`diff` 输出为空）。Bosch 5 个驱动文件和 WCH 4 个 Profile 文件左右 md5 全部一致。

影响：改一处逻辑要改两份、还得先跨过右脚那堆乱码注释，长期维护必然发散（现在已经发散了——右脚少了排障函数、注释写错了采样率、映射表还挪了位置）。建议合并成单份 + `-DINSOLE_LEFT` / `-DINSOLE_RIGHT` 编译宏，具体做法和 5 个替换点见 build_instructions 末节。

### 63. 接收板 main.c 混用 FreeRTOS 原生 API 与 SDK OS 抽象层，锁死在 FreeRTOS 上且绕过了 SDK 的移植层

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

`接收/app/main.c` 同时用了两套 API：SDK 的 OS 抽象（`sys_os_init` / `sys_task_create_dynamic` / `sys_ms_sleep`，见 `:159, 264, 267, 312`）和 FreeRTOS 原生调用（`xQueueCreate` / `xQueueSend` / `xQueueReceive` / `xTaskGetTickCount`，见 `:261, 234, 277, 317`，头文件 `:60-62`）。

影响：SDK 明确支持另一套 `PLATFORM_OS_RTTHREAD` 编译分支（同文件 `:130-146` 和 `:172-181` 就是这个分支的代码，作者还保留着），但一旦切到 RT-Thread，这些 `xQueue*` / `xTaskGetTickCount` 全部不存在，编译直接失败——也就是作者代码只在 FreeRTOS 分支下成立，而这个约束没在任何地方写明。另外用原生 API 也让这份代码不能直接搬到 SDK 的其他 OS 配置上。属于清理项而非阻塞问题：SDK 的 `wrapper_os.h` 提供了对应的队列封装，替换成 `sys_queue_*` 系列即可两边通吃。建议至少在文件顶部加一句「本文件仅在 PLATFORM_OS_FREERTOS 配置下可编译」。

### 64. 接收板 app_conn_mgr.c 里注释与代码自相矛盾，同一个拦截器被重复注册；另有一处死代码

**位置**：[`firmware/gd32vw553-receiver`](../firmware/gd32vw553-receiver)

三处小问题，都是排查期留下的残渣：

1. **注释与代码打架 + 重复注册**：`接收/ble/app/app_conn_mgr.c:390` 的注释明写「(不要在这里调 ble_gattc_svc_reg)」，但同一函数 `:387`（就在注释上方 3 行）**恰恰就在调它**。而 `:906` 的 `app_conn_mgr_init` 里已经全局注册过一次同样的 `0xFFE0 → insole_data_recv_cb`。于是每建立一次连接就重复注册一次，注册返回码被打印成「拦截器注册状态: 0x%x (0 代表完美挂载)」，第二次起大概率是非 0，日志会显示一个看起来像失败的状态码，误导排查。二者留一即可（留 `:906` 的全局注册，删 `:387`）。

2. **死代码**：`接收/app/main.c:378-385` 的 `set_lung_brightness()` 定义了但**全项目零调用**——`led_organic_task` 在 `:406-407` 直接自己算 pulse 写寄存器。同时 `:337` 的注释说「PA3/PA4 呼吸肺 (PWM)」，实际只有 PA3 是 PWM，PA4 是纯 GPIO 翻转（`:352, 411-412`），注释误导。

3. **未使用字段**：`接收/app/main.c:228` 写入 `msg.len = len + 1`，但 `ble_parse_task`（`:273-294`）从头到尾只用 `msg.data`，`msg.len` 永远不被读；而且 NUL 终止符实际写在 `data[len+1]`（`:231`），有效长度应该是 `len+2`，这个值本身也是错的（只是恰好没人用）。

影响：都不影响功能，但开源代码里这类矛盾注释最消耗读者信任，建议一并清掉。

### 65. 帧缓存数组维度写反（[800][480] 应为 [480][800]）

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`Hardware/RGB/lcd.c:8` 与 `Hardware/RGB/lcd.h:41`：`uint16_t ltdc_lcd_framebuf0[800][480] __attribute__((at(0xC0000000)))`。屏幕是 800 宽 × 480 高，按 C 的行优先约定应写成 `[480][800]`。总字节数 768 000 与正确写法完全相同，TLI 只认起始地址，所以现在跑得好；`lv_port_disp_template.c:130` 也是手工算 `2*(800*y1+x1)` 绕开了下标。但 `Hardware/RGB/lcd_ui.c` 里的 `tli_draw_point` 等一系列函数都引用了这个数组（.map:681-698 有 8 处引用），任何按 `framebuf[y][x]` 直觉写的新代码都会算出错误偏移、把像素画到别的行去，且越界不报错（SDRAM 32 MB 全可写）。属于"能跑但埋雷"的声明错误。

### 66. 解析处把左右脚交叉赋值，此后变量名与协议字段永久相反

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`User/main.c:173-176`：注释 `// 🎯 修复左右脚反转`，然后 `memcpy(g_left_adc, p_data->right_adc, ...)`、`memcpy(g_left_imu, p_data->right_imu, ...)`、`memcpy(received_adc_values, p_data->left_adc, ...)`。也就是名叫 `g_left_*` 的变量装的是包里的 right 字段，名叫 `received_adc_values`（历史上代表右脚）的装的是 left 字段。真正的根因大概在 VW553 侧两个 Central 的连接顺序（谁先连上就是 left），在 F470 用交叉 memcpy 打补丁只是把问题挪了个地方，代价是从此没人能靠变量名判断哪只脚。同一处还留着 `g_wireless_adc` / `g_wireless_imu` 两个从未被写入的哑数组（main.c:34-36，注释自称"专门用来糊弄 lcd_mytest.c，防止编译报错 L6218E"）。建议：在 VW553 侧用 BLE 地址锚定左右，F470 侧恢复直连赋值并删掉哑数组。

### 67. 文件编码三国杂处：同一模块 .c 是 UTF-8、.h 是 GBK，另有两份中文注释已被存成 "?"

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

实测（`file` 命令）：`User/main.c`、`Hardware/usart/bsp_usart.c`、`bsp_usart.h`、`Hardware/lcd_my_test/lcd_mytest.c`、`Hardware/si24r1/si24r1_soft_spi.c` 是 UTF-8；`Hardware/lcd_my_test/lcd_mytest.h` 是 GBK 且注释已损坏（第 16 行 （原注释字节已损坏，无法恢复））；`User/main.h:46`、`User/gd32f4xx_it.c:47` 的中文注释是 `/* ????? */`；`LVGL/lv_conf.h`（:775-780 那段 IPA 说明）和 `LVGL/porting/lv_port_disp_template.c`（:48-49、:116-148 大段说明）已经是纯 ASCII——中文全被某个编辑器替换成了 `?`，注释信息永久丢失。之所以还能编译，是靠 uvprojx 里的 `--no-multibyte-chars`。真正的风险：`lcd_mytest.c` 里的中文标签（"痛性跛行" 等，:31-34）是**运行期字符串**，LVGL 必须拿 UTF-8 才能在 `ui_font_cn_48` 里查到字模；一旦有人在 Keil 里按默认 GB2312 打开并保存这个文件，屏幕上的中文大字报会整片变成方块或空白，而编译毫无警告。建议：仓库统一转 UTF-8（无 BOM），并在 README 里写明 Keil 要设 Encoding = UTF-8。

### 68. 大量死代码与哑函数：六个空实现、三个哑数组，以及八个编译但从未调用的 BSP 模块

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

(a) `Hardware/lcd_my_test/lcd_mytest.c:179-185` 六个"哑函数防报错"：`create_adc_btn`（转调 create_dashboard_ui）、`update_uart_adc_display`、`update_imu_display`、`create_uart_adc_display`、`update_adc_display`、`lcd_rgb_config` 全是空壳，头文件 :36-42 还在声明它们。(b) `User/main.c:34-36` 三个哑数组，注释自称糊弄编译器。(c) uvprojx 的编译清单里 `Hardware/si24r1/si24r1_soft_spi.c`（旧 2.4 GHz 私链）、`Hardware/adc/bsp_adc.c`（PC1 单通道例程 ADC）、`Hardware/zzu/zzu.c`（校徽点阵）、`Hardware/SD/sdcard.c`（2526 行）、`Hardware/spi/bsp_w25q64.c`、`Hardware/rtc/bsp_rtc.c`、`Hardware/key/bsp_key.c`、`Hardware/timer/bsp_basic_timer.c` 都在编译，但 grep 全工程无一处调用（只有 `bsp_led.c` 的 `led_gpio_config()` 被 main.c:79 用到）。(d) `LVGL/porting/lv_port_indev_template.c` 编进去了，但 `lv_port_indev_init()` 从未被调用——触摸屏没有接进 LVGL，屏幕纯输出。这些占了 396 kB Code+RO 里相当一部分，也让新读者分不清哪条链路是活的。

### 69. printf 被重定向到 UART4（上游 VW553 那根线），而不是接上位机的 UART3

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`Hardware/usart/bsp_usart.c:118-122` 的 `fputc` 走 `usart_send_data()`，后者写的是 `BSP_USART` = UART4（`bsp_usart.h:18`），TX 引脚 PC12 正是与 VW553 对接的那一路。于是 `main.c:87` 的 `printf("\r\nSystem Booting...\r\n")` 会把调试文本打到传感器链路上（VW553 侧不收，所以目前无害），而真正插着 USB-TTL 的 UART3(PC10) 反而看不到任何调试输出。调试口和数据链路混在一根线上，将来任何在 task 里加 printf 的行为都会往上游链路灌垃圾。建议：把 `fputc` 改到 UART3，或单独留一路 USART0 做调试口。

### 70. 屏幕右下角的 FPS/CPU 叠层是 LVGL 的性能监视器，不是推理帧率；而且它的数值本身不可信

**位置**：[`firmware/gd32f470-hub`](../firmware/gd32f470-hub)

`LVGL/lv_conf.h:285-287` 打开了 `LV_USE_PERF_MONITOR 1` + `LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT`，那个小小的 "xx FPS / yy%" 是 LVGL 自己统计的**屏幕重绘帧率和 CPU 占用**，与 GD32H737 的推理速率（每 15 帧/500 ms 一次，`h737vmt6_AI1.0/main.c:36`）毫无关系。答辩/演示视频里极容易被当成"推理帧率"读。更进一步，这个数值连 UI 帧率都不准：`main.c:129-131` 是在任务里手工 `lv_tick_inc(5)` 然后 `vTaskDelay(pdMS_TO_TICKS(5))`，而 `lv_conf.h:88` 的 `LV_TICK_CUSTOM = 0`——LVGL 的时基跟 SysTick 不同源，每轮实际耗时超过 5 ms 时 LVGL 仍按 5 ms 记账，于是 FPS 被系统性高估、CPU% 被低估。屏幕上真正与业务相关的两个数是左下角 `EVAL: n%`（一分钟窗口进度）和右下角 `FM: nnnnnn`（H737 回传的帧号，`lcd_mytest.c:126`）。建议：演示时关掉 PERF_MONITOR，或在 UI 上把它标注成 "LVGL FPS"；并把 `LV_TICK_CUSTOM` 接到真实 tick 上。

### 71. 全链路用 for(volatile) 忙等做延时，与 600 MHz 主频耦合，且注释里的秒数与实际不符

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：
- `main.c:147` `for(volatile int delay = 0; delay < 200000000; delay++);` —— `main - 副本.c` 里同位置的注释写「傻等 2 秒钟」，但 2 亿次 volatile 循环在 600 MHz 下按约 4 周期/次估算约 **1.3 s**，与注释的 2 s 不符（且这个数字随编译器版本、优化等级、Cache 命中情况漂移）。
- `main.c:148-152` 用 `idle_count < 1000000` 次连续读高电平判定总线静默，同样是纯循环计数、没有时间量纲。
- `main.c:243` 发送前 `for(volatile int delay=0; delay<200000; delay++);`。
- `main.c:248` 每发一个字节后 `for(volatile int nop=0; nop<150; nop++);`，94 字节全发完要绕 94 次。

**影响**：换编译器版本、改优化等级（现在是 `<Optim>2`）、或改主频，所有这些"延时"都会跟着变，上电同步时序（这段代码作者自己叫"护城河"）可能失效。而且主循环里这些忙等期间 DMA 仍在环形接收，`main.c:243` 那段约 1.3 ms 的空转 + 逐字节发送叠加起来会吃掉 33 ms 帧周期里的一部分余量。

**建议**：改用 DWT->CYCCNT 或已经编进工程却没接上的 SysTick（见上面那条 issue）来做有明确时间量纲的延时。

### 72. 上位机注释「21 帧 (1500ms 窗口成型)」是旧版遗留，与 window_size = 45 矛盾

**位置**：[`firmware/gd32h737-inference`](../firmware/gd32h737-inference)

**证据**：`4.工程代码/5.上位机代码/smart_insole_display10.py:335-336` 实际值是 `self.window_size = 45` / `self.inference_step = 15`（与固件 `main.c:33`、`:36` 一致，没问题），但 `:611` 的注释仍写「当队列刚满 **21 帧**时 (1500ms 窗口成型)」、`:615` 写「每滑动 **7 帧**触发一次 WebAssembly 预测」。

**影响**：纯注释错误，不影响运行。但 21 帧 @30 Hz = 700 ms 而不是 1500 ms，这句注释是从 21 帧那一代原封不动搬过来的，会让读者以为窗口是 21 帧、进而以为仓库里那份 21 帧训练脚本是对的（实际不是，见第一条 issue）。开源前顺手改掉，免得三处（固件 45 / 脚本 21 / 注释 21）互相打架。

### 73. 上位机硬编码了作者本机的 WSL 路径，且 os.makedirs 没有异常保护

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:740：
```python
target_dir = r"<作者本机的一个绝对路径>"
```
紧接着 :743-744 无条件创建它：
```python
if not os.path.exists(target_dir):
    os.makedirs(target_dir)
```
然后 :748 拼成默认文件名传给 QFileDialog。

三个问题：
1. 这是作者 WSL 环境下的 Windows D 盘挂载路径，对任何其他人都无意义。
2. `os.makedirs` 不在 try 里。在 Linux/macOS 上，非 root 用户对 `/workspace` 无写权限会抛未捕获的 `PermissionError`，直接从 `save_data_prompt` 冒出去——而这个函数是 QPushButton 的槽（:311），异常会被 Qt 事件循环吞掉或打印到 stderr，界面上什么反应都没有，用户只会觉得「导出按钮点了没用」。
3. 就算有权限，也会在别人的文件系统根目录下凭空造出一串带中文的空目录树（Windows 上会造出一串带中文的空目录树）。

修复：改成 `os.path.expanduser("~/insole_data")` 或空串（让 QFileDialog 用当前目录），并把 makedirs 包进 try/except 后在 UI 上给出提示。

### 74. 127.0.0.1:8080 推理服务未随仓库开源，「开启 AI 预测」按钮开箱即坏

**位置**：[`host-app`](../host-app)

smart_insole_display10.py:663-713 的 `send_to_nodejs` 向 `http://127.0.0.1:8080/predict` POST `{"features": [...]}`，期望回 `{"classification": {类名: 分数, ...}}`。

这个服务在仓库里不存在：`find . \( -name "*.js" -o -name "package.json" -o -name "*.wasm" \)` 全仓库零命中。它是作者本机自己写的一层壳——注意 Edge Impulse 的 Node.js WASM 导出包，其 `EdgeImpulseClassifier.classify()` 原生返回的是 `{results: [{label, value}, ...], anomaly}` 形状，与代码期望的 `{classification: {label: value}}` 字典不同，所以中间必然有一段作者写的转换逻辑，这段逻辑也一并丢失了。

后果：别人点「▶ 开启 AI 预测」，:708-711 的 except 会捕获连接错误，界面显示「🤖 模型推断错误」并在 stderr 打一行「❌ 通信出错」。功能完全不可用，且看不出原因是缺服务而不是模型有问题。

补救不必重新开源那个服务（它本来也只是十几行 Express），但 README 必须写清三件事：(1) 这个服务不在仓库里；(2) 怎么从 EI 的 WebAssembly (Node.js) 部署包搭一个等价的（含 results→classification 的形状转换）；(3) 搭好之后还必须先修掉展平顺序和滑窗长度两个 bug，否则服务通了结果依然是错的。

另外这条链路上还有个效率问题：:667 每次推理都新建一个 `aiohttp.ClientSession()`，按 inference_step=15 帧计算约每 0.5 s 一次，等于每 0.5 s 建一个新连接池。应该在 `__init__` 里建一个长生命周期的 session 复用。

### 75. 数据集缺少许可、采集说明与被试知情记录；类别帧数不均衡

**位置**：[`host-app`](../host-app)

57 个 CSV / 29 798 帧 / 993.3 s / 4.3 MB，从 mtime 看采集集中在 2026-06-14 单日 10:38–17:24。目前这个目录里只有裸 CSV，没有 README、没有 LICENSE、没有采集协议、没有被试信息。

开源前必须补齐的信息（我在下面的「数据集要不要开源」结论里给了完整清单）：
1. 单被试单日采集，被试身份/身高/体重/鞋码未记录
2. 无伦理审查记录（学生作品，建议补一句「数据由作者本人采集、被试即作者本人」作为知情说明的替代，并明确说明这不等同于 IRB 批准）
3. 3 个病理步态类为健康被试模拟
4. 15.19% 零阶保持假帧
5. 2 路死通道 L_ADC_2 / L_ADC_14
6. 左右基线差 2.13 倍
7. ADC 为无单位原始计数（2–526），未做力标定；IMU 为 BMI270 原始 LSB，加速度计经实测反推为 ±8 g（4096 LSB/g），陀螺仪推断 ±2000 dps 但无配置代码证据；IMU 安装朝向未记录
8. CSV 的 L_* 列物理上来自报文 right_* 半区

另外类别帧数明显不均衡：
| 类别 | 文件数 | 帧数 | 秒数 |
|---|---|---|---|
| eye | 1 | 4265 | 142.2 |
| band | 1 | 4200 | 140.0 |
| upstair | 25 | 4152 | 138.4 |
| unnormal_singol | 1 | 4057 | 135.2 |
| normal_walk | 1 | 4011 | 133.7 |
| sit | 2 | 4007 | 133.6 |
| downstair | 25 | 3698 | 123.3 |
| **stand** | 1 | **1408** | **46.9** |

stand 只有其他类的三分之一，训练时需要加权或重采样。而且 6 个类是单文件长录（4000 帧一段），只有 upstair/downstair 是多段短录——按窗口切样本后，单文件类的所有样本都来自同一次连续录制，**训练/验证集若随机切分会发生严重的时序泄漏**（相邻窗口高度重叠），95.5% 这个验证集准确率很可能因此虚高。这一点比上面任何一条都更影响那个数字的可信度，README 里应当写明。

代码侧对应的缺失：仓库里没有 requirements.txt（依赖只能从 import 反推），也没有为数据集单独指定许可。

