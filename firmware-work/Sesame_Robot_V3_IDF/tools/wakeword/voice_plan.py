"""Fixed, reproducible voice split for 你好芝麻 wake-word training."""

VOICE_PLAN = (
    {"name": "Xiaoxiao", "voice_id": "zh-CN-XiaoxiaoNeural", "split": "train"},
    {"name": "Xiaoyi", "voice_id": "zh-CN-XiaoyiNeural", "split": "train"},
    {"name": "Yunxi", "voice_id": "zh-CN-YunxiNeural", "split": "train"},
    {"name": "Yunyang", "voice_id": "zh-CN-YunyangNeural", "split": "train"},
    {"name": "Yunjian", "voice_id": "zh-CN-YunjianNeural", "split": "train"},
)

TARGET_PHRASE = "你好，芝麻"
HARD_NEGATIVE_PHRASE = "你好，小智"
GENERAL_NEGATIVE_PHRASES = (
    "你好，机器人",
    "芝麻开门",
    "请播放音乐",
)
