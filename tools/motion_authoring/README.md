# Sesame V3 自然语言动作模拟器

这个原型验证一条具体链路：

```text
“快速游泳 3 次”
  → MotionIntent（动作、风格、周期、幅度）
  → dog_paddle 运动基元
  → V3 的 8 路原始舵机角度
  → MotionClip JSON + 浏览器动画预览
```

它使用现有 V3 固件的真实顺序 `R1,R2,L1,L2,R4,R3,L3,L4`，以及固件现有站立姿态
`135,45,45,135,0,180,0,180`。当前自然语言解析器是可复现的规则模型；以后可以由大模型输出同样的
`MotionIntent`，而舵机映射、限位和轨迹生成仍由确定性程序控制。

## 运行

在项目根目录执行：

```bash
python3 -m tools.motion_authoring.cli "快速游泳 3 次" \
  --json output/motion/swim.motion.json \
  --preview output/motion/swim.preview.html
```

浏览器打开 `output/motion/swim.preview.html` 可播放、暂停和逐帧查看 8 个角度。

测试：

```bash
python3 -m unittest tools.motion_authoring.test_motion_authoring -v
```

## 当前边界

- 这是简化运动学模型，不计算重力、质心、碰撞、舵机扭矩和机身结构干涉。
- `robot_model.json` 暂用固件的 0–180° 软件范围。真机必须逐路测量 `min/max/home/direction` 后替换。
- 首个运动基元只实现游泳；新增动作时增加新的语义基元，而不是让大模型任意输出未校验角度。
- 输出 JSON 已经是动作数据，但现有 V3 固件还没有 MotionClip 播放器；接入 ESP32-S3 需要实现解析、存储、校验和非阻塞播放。
