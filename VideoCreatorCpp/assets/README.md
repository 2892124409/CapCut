# 资源文件目录

这个目录用于存放视频合成所需的资源文件。

## 需要的文件：

1. **image1.jpg** - 开场场景图片
2. **image2.jpg** - 结束场景图片  
3. **video1.mp4** - 主场景视频
4. **audio1.mp3** - 开场场景音频
5. **audio2.mp3** - 主场景音频

## 文件要求：

- 图片：支持 JPG、PNG 格式
- 视频：支持 MP4、MOV 等常见格式
- 音频：支持 MP3、WAV 等常见格式

## 使用说明：

将您的资源文件放在此目录下，然后在 `config.json` 中配置相应的文件路径。

例如：
```json
{
  "path": "assets/image1.jpg",
  "type": "IMAGE"
}
