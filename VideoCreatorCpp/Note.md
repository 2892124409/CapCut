# 转场画面偏色问题分析与解决

## 问题现象
最初所有转场都有颜色漂移，后期优化后仅第三个转场仍偏色。

## 原因分析
### 通用原因
- 转场/特效滤镜链输入的帧格式与色彩元数据不一致（原图多为 RGB/GBR、PC/full range，管线目标是 YUV420P BT.709/TV）
- zoompan/xfade 在属性不一致时会隐式协商色彩空间/范围，插入默认转换，导致转场帧与正常场景帧不一致

### 第三个转场特殊点
- 起始帧来自上一场景的 Ken Burns 最后一帧，之前直接用原始 RGB 帧进入滤镜，隐式转换最明显，导致第三个转场仍偏色

## 解决方案
### 通用修复
- 在进入滤镜前统一将帧显式转换为目标分辨率的 yuv420p，并写入完整的色彩元数据（colorspace BT.709/BT.601、color_range TV、primaries、TRC、SAR）
- 在滤镜源节点（Ken Burns、转场的 buffer source）显式设置相同的色彩参数，输出帧也补齐元数据，避免滤镜链自行猜测
- 图片解码缩放后立即写入色彩元数据，保证进入所有滤镜的帧定义一致

### 第三个转场特殊处理
- 在 renderTransition 中先把"from"源图缩放/转换到目标 yuv420p 并带上色彩元数据，再生成 Ken Burns 序列取最后一帧，确保起始帧与管线色彩一致

***

# Ken Burns/转场导致系统重启问题分析与解决

## 问题现象
渲染带 Ken Burns 或转场的工程时，可执行程序瞬间占用数 GB 到数十 GB 内存，Windows 在内存耗尽后直接强制重启。

## 原因分析
### 通用原因
- EffectProcessor 会把 Ken Burns、转场的所有帧一次性生成并缓存（每帧都是完整的 1080p/4K YUV420P），场景越长、分辨率越高占用越夸张。
- renderTransition 在取起始/结束帧时还会重复运行整段 Ken Burns 序列，进一步放大峰值内存。

## 解决方案
### 通用修复
- 将 EffectProcessor 改成“启动 + 单帧拉取”模式：startKenBurnsSequence / fetchKenBurnsFrame、startTransitionSequence /fetchTransitionFrame，随取随用，不再缓存整段序列。
- renderScene、renderTransition 主循环中按帧调用上述接口，同时保留现有的音频同步与编码逻辑，确保只持有当前帧和编码器缓冲。
- 在需要从 Ken Burns 序列获取最后/第一帧时，只遍历到目标帧立即复制释放，避免为了单帧生成整段缓存。

### 结果
- 峰值内存降到“一两帧 + 编码器 FIFO”的常规水平，长场景或高分辨率项目不再触发系统级 OOM。
- 渲染流程保持原有功能，CPU/GPU 负载按帧推进，整体稳定性恢复。